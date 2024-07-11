#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <ctype.h>
#include <string.h>
#include <memory.h>

#include <tcl.h>
#include <df.h>
#include <tcl_dl.h>

#include <glad/glad.h>

#include <stim2.h>

#include "FgOpenGL.hpp"
#include "Fg3dOpenGL.hpp"
#include "FgMain.hpp"
#include "FgTestUtils.hpp"
#include "Fg3Version.hpp"
#include "Fg3Face.hpp"
#include "Fg3Controls.hpp"
#include "FgStdString.hpp"
#include "FgFileSystem.hpp"
#include "FgTypes.hpp"
#include "FgDiagnostics.hpp"
#include "FgFileSystem.hpp"
#include "FgTime.hpp"
#include "FgImgBase.hpp"
#include "FgImgMipmap.hpp"
#include "Fg3Sam.hpp"
#include "Fg3dMeshOps.hpp"
#include "Fg3dMeshIO.hpp"
#include "FgMetaFormat.hpp"
#include "FgSimilarity.hpp"
#include "Fg3dNormals.hpp"
#include "FgMatrixC.hpp"
#include "FgLighting.hpp"
#include "FgFileSystem.hpp"
#include "Fg3dViewXform.hpp"
#include "FgSingleton.hpp"
#include "FgAffine1.hpp"
#include "FgAffineCwC.hpp"

using namespace std;

#include "facegen.h"


static int FacegenObjID = -1;	/* unique facegen object id */

/****************************************************************/
/*                      Local Datatypes                         */
/****************************************************************/

typedef struct _face {
  char handle[128];
  FG_FACE *face;
  GLuint texids[3];
} FACEGEN_OBJ;


static void faceDelete(GR_OBJ *o) 
{
  FACEGEN_OBJ *g = (FACEGEN_OBJ *) GR_CLIENTDATA(o);
  
  glDeleteTextures(3,g->texids);
  
  free((void *) g);
}

static void faceReset(GR_OBJ *o) 
{
  FACEGEN_OBJ *g = (FACEGEN_OBJ *) GR_CLIENTDATA(o);
}


static void fgUpdateNormals(FG_FACE *face)
{
  face->normals.clear();

  for (uint i = 0; i < face->meshes.size(); i++) {
    Fg3dNormals normals = 
      fgNormals(face->meshes[i].surfaces, face->meshes[i].verts);
    face->normals.push_back(normals);
  }
}


void textureUpdate(
    uint                name,
    const FgImgRgbaUb & img)
{
  // OGL requires power of 2 dimensioned images, and expects them stored bottom to top:
  FgImgRgbaUb     oglImg;
  fgPower2Ceil(img,oglImg);
  fgImgFlipVertical(oglImg);
  FgImgMipmap<FgImgRgbaUb> mipmap(oglImg);
  
  glEnable(GL_TEXTURE_2D);
  // Set this texture "name" as the current 2D texture "target":
  glBindTexture(GL_TEXTURE_2D,name);
  
  GLint tmp;
  glGetIntegerv(GL_MAX_TEXTURE_SIZE,&tmp);
  uint        oglTexMax = static_cast<uint>(tmp);
  uint        dimMax = std::max(oglImg.width(),oglImg.height()),
    jj;
  for (jj=0; dimMax>oglTexMax; jj++) dimMax/=2;
  
  // Load into GPU:
  for (uint ii=jj; ii<=mipmap.levels(); ii++)
    {
      const FgImgRgbaUb  *img;
      if (ii == 0)
	img = &oglImg;
      else
	img = &(mipmap.m_img[ii-1]);
      glTexImage2D(GL_TEXTURE_2D,
		   ii-jj,                 // Mipmap level being specified.
		   4,                     // 4 Channels
		   img->width(),
		   img->height(),
		   0,                     // No border colour
		   GL_RGBA,               // Channel order
		   GL_UNSIGNED_BYTE,      // Channel type
		   img->dataPtr());
    }
  
    // Note that although glTexSubImage2D can be used to update an already loaded
    // texture image more efficiently, this didn't work properly on some machines -
    // It would not update the texture if too much texture memory was being used 
    // (NT4, Gauss) or it would randomly corrupt all the textures (XP, beta tester).
}

uint
textureAdd(const FgImgRgbaUb & img)
{
  glEnable(GL_TEXTURE_2D);
  uint name;
  glGenTextures(1,&name);
  textureUpdate(name,img);
  return name;
}

static void setup_textures(FACEGEN_OBJ *f)
{
  uint i;
  for (i = 0; i < f->face->meshes.size(); i++) {
    f->texids[i] = textureAdd(f->face->meshes[i].texImages[0]);
  }
}

struct  Tri
{
    // Assume CC winding for verts & uvs:
    FgVect3F        v[3];   // verts
    FgVect3F        n[3];   // norms
    FgVect2F        u[3];   // uvs

    FgVect4F
    meanVertH() const
    {return fgAsHomogVec((v[0]+v[1]+v[2]) * 0.33333333f); }
};

static
void
drawTris(const vector<vector<Tri> > & tris)
{
    glBegin(GL_TRIANGLES);
    for (size_t ii=0; ii<tris.size(); ++ii) {
        const vector<Tri> & ts = tris[ii];
        for (size_t jj=0; jj<ts.size(); ++jj) {
            const Tri & t = ts[jj];
            // Texture coordinates are just ignored if gl texturing is off:
            for (uint kk=0; kk<3; ++kk) {
                glTexCoord2fv(&t.u[kk][0]);
                glNormal3fv  (&t.n[kk][0]);
                glVertex3fv  (&t.v[kk][0]); }
        }
    }
    glEnd();
}

static
void
insertTri(
    vector<vector<Tri> > &  tris,
    const Tri &             t,
    const FgMatrix44F &     prj,
    const FgAffine1F &      depToBin)
{
    FgVect4F    mean = prj * t.meanVertH();
    float       dep = mean[2] / mean[3];
    size_t      bin = size_t(depToBin * dep);
    if (bin < tris.size())
        tris[bin].push_back(t);
}

static
void
drawSurfaces(
	     const Fg3dMesh &            mesh,
	     const FgVerts &             verts,
	     const Fg3dNormals &         norms,
	     uint *                      texNames)
{
  // Get the modelview matrix, remove the translational component,
  // and adjust it so that it converts the result from OICS to OXCS 
  // for sphere mapping.
  FgMatrix44F     mvm,prj;
  glGetFloatv(GL_MODELVIEW_MATRIX,&mvm[0]);
  glGetFloatv(GL_PROJECTION_MATRIX,&prj[0]);
  mvm = mvm.transpose();
  prj = prj.transpose() * mvm;
  FgAffine3F      trans(mvm.subMatrix<3,3>(0,0));
  FgAffine3F      oicsToOxcs(FgVect3F(1.0f));
  oicsToOxcs.postScale(0.5f);
  trans = oicsToOxcs * trans;
  for (uint ss=0; ss<mesh.surfaces.size(); ++ss) {
    FgValid<uint>   texName;
    texName = texNames[mesh.getSurfTextureInd(ss)];
    
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    glPolygonMode(GL_FRONT_AND_BACK,GL_FILL);
    glEnable(GL_LIGHTING);
    
    const Fg3dSurface & surf = mesh.surfaces[ss].surf;
    bool    doTex = (texName.valid() && (surf.hasUvIndices()));
    if (doTex) {
      glEnable(GL_TEXTURE_2D);
      glBindTexture(GL_TEXTURE_2D,GLuint(texName.val()));
      // If texture is on, we use two-pass rendering for true spectral and
      // we also use the per-object spectral surface properties. If texture
      // mode is off, all objects are rendered the same and in a single pass.
      float   alpha = 1.0f;
      GLfloat  white[]  = {1.0f, 1.0f, 1.0f, alpha};
      GLfloat  black[]  = {0.0f, 0.0f, 0.0f, 1.0f};
      glMaterialfv(GL_FRONT_AND_BACK,GL_AMBIENT,white);
      glMaterialfv(GL_FRONT_AND_BACK,GL_DIFFUSE,white);
      glMaterialfv(GL_FRONT_AND_BACK,GL_SPECULAR,black);
    }
    else
      {
	glDisable(GL_TEXTURE_2D);
	GLfloat     grey[]  = {0.8f, 0.8f, 0.8f, 1.0f},
	    black[]  = {0.0f, 0.0f, 0.0f, 1.0f},
	      *clr = grey;
	      glMaterialfv(GL_FRONT_AND_BACK,GL_DIFFUSE,clr);
	      glMaterialfv(GL_FRONT_AND_BACK,GL_AMBIENT,clr);
	      glMaterialfv(GL_FRONT_AND_BACK,GL_SPECULAR,black);
      }
    
    // More bins slows things down without helping since depth sort is only approximate anyway:
    const size_t    numBins = 10000;
    FgAffine1F      depToBin(FgVectF2(1,-1),FgVectF2(0,numBins));
    vector<vector<Tri> >    tris(numBins);
    for (uint ii=0; ii<surf.numTris(); ii++) {
      Tri             tri;
      FgVect3UI       triInds = surf.getTri(ii);
      for (uint kk=0; kk<3; ++kk)
	tri.v[kk] = verts[triInds[kk]];
      for (uint kk=0; kk<3; ++kk)
	tri.n[kk] = norms.vert[triInds[kk]];
      if (doTex) {
	FgVect3UI   texInds = surf.tris.uvInds[ii];
	for (uint kk=0; kk<3; ++kk)
	  tri.u[kk] = mesh.uvs[texInds[kk]]; }
      insertTri(tris,tri,prj,depToBin);
    }
    // Surface rasterization is done purely with triangles since some OGL drivers
    // can't handle quads:
    for (uint ii=0; ii<surf.numQuads(); ii++) {
      Tri             tri0,tri1;
      FgVect4UI       quadInds = surf.getQuad(ii);
      for (uint kk=0; kk<3; ++kk) {
	tri0.v[kk] = verts[quadInds[kk]];
	tri1.v[kk] = verts[quadInds[(kk+2)%4]]; 
      }
      for (uint kk=0; kk<3; ++kk) {
	tri0.n[kk] = norms.vert[quadInds[kk]];
	tri1.n[kk] = norms.vert[quadInds[(kk+2)%4]]; 
      }
      if (doTex) {
	FgVect4UI   texInds = surf.quads.uvInds[ii];
	for (uint kk=0; kk<3; ++kk) {
	  tri0.u[kk] = mesh.uvs[texInds[kk]];
	  tri1.u[kk] = mesh.uvs[texInds[(kk+2)%4]]; }
      }
      insertTri(tris,tri0,prj,depToBin);
      insertTri(tris,tri1,prj,depToBin);
    }
    
    glShadeModel(GL_SMOOTH);
    drawTris(tris);
    
    glDisable(GL_BLEND);
  }
}

static void
OglSetup()
{
    
  //    FgSingleton<FgAddGlInfo>::instance();
  
  // Set up OGL rendering preferences:
  
  GLfloat     blackLight[] = {0.0,  0.0,  0.0,  1.0f};
  
  glEnable(GL_POLYGON_OFFSET_FILL);
  
  glLightModelfv(GL_LIGHT_MODEL_AMBIENT,blackLight);  // No global ambient.
  // Calculate spectral reflection approximating viewer at infinity (default):
  glLightModeli(GL_LIGHT_MODEL_LOCAL_VIEWER,0);

  glEnable(GL_DEPTH_TEST);
  
  // Enable rendering of both sides of each polygon as the default by
  // enabling proper lighting calculations for the back (clockwise) side.
  glDisable(GL_CULL_FACE);        // The OGL default
  glLightModeli(GL_LIGHT_MODEL_TWO_SIDE,GL_TRUE); // NOT the OGL default
  //glEnable(GL_CULL_FACE);       // Use this to cull back faces (those facing
  //glCullFace(GL_BACK);          // away from the viewer
  
  glReadBuffer(GL_BACK);          // Default but just in case, for glAccum().
  glDepthFunc(GL_LEQUAL);         // (default LESS) this allows the second render pass to work.
  
  // We probably don't need this anymore:
  glPixelStorei(GL_UNPACK_ALIGNMENT,8);
  
  // Repeat rather than clamp to avoid use of the border
  // color since some OGL 1.1 drivers (such as ATI) always
  // use the border color.
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);
  
  // We use trilinear texturing for quality. To force the use
  // of a single texture map we'd have to keep track of the view
  // window size in pixels.
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);
  glTexEnvf(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_MODULATE);
}

void
OglSetLighting(const FgLighting & lt)
{
    GLint       glLight[] = {GL_LIGHT0,GL_LIGHT1,GL_LIGHT2,GL_LIGHT3};

    glPushMatrix();

    glEnable(GL_LIGHTING);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();               // Lights are transformed by the current MVM
    FgVect4F    amb = fgConcatVert(lt.m_ambient,1.0f);
    glLightfv(glLight[0],GL_AMBIENT,amb.dataPtr());
    for (uint ll=0; (ll<lt.m_lights.size()) && (ll < 4); ll++)
    {
        const FgLight & lgt = lt.m_lights[ll];
        glEnable(glLight[ll]);
        FgVect4F        pos = fgConcatVert(lgt.m_direction,0.0f);
        glLightfv(glLight[ll],GL_POSITION,pos.dataPtr());
        FgVect4F        clr = fgConcatVert(lgt.m_colour,1.0f);
        glLightfv(glLight[ll],GL_DIFFUSE,clr.dataPtr());
    }

    glPopMatrix();
}

static void faceDraw(GR_OBJ *m) 
{
  FACEGEN_OBJ *g = (FACEGEN_OBJ *) GR_CLIENTDATA(m);
  unsigned int i;

  FgLighting lt;
  
  OglSetup();
  OglSetLighting(lt);

  fgUpdateNormals(g->face);
  for (i = 0; i < g->face->meshes.size(); i++) {
    drawSurfaces(g->face->meshes[i],
		 g->face->meshes[i].verts,
		 g->face->normals[i],
		 &g->texids[i]);
  }
}

static void faceUpdate(GR_OBJ *m) 
{
  FACEGEN_OBJ *g = (FACEGEN_OBJ *) GR_CLIENTDATA(m);
}

static int faceCreate(OBJ_LIST *olist, FG_FACE *fg, char *handle)
{
  const char *name = "Face";
  GR_OBJ *obj;
  FACEGEN_OBJ *g;
  
  obj = gobjCreateObj();
  if (!obj) return -1;

  strcpy(GR_NAME(obj), name);
  GR_OBJTYPE(obj) = FacegenObjID;

  GR_ACTIONFUNCP(obj) = faceDraw;
  GR_RESETFUNCP(obj) = faceReset;
  GR_DELETEFUNCP(obj) = faceDelete;
  GR_UPDATEFUNCP(obj) = faceUpdate;

  g = (FACEGEN_OBJ *) calloc(1, sizeof(FACEGEN_OBJ));
  GR_CLIENTDATA(obj) = g;
  g->face = fg;
  strcpy(g->handle, handle);

  setup_textures(g);

  return(gobjAddObj(olist, obj));
}

static int facegenObjCmd(ClientData clientData, Tcl_Interp *interp,
			 int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  int id;
  FG_FACE *fg;

  if (argc < 2) {
    Tcl_AppendResult(interp, "usage: ", argv[0], " face", NULL);
    return TCL_ERROR;
  }

  if (fgFindFace(interp, argv[1], &fg) != TCL_OK) {
    Tcl_AppendResult(interp, argv[0], ": face \"", argv[1], 
		     "\" not found", (char *) NULL);
    return TCL_ERROR;
  }
  

  if ((id = faceCreate(olist, fg, argv[1])) < 0) {
    sprintf(interp->result,"%s: unable to create face", argv[0]);
    return(TCL_ERROR);
  }
  
  sprintf(interp->result,"%d", id);
  return(TCL_OK);
}


static int facegenObjHandleCmd(ClientData clientData, Tcl_Interp *interp,
			       int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  int id;
  FACEGEN_OBJ *g;
  
  if (argc < 2) {
    Tcl_AppendResult(interp, "usage: ", argv[0], " facegenObj", NULL);
    return TCL_ERROR;
  }
  
  if (Tcl_GetInt(interp, argv[1], &id) != TCL_OK) return TCL_ERROR;
  if (id >= OL_NOBJS(olist)) {
    Tcl_AppendResult(interp, argv[0], ": objid out of range", NULL);
    return TCL_ERROR;
  }
  
  /* Make sure it's a shader object */
  if (GR_OBJTYPE(OL_OBJ(olist,id)) != FacegenObjID) {
    Tcl_AppendResult(interp, argv[0], ": object not of type facegenObj", NULL);
    return TCL_ERROR;
  }

  g = (FACEGEN_OBJ *) GR_CLIENTDATA(OL_OBJ(olist,id));
  Tcl_SetResult(interp, g->handle, TCL_VOLATILE);
  return(TCL_OK);
}


#ifdef __cplusplus
extern "C" {
#endif
int Fg_Init(Tcl_Interp *interp)
{
  OBJ_LIST *OBJList = getOBJList();
  const GLubyte* renderer;
  const GLubyte* version;

  if (
#ifdef USE_TCL_STUBS
      Tcl_InitStubs(interp, "8.5", 0)
#else
      Tcl_PkgRequire(interp, "Tcl", "8.5", 0)
#endif
      == NULL) {
    return TCL_ERROR;
  }
  Tcl_PkgRequire(interp, "dlsh", "1.2", 0);
  Tcl_PkgRequire(interp, "facegen", "0.68", 0);

  if (FacegenObjID < 0) FacegenObjID = gobjRegisterType();
  
  gladLoadGL();

  Tcl_CreateCommand(interp, "facegenObj", 
		    (Tcl_CmdProc *) facegenObjCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);

  Tcl_CreateCommand(interp, "facegenObjHandle", 
		    (Tcl_CmdProc *) facegenObjHandleCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);

  return TCL_OK;
}


int Fg_SafeInit(Tcl_Interp *interp)
{
  return Fg_Init(interp);
}


int Fg_Unload(Tcl_Interp *interp)
{
  interp = interp;
  return TCL_OK;
}

int Fg_SafeUnload(Tcl_Interp *interp)
{
  return Fg_Unload(interp);
}

#ifdef __cplusplus
}
#endif


