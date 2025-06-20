/*
 * NAME
 *      points.c
 *
 * DESCRIPTION
 *      Optimized routines for picking stimulus locations
 *
 * DETAILS
 *      
 *
 * AUTHOR
 *      
 *
 * DATE
 *      
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#if defined(WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#undef WIN32_LEAN_AND_MEAN
#pragma warning (disable:4244)
#pragma warning (disable:4305)
#define DllEntryPoint DllMain
#define EXPORT(a,b) __declspec(dllexport) a b
#else
#define DllEntryPoint
#define EXPORT a b
#include <unistd.h>
#endif

#include <tcl.h>
#include <sys/types.h>

#include <df.h>
#include <dfana.h>
#include <tcl_dl.h>

#ifndef PI
#define PI 3.141592654
#endif

static DYN_LIST *pickpoints (int n, int old, float xs[], float ys[], 
			     float mindist, float maxx, float maxy);
static int pickpointsCmd (ClientData, Tcl_Interp *, int, char **);

static DYN_LIST *pickpoints_ecc (int n, int old, float xs[], float ys[], 
				 float mindist, float maxx, float maxy,
				 float ecc, float anchor[]);
static int pickpointsEccCmd (ClientData, Tcl_Interp *, int, char **);

static DYN_LIST *pickpoints_away_from (int n, int old, float xs[], float ys[], 
				       float mindist, float maxx, float maxy,
				       float ecc, float anchor[]);
static int pickpointsAwayFromCmd (ClientData, Tcl_Interp *, int, char **);



/************************************************************************/
/*                            Points_Init                               */
/************************************************************************/

#ifdef WIN32
EXPORT(int,Points_Init) _ANSI_ARGS_((Tcl_Interp *interp))
#else
     int Points_Init(Tcl_Interp *interp)
#endif
{
  if (
#ifdef USE_TCL_STUBS
      Tcl_InitStubs(interp, "8.5", 0)
#else
      Tcl_PkgRequire(interp, "Tcl", "8.5", 0)
#endif
      == NULL) {
    return TCL_ERROR;
  }


  Tcl_Eval(interp, "namespace eval ::points {}");
  Tcl_CreateCommand(interp, "::points::pickpoints", 
		    (Tcl_CmdProc *) pickpointsCmd, 
		    (ClientData) NULL, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "::points::pickpointsEcc", 
		    (Tcl_CmdProc *) pickpointsEccCmd, 
		    (ClientData) NULL, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "::points::pickpointsAwayFrom", 
		    (Tcl_CmdProc *) pickpointsAwayFromCmd, 
		    (ClientData) NULL, (Tcl_CmdDeleteProc *) NULL);

  return TCL_OK;
}



/*****************************************************************************
 *
 * FUNCTION
 *    pickpoints
 *
 * ARGS
 *    number of desired points
 *    current points as xs and ys array
 *    minimum distance between points
 *    maximum radius 
 *
 * RETURNS
 *    dynamic list of n (x,y) pairs of new points
 *
 * DESCRIPTION
 *    Choose n new points (x,y) pairs at least mindist from each other
 *    and within maxx and maxy.    
 *
 *****************************************************************************/
static DYN_LIST *pickpoints (int n, int old, float xs[], float ys[], float mindist, float maxx, float maxy)
{
  int tries, i, k, failed;
  float this_x, this_y, xdiff, ydiff, dist2, mindist2;
  float *new_xs, *new_ys;

  DYN_LIST *new_points, *p;

  new_xs = calloc(n, sizeof(float));
  new_ys = calloc(n, sizeof(float));

  mindist2 = mindist * mindist;
      
  for (i = 0; i < n; i++) {
    tries = -1;

    while (++tries < 100000) {
      failed = 0;

      this_x = maxx * 2 * (frand() - 0.5);
      this_y = maxy * 2 * (frand() - 0.5);
      
      for (k = 0; k < old; k++) {
	xdiff = this_x - xs[k];
	ydiff = this_y - ys[k];
	dist2 = xdiff*xdiff + ydiff*ydiff;
	if (dist2 < mindist2) {
	  failed = 1;
	  break;
	}
      }
      if (failed) continue;
      for (k = 0; k < i; k++) {
	xdiff = this_x - new_xs[k];
	ydiff = this_y - new_ys[k];
	dist2 = xdiff*xdiff + ydiff*ydiff;
	if (dist2 < mindist2) {
	  failed = 1;
	  break;
	}
      }
      if (failed) continue;

      //* if we get this far then we have a valid new point
      new_xs[i] = this_x;
      new_ys[i] = this_y;
      break;
    }
    /*
      if (tries  == 100000) {
      // error, couldn't choose a point in 100000 tries
      return NULL;
      }
    */
  }

  //* now we have to pack up new_xs and new_ys into a DYN_LIST called new_points
  new_points = dfuCreateDynList(DF_LIST, n);
  p = dfuCreateDynList(DF_FLOAT, 2);
  for (i = 0; i < n; i++) {
    dfuResetDynList(p);
    dfuAddDynListFloat(p, new_xs[i]);
    dfuAddDynListFloat(p, new_ys[i]);
    dfuAddDynListList(new_points, p);
  }

  free(new_xs);
  free(new_ys);

  return new_points;
}


/*****************************************************************************
 *
 * Function
 *    pickpointsCmd
 *
 * ARGS
 *    Tcl Args
 *
 * DLSH FUNCTION
 *    points::pickpoints
 *
 * DESCRIPTION
 *    interface between TCL and pickpointsList
 *    calls pickpoints, but for each element of the lists entered.
 *
 *****************************************************************************/

static int pickpointsCmd (ClientData data, Tcl_Interp *interp,
			      int argc, char *argv[])
{
  int i, j, length;
  int this_nold, this_ndist;
  int *ndist_vals;
  float this_mindist, this_maxx, this_maxy;
  float *xs, *ys, *mindist_vals, *maxx_vals, *maxy_vals, *vals;
  DYN_LIST *ndist, *points, *mindist, *maxx, *maxy;
  DYN_LIST *curlist, *cursublist, *retlist;
  DYN_LIST **sublists, **subsublists;
  
  if (argc < 6) {
    Tcl_AppendResult(interp, "usage: ", argv[0], " n points mindist maxx maxy", NULL);
    return TCL_ERROR;
  }
  
  if (tclFindDynList(interp, argv[1], &ndist) != TCL_OK) return TCL_ERROR;
  if (DYN_LIST_DATATYPE(ndist) != DF_LONG) {
    Tcl_AppendResult(interp, argv[0], ": ndist must be of type integer", NULL);
    return TCL_ERROR;
  }
  length = DYN_LIST_N(ndist);

  if (tclFindDynList(interp, argv[2], &points) != TCL_OK) return TCL_ERROR;
  if (DYN_LIST_DATATYPE(points) != DF_LIST) {
    Tcl_AppendResult(interp, argv[0], ": invalid points data", NULL);
    return TCL_ERROR;
  }
  if (DYN_LIST_N(points) != length) {
    Tcl_AppendResult(interp, argv[0], ": points must be length of ndist", NULL);
    return TCL_ERROR;
  }
  // get sublist and check type, will have to pack points if sublist is FLOAT list
  sublists = (DYN_LIST **) DYN_LIST_VALS(points);
  for (i = 0; i < DYN_LIST_N(points); i++) {
    curlist = (DYN_LIST *) sublists[i];
    if (DYN_LIST_DATATYPE(curlist) != DF_LIST) {
      Tcl_AppendResult(interp, argv[0], ": invalid points data (might try packing it)", NULL);
      return TCL_ERROR;
    }
    
    subsublists = (DYN_LIST **) DYN_LIST_VALS(curlist);
    for (j = 0; j < DYN_LIST_N(curlist); j++) {
      cursublist = (DYN_LIST *) subsublists[j];
      if (DYN_LIST_N(cursublist) != 2 || 
	  DYN_LIST_DATATYPE(cursublist) != DF_FLOAT) {
	Tcl_AppendResult(interp, argv[0], ": invalid points list", NULL);
	return TCL_ERROR;
      }      
    }
  }

  if (tclFindDynList(interp, argv[3], &mindist) != TCL_OK) return TCL_ERROR;
  if (DYN_LIST_DATATYPE(mindist) != DF_FLOAT) {
    Tcl_AppendResult(interp, argv[0], ": mindist must be or type float", NULL);
    return TCL_ERROR;
  }
  if (DYN_LIST_N(mindist) != length) {
    Tcl_AppendResult(interp, argv[0], ": mindist must be length of ndist", NULL);
    return TCL_ERROR;
  }

  if (tclFindDynList(interp, argv[4], &maxx) != TCL_OK) return TCL_ERROR;
  if (DYN_LIST_DATATYPE(maxx) != DF_FLOAT) {
    Tcl_AppendResult(interp, argv[0], ": maxx must be or type float", NULL);
    return TCL_ERROR;
  }
  if (DYN_LIST_N(maxx) != length) {
    Tcl_AppendResult(interp, argv[0], ": maxx must be length of ndist", NULL);
    return TCL_ERROR;
  }

  if (tclFindDynList(interp, argv[5], &maxy) != TCL_OK) return TCL_ERROR;
  if (DYN_LIST_DATATYPE(maxy) != DF_FLOAT) {
    Tcl_AppendResult(interp, argv[0], ": maxy must be or type float", NULL);
    return TCL_ERROR;
  }
  if (DYN_LIST_N(maxy) != length) {
    Tcl_AppendResult(interp, argv[0], ": maxy must be length of ndist", NULL);
    return TCL_ERROR;
  }


  //change all dynlists into arrays
  ndist_vals = (int *) DYN_LIST_VALS(ndist);
  mindist_vals = (float *) DYN_LIST_VALS(mindist);
  maxx_vals = (float *) DYN_LIST_VALS(maxx);
  maxy_vals = (float *) DYN_LIST_VALS(maxy);

  // initialize return list
  retlist = dfuCreateDynList(DF_LIST, length);

  for (i = 0; i < length; i++) {
    // get next set of variables
    this_ndist = ndist_vals[i];
    this_mindist = mindist_vals[i];
    this_maxx = maxx_vals[i];
    this_maxy = maxy_vals[i];
    curlist = (DYN_LIST *) sublists[i];
    subsublists = (DYN_LIST **) DYN_LIST_VALS(curlist);

    this_nold = DYN_LIST_N(curlist);
    xs = calloc(this_nold, sizeof(float));
    ys = calloc(this_nold, sizeof(float));

    for (j = 0; j < this_nold; j++) {
      cursublist = (DYN_LIST *) subsublists[j];
      vals = (float *) DYN_LIST_VALS(cursublist);
      // add elements to xs and ys
      xs[j] = vals[0];
      ys[j] = vals[1];
    }   

    // call pickpoints for this set of variables
    curlist = pickpoints(this_ndist, this_nold, xs, ys, this_mindist, this_maxx, this_maxy);
    dfuAddDynListList(retlist, curlist);
    
    // free memory
    free(xs);
    free(ys);

  }

  if (retlist)
    return(tclPutList(interp, retlist));
  else {
    Tcl_AppendResult(interp, argv[0], ": error creating point list", NULL);
    return TCL_ERROR;
  }
}

/*****************************************************************************
 *
 * FUNCTION
 *    pickpoints_ecc
 *
 * ARGS
 *    number of desired points
 *    current points as xs and ys array
 *    minimum distance between points
 *    maximum x
 *    maximum y
 *    ecc of desired points from anchor point
 *    anchor point in a single array (x = 0, y = 1)
 *
 * RETURNS
 *    dynamic list of n (x,y) pairs of new points
 *
 * DESCRIPTION
 *    Choose n new points (x,y) pairs at least mindist from each other
 *    and within maxx and maxy.    
 *
 *****************************************************************************/
static DYN_LIST *pickpoints_ecc (int n, int old, float xs[], float ys[], 
				 float mindist, float maxx, float maxy, 
				 float ecc, float anchor[])
{
  int tries, i, k, failed;
  float ang, this_x, this_y, xdiff, ydiff, dist2, mindist2;
  float *new_xs, *new_ys;

  DYN_LIST *new_points, *p;


  new_xs = calloc(n, sizeof(float));
  new_ys = calloc(n, sizeof(float));

  // if specifying 0.0 eccentricity, then just return the anchor point
  if (ecc == 0.) {
    new_points = dfuCreateDynList(DF_LIST, n);
    p = dfuCreateDynList(DF_FLOAT, 2);
    for (i = 0; i < n; i++) {
      dfuResetDynList(p);
      dfuAddDynListFloat(p, anchor[0]);
      dfuAddDynListFloat(p, anchor[1]);
      dfuAddDynListList(new_points, p);
    }
    free(new_xs);
    free(new_ys);
    
    return new_points;
  }
  
  mindist2 = mindist * mindist;
  
  for (i = 0; i < n; i++) {
    tries = -1;

    while (++tries < 100000) {
      failed = 0;

      ang = 2 * PI * frand() - 0.5;
      this_x = (cos(ang) * ecc) + anchor[0];
      this_y = (sin(ang) * ecc) + anchor[1];
      
      // check to make sure it is within maxx and maxy
      if ( abs(this_x) > maxx || abs(this_y) > maxy ) {
	continue;
      }

      for (k = 0; k < old; k++) {
	xdiff = this_x - xs[k];
	ydiff = this_y - ys[k];
	dist2 = xdiff*xdiff + ydiff*ydiff;
	if (dist2 < mindist2) {
	  failed = 1;
	  break;
	}
      }
      if (failed) continue;
      for (k = 0; k < i; k++) {
	xdiff = this_x - new_xs[k];
	ydiff = this_y - new_ys[k];
	dist2 = xdiff*xdiff + ydiff*ydiff;
	if (dist2 < mindist2) {
	  failed = 1;
	  break;
	}
      }
      if (failed) continue;

      //* if we get this far then we have a valid new point
      new_xs[i] = this_x;
      new_ys[i] = this_y;
      break;
    }
    /*
      if (tries  == 100000) {
      // error, couldn't choose a point in 100000 tries
      return NULL;
      }
    */
  }

  //* now we have to pack up new_xs and new_ys into a DYN_LIST called new_points
  new_points = dfuCreateDynList(DF_LIST, n);
  p = dfuCreateDynList(DF_FLOAT, 2);
  for (i = 0; i < n; i++) {
    dfuResetDynList(p);
    dfuAddDynListFloat(p, new_xs[i]);
    dfuAddDynListFloat(p, new_ys[i]);
    dfuAddDynListList(new_points, p);
  }

  free(new_xs);
  free(new_ys);

  return new_points;
}


/*****************************************************************************
 *
 * Function
 *    pickpointsEccCmd
 *
 * ARGS
 *    Tcl Args
 *
 * DLSH FUNCTION
 *    points::pickpointsEccList
 *
 * DESCRIPTION
 *    interface between TCL and pickpoints
 *
 *****************************************************************************/
static int pickpointsEccCmd (ClientData data, Tcl_Interp * interp,
				 int argc, char *argv[])
{
  int i, j, length;
  int this_nold, this_ndist;
  int *ndist_vals;
  float this_mindist, this_maxx, this_maxy, this_ecc;
  float *xs, *ys, *mindist_vals, *maxx_vals, *maxy_vals, *ecc_vals, *vals;
  DYN_LIST *ndist, *points, *mindist, *maxx, *maxy, *ecc, *anchor, *this_anchor;
  DYN_LIST *curlist, *cursublist, *curanchor, *retlist;
  DYN_LIST **sublists, **subsublists, **anchorsub;
  
  if (argc < 8) {
    Tcl_AppendResult(interp, "usage: ", argv[0], 
		     " n points mindist maxx maxy ecc anchor", NULL);
    return TCL_ERROR;
  }
  
  if (tclFindDynList(interp, argv[1], &ndist) != TCL_OK) return TCL_ERROR;
  if (DYN_LIST_DATATYPE(ndist) != DF_LONG) {
    Tcl_AppendResult(interp, argv[0], ": ndist must be of type integer", NULL);
    return TCL_ERROR;
  }
  length = DYN_LIST_N(ndist);

  if (tclFindDynList(interp, argv[2], &points) != TCL_OK) return TCL_ERROR;
  if (DYN_LIST_DATATYPE(points) != DF_LIST) {
    Tcl_AppendResult(interp, argv[0], ": invalid points data", NULL);
    return TCL_ERROR;
  }
  if (DYN_LIST_N(points) != length) {
    Tcl_AppendResult(interp, argv[0], ": points must be length of ndist", NULL);
    return TCL_ERROR;
  }
  // get sublist and check type, will have to pack points if sublist is FLOAT list
  sublists = (DYN_LIST **) DYN_LIST_VALS(points);
  for (i = 0; i < DYN_LIST_N(points); i++) {
    curlist = (DYN_LIST *) sublists[i];
    if (DYN_LIST_DATATYPE(curlist) != DF_LIST) {
      Tcl_AppendResult(interp, argv[0], ": invalid points data (might try packing it)", NULL);
      return TCL_ERROR;
    }
    
    subsublists = (DYN_LIST **) DYN_LIST_VALS(curlist);
    for (j = 0; j < DYN_LIST_N(curlist); j++) {
      cursublist = (DYN_LIST *) subsublists[j];
      if (DYN_LIST_N(cursublist) != 2 || 
	  DYN_LIST_DATATYPE(cursublist) != DF_FLOAT) {
	Tcl_AppendResult(interp, argv[0], ": invalid points list", NULL);
	return TCL_ERROR;
      }      
    }
  }

  if (tclFindDynList(interp, argv[3], &mindist) != TCL_OK) return TCL_ERROR;
  if (DYN_LIST_DATATYPE(mindist) != DF_FLOAT) {
    Tcl_AppendResult(interp, argv[0], ": mindist must be or type float", NULL);
    return TCL_ERROR;
  }
  if (DYN_LIST_N(mindist) != length) {
    Tcl_AppendResult(interp, argv[0], ": mindist must be length of ndist", NULL);
    return TCL_ERROR;
  }

  if (tclFindDynList(interp, argv[4], &maxx) != TCL_OK) return TCL_ERROR;
  if (DYN_LIST_DATATYPE(maxx) != DF_FLOAT) {
    Tcl_AppendResult(interp, argv[0], ": maxx must be or type float", NULL);
    return TCL_ERROR;
  }
  if (DYN_LIST_N(maxx) != length) {
    Tcl_AppendResult(interp, argv[0], ": maxx must be length of ndist", NULL);
    return TCL_ERROR;
  }

  if (tclFindDynList(interp, argv[5], &maxy) != TCL_OK) return TCL_ERROR;
  if (DYN_LIST_DATATYPE(maxy) != DF_FLOAT) {
    Tcl_AppendResult(interp, argv[0], ": maxy must be or type float", NULL);
    return TCL_ERROR;
  }
  if (DYN_LIST_N(maxy) != length) {
    Tcl_AppendResult(interp, argv[0], ": maxy must be length of ndist", NULL);
    return TCL_ERROR;
  }

  if (tclFindDynList(interp, argv[6], &ecc) != TCL_OK) return TCL_ERROR;
  if (DYN_LIST_DATATYPE(ecc) != DF_FLOAT) {
    Tcl_AppendResult(interp, argv[0], ": ecc must be or type float", NULL);
    return TCL_ERROR;
  }
  if (DYN_LIST_N(ecc) != length) {
    Tcl_AppendResult(interp, argv[0], ": ecc must be length of ndist", NULL);
    return TCL_ERROR;
  }

  if (tclFindDynList(interp, argv[7], &anchor) != TCL_OK) return TCL_ERROR;
  if (DYN_LIST_DATATYPE(anchor) != DF_LIST) {
    Tcl_AppendResult(interp, argv[0], ": invalid anchor list", NULL);
    return TCL_ERROR;
  }
  if (DYN_LIST_N(anchor) != length) {
    Tcl_AppendResult(interp, argv[0], ": anchor must be length of ndist", NULL);
    return TCL_ERROR;
  }
  // get sublist and check type, will have to pack points if sublist is FLOAT list
  anchorsub = (DYN_LIST **) DYN_LIST_VALS(anchor);
  for (i = 0; i < DYN_LIST_N(anchor); i++) {
    curlist = (DYN_LIST *) anchorsub[i];
    if (DYN_LIST_DATATYPE(curlist) != DF_FLOAT
	|| DYN_LIST_N(curlist) !=2) {
      Tcl_AppendResult(interp, argv[0], ": invalid anchor data", NULL);
      return TCL_ERROR;
    }
  }

  //change all dynlists into arrays
  ndist_vals = (int *) DYN_LIST_VALS(ndist);
  mindist_vals = (float *) DYN_LIST_VALS(mindist);
  maxx_vals = (float *) DYN_LIST_VALS(maxx);
  maxy_vals = (float *) DYN_LIST_VALS(maxy);
  ecc_vals = (float *) DYN_LIST_VALS(ecc);

  // initialize return list
  retlist = dfuCreateDynList(DF_LIST, length);

  for (i = 0; i < length; i++) {
    // get next set of variables
    this_ndist = ndist_vals[i];
    this_mindist = mindist_vals[i];
    this_maxx = maxx_vals[i];
    this_maxy = maxy_vals[i];
    this_ecc = ecc_vals[i];
    
    curanchor = (DYN_LIST *) anchorsub[i];
    this_anchor = (float *) DYN_LIST_VALS(curanchor);

    curlist = (DYN_LIST *) sublists[i];
    subsublists = (DYN_LIST **) DYN_LIST_VALS(curlist);

    this_nold = DYN_LIST_N(curlist);
    xs = calloc(this_nold, sizeof(float));
    ys = calloc(this_nold, sizeof(float));

    for (j = 0; j < this_nold; j++) {
      cursublist = (DYN_LIST *) subsublists[j];
      vals = (float *) DYN_LIST_VALS(cursublist);
      // add elements to xs and ys
      xs[j] = vals[0];
      ys[j] = vals[1];
    }   

    // call pickpoints for this set of variables
    curlist = pickpoints_ecc(this_ndist, this_nold, xs, ys, 
			     this_mindist, this_maxx, this_maxy, this_ecc, this_anchor);
    dfuAddDynListList(retlist, curlist);

    // free memory
    free(xs);
    free(ys);
  }

  if (retlist)
    return(tclPutList(interp, retlist));
  else {
    Tcl_AppendResult(interp, argv[0], ": error creating point list", NULL);
    return TCL_ERROR;
  }
}



/*****************************************************************************
 *
 * FUNCTION
 *    pickpoints_away_from
 *
 * ARGS
 *    number of desired points
 *    current points as xs and ys array
 *    minimum distance between points
 *    maximum radius 
 *    anchor
 *    ecc 
 *
 * RETURNS
 *    dynamic list of n (x,y) pairs of new points
 *
 * DESCRIPTION
 *    Choose n new points (x,y) pairs at least mindist from each other
 *    and within maxx and maxy, and away from the anchor   
 *
 *****************************************************************************/
static DYN_LIST *pickpoints_away_from (int n, int old, float xs[], float ys[], 
				       float mindist, float maxx, float maxy,
				       float ecc, float anchor[])
{
  int tries, i, k, failed;
  float this_x, this_y, xdiff, ydiff, dist2, mindist2;
  float *new_xs, *new_ys;
  
  float ecc2 = ecc*ecc;

  DYN_LIST *new_points, *p;

  new_xs = calloc(n, sizeof(float));
  new_ys = calloc(n, sizeof(float));

  mindist2 = mindist * mindist;
      
  for (i = 0; i < n; i++) {
    tries = -1;

    while (++tries < 100000) {
      failed = 0;

      this_x = maxx * 2 * (frand() - 0.5);
      this_y = maxy * 2 * (frand() - 0.5);
      
      /* Make sure the point is away from the anchor */
      xdiff = this_x - anchor[0];
      ydiff = this_y - anchor[1];
      if (xdiff*xdiff+ydiff*ydiff < ecc2) continue;

      for (k = 0; k < old; k++) {
	xdiff = this_x - xs[k];
	ydiff = this_y - ys[k];
	dist2 = xdiff*xdiff + ydiff*ydiff;
	if (dist2 < mindist2) {
	  failed = 1;
	  break;
	}
      }
      if (failed) continue;
      for (k = 0; k < i; k++) {
	xdiff = this_x - new_xs[k];
	ydiff = this_y - new_ys[k];
	dist2 = xdiff*xdiff + ydiff*ydiff;
	if (dist2 < mindist2) {
	  failed = 1;
	  break;
	}
      }
      if (failed) continue;

      //* if we get this far then we have a valid new point
      new_xs[i] = this_x;
      new_ys[i] = this_y;
      break;
    }
    /*
      if (tries  == 100000) {
      // error, couldn't choose a point in 100000 tries
      return NULL;
      }
    */
  }

  //* now we have to pack up new_xs and new_ys into a DYN_LIST called new_points
  new_points = dfuCreateDynList(DF_LIST, n);
  p = dfuCreateDynList(DF_FLOAT, 2);
  for (i = 0; i < n; i++) {
    dfuResetDynList(p);
    dfuAddDynListFloat(p, new_xs[i]);
    dfuAddDynListFloat(p, new_ys[i]);
    dfuAddDynListList(new_points, p);
  }

  free(new_xs);
  free(new_ys);

  return new_points;
}



/*****************************************************************************
 *
 * Function
 *    pickpointsEccCmd
 *
 * ARGS
 *    Tcl Args
 *
 * DLSH FUNCTION
 *    points::pickpointsAwayFrom
 *
 * DESCRIPTION
 *    interface between TCL and pickpoints
 *
 *****************************************************************************/
static int pickpointsAwayFromCmd (ClientData data, Tcl_Interp * interp,
				  int argc, char *argv[])
{
  int i, j, length;
  int this_nold, this_ndist;
  int *ndist_vals;
  float this_mindist, this_maxx, this_maxy, this_ecc;
  float *xs, *ys, *mindist_vals, *maxx_vals, *maxy_vals, *ecc_vals, *vals;
  DYN_LIST *ndist, *points, *mindist, *maxx, *maxy, *ecc, *anchor, *this_anchor;
  DYN_LIST *curlist, *cursublist, *curanchor, *retlist;
  DYN_LIST **sublists, **subsublists, **anchorsub;
  
  if (argc < 8) {
    Tcl_AppendResult(interp, "usage: ", argv[0], 
		     " n points mindist maxx maxy ecc anchor", NULL);
    return TCL_ERROR;
  }
  
  if (tclFindDynList(interp, argv[1], &ndist) != TCL_OK) return TCL_ERROR;
  if (DYN_LIST_DATATYPE(ndist) != DF_LONG) {
    Tcl_AppendResult(interp, argv[0], ": ndist must be of type integer", NULL);
    return TCL_ERROR;
  }
  length = DYN_LIST_N(ndist);

  if (tclFindDynList(interp, argv[2], &points) != TCL_OK) return TCL_ERROR;
  if (DYN_LIST_DATATYPE(points) != DF_LIST) {
    Tcl_AppendResult(interp, argv[0], ": invalid points data", NULL);
    return TCL_ERROR;
  }
  if (DYN_LIST_N(points) != length) {
    Tcl_AppendResult(interp, argv[0], ": points must be length of ndist", NULL);
    return TCL_ERROR;
  }
  // get sublist and check type, will have to pack points if sublist is FLOAT list
  sublists = (DYN_LIST **) DYN_LIST_VALS(points);
  for (i = 0; i < DYN_LIST_N(points); i++) {
    curlist = (DYN_LIST *) sublists[i];
    if (DYN_LIST_DATATYPE(curlist) != DF_LIST) {
      Tcl_AppendResult(interp, argv[0], ": invalid points data (might try packing it)", NULL);
      return TCL_ERROR;
    }
    
    subsublists = (DYN_LIST **) DYN_LIST_VALS(curlist);
    for (j = 0; j < DYN_LIST_N(curlist); j++) {
      cursublist = (DYN_LIST *) subsublists[j];
      if (DYN_LIST_N(cursublist) != 2 || 
	  DYN_LIST_DATATYPE(cursublist) != DF_FLOAT) {
	Tcl_AppendResult(interp, argv[0], ": invalid points list", NULL);
	return TCL_ERROR;
      }      
    }
  }

  if (tclFindDynList(interp, argv[3], &mindist) != TCL_OK) return TCL_ERROR;
  if (DYN_LIST_DATATYPE(mindist) != DF_FLOAT) {
    Tcl_AppendResult(interp, argv[0], ": mindist must be or type float", NULL);
    return TCL_ERROR;
  }
  if (DYN_LIST_N(mindist) != length) {
    Tcl_AppendResult(interp, argv[0], ": mindist must be length of ndist", NULL);
    return TCL_ERROR;
  }

  if (tclFindDynList(interp, argv[4], &maxx) != TCL_OK) return TCL_ERROR;
  if (DYN_LIST_DATATYPE(maxx) != DF_FLOAT) {
    Tcl_AppendResult(interp, argv[0], ": maxx must be or type float", NULL);
    return TCL_ERROR;
  }
  if (DYN_LIST_N(maxx) != length) {
    Tcl_AppendResult(interp, argv[0], ": maxx must be length of ndist", NULL);
    return TCL_ERROR;
  }

  if (tclFindDynList(interp, argv[5], &maxy) != TCL_OK) return TCL_ERROR;
  if (DYN_LIST_DATATYPE(maxy) != DF_FLOAT) {
    Tcl_AppendResult(interp, argv[0], ": maxy must be or type float", NULL);
    return TCL_ERROR;
  }
  if (DYN_LIST_N(maxy) != length) {
    Tcl_AppendResult(interp, argv[0], ": maxy must be length of ndist", NULL);
    return TCL_ERROR;
  }

  if (tclFindDynList(interp, argv[6], &ecc) != TCL_OK) return TCL_ERROR;
  if (DYN_LIST_DATATYPE(ecc) != DF_FLOAT) {
    Tcl_AppendResult(interp, argv[0], ": ecc must be or type float", NULL);
    return TCL_ERROR;
  }
  if (DYN_LIST_N(ecc) != length) {
    Tcl_AppendResult(interp, argv[0], ": ecc must be length of ndist", NULL);
    return TCL_ERROR;
  }

  if (tclFindDynList(interp, argv[7], &anchor) != TCL_OK) return TCL_ERROR;
  if (DYN_LIST_DATATYPE(anchor) != DF_LIST) {
    Tcl_AppendResult(interp, argv[0], ": invalid anchor list", NULL);
    return TCL_ERROR;
  }
  if (DYN_LIST_N(anchor) != length) {
    Tcl_AppendResult(interp, argv[0], ": anchor must be length of ndist", NULL);
    return TCL_ERROR;
  }
  // get sublist and check type, will have to pack points if sublist is FLOAT list
  anchorsub = (DYN_LIST **) DYN_LIST_VALS(anchor);
  for (i = 0; i < DYN_LIST_N(anchor); i++) {
    curlist = (DYN_LIST *) anchorsub[i];
    if (DYN_LIST_DATATYPE(curlist) != DF_FLOAT
	|| DYN_LIST_N(curlist) !=2) {
      Tcl_AppendResult(interp, argv[0], ": invalid anchor data", NULL);
      return TCL_ERROR;
    }
  }

  //change all dynlists into arrays
  ndist_vals = (int *) DYN_LIST_VALS(ndist);
  mindist_vals = (float *) DYN_LIST_VALS(mindist);
  maxx_vals = (float *) DYN_LIST_VALS(maxx);
  maxy_vals = (float *) DYN_LIST_VALS(maxy);
  ecc_vals = (float *) DYN_LIST_VALS(ecc);

  // initialize return list
  retlist = dfuCreateDynList(DF_LIST, length);

  for (i = 0; i < length; i++) {
    // get next set of variables
    this_ndist = ndist_vals[i];
    this_mindist = mindist_vals[i];
    this_maxx = maxx_vals[i];
    this_maxy = maxy_vals[i];
    this_ecc = ecc_vals[i];
    
    curanchor = (DYN_LIST *) anchorsub[i];
    this_anchor = (float *) DYN_LIST_VALS(curanchor);

    curlist = (DYN_LIST *) sublists[i];
    subsublists = (DYN_LIST **) DYN_LIST_VALS(curlist);

    this_nold = DYN_LIST_N(curlist);
    xs = calloc(this_nold, sizeof(float));
    ys = calloc(this_nold, sizeof(float));

    for (j = 0; j < this_nold; j++) {
      cursublist = (DYN_LIST *) subsublists[j];
      vals = (float *) DYN_LIST_VALS(cursublist);
      // add elements to xs and ys
      xs[j] = vals[0];
      ys[j] = vals[1];
    }   

    // call pickpoints for this set of variables
    curlist = pickpoints_away_from(this_ndist, this_nold, xs, ys, 
				   this_mindist, this_maxx, this_maxy, this_ecc, this_anchor);
    dfuAddDynListList(retlist, curlist);

    // free memory
    free(xs);
    free(ys);
  }

  if (retlist)
    return(tclPutList(interp, retlist));
  else {
    Tcl_AppendResult(interp, argv[0], ": error creating point list", NULL);
    return TCL_ERROR;
  }
}


#ifdef WIN32
BOOL APIENTRY
DllEntryPoint(hInst, reason, reserved)
    HINSTANCE hInst;
    DWORD reason;
    LPVOID reserved;
{
  return TRUE;
}
#endif
