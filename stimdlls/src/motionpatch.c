/*
 * motionpatch.c
 *  Module to show a flowfield of moving dots, with optional shape
 *  masking and world-map modulation via two texture samplers.
 *
 * ============================================================
 * OVERVIEW
 * ============================================================
 *
 * A motionpatch is a fixed-count cloud of dots living in patch-local
 * coordinates [-0.5, 0.5] x [-0.5, 0.5]. Per frame, each dot is
 * advanced along its direction at the current speed; when its
 * lifetime expires (or it leaves the patch), it respawns at a random
 * position with a freshly sampled direction. A configurable fraction
 * (`coherence`) of dots stream along a shared global direction; the
 * rest get random directions, producing a flicker noise field.
 *
 * Two RGBA texture samplers can be attached:
 *
 *   tex0 ("primary mask"):
 *     Sampled at a transformed dot texcoord:
 *         tc = (texcoord - 0.5 - maskOffset) / maskScale
 *         rotated by maskRotation about (0.5, 0.5)
 *     `samplermaskmode` selects how its alpha gates dot rendering:
 *         0 (SMASK_NONE)            : ignore tex0; uColor1 only.
 *         1 (SMASK_ALPHA)           : alpha *= texAlpha (visible
 *                                     INSIDE the mask shape).
 *         2 (SMASK_ONE_MINUS_ALPHA) : alpha *= 1 - texAlpha (visible
 *                                     OUTSIDE the mask shape).
 *         3 (SMASK_TWO_COLOR)       : uColor1 INSIDE, uColor2 OUTSIDE.
 *     `masksoftness` smoothsteps the alpha edge for anti-aliasing.
 *
 *   tex1 ("world map", multi-layer):
 *     Sampled at the dot's RAW patch-local texcoord (no offset/scale/
 *     rotation), so the world map stays fixed in the patch frame even
 *     when tex0 translates. Each RGBA channel of tex1 is an INDEPENDENT
 *     alpha mask treated as one semantic layer; the four layers
 *     compose at fragment time, each with its own mode/color/dim:
 *         layerModes[i]:  0=off, 1=dim, 2=tint, 3=hide
 *         layerDims[i]:   alpha multiplier for mode 1
 *         layerColors[i]: RGBA tint for mode 2
 *     Channels compose in order R, G, B, A so layer 3 (alpha) draws
 *     last and overwrites earlier layers in tint mode. Typical
 *     assignment for a planko-style task:
 *         A: planks      (dim mode 1)
 *         R: left zone   (tint mode 2, e.g. green)
 *         G: right zone  (tint mode 2, e.g. red)
 *         B: spare       (off; reserved for walls/paddles/etc)
 *     The two samplers compose: tex0 picks WHICH dots are visible
 *     (inside vs. outside a translating shape), tex1 modulates them
 *     based on a fixed world-frame multi-layer map. Each motionpatch
 *     can opt in/out of individual layers independently, so a "ball"
 *     patch and a "surround" patch sharing the same tex1 can render
 *     the target on top (target layerModes=0,0,0,0) or have it be
 *     occluded by selected layers (target layerModes[3]=3 only).
 *
 *     Backward-compat: motionpatch_worldmaskmode/dim/color operate on
 *     the alpha channel (layer 3) for code written before the multi-
 *     layer extension.
 *
 * ============================================================
 * UNITS & COORDINATE CONVENTIONS
 * ============================================================
 *
 *   * Patch-local space: the dot positions live in [-0.5, 0.5]^2.
 *     A metagroup typically scales the patch so 1.0 patch-unit
 *     spans a fixed dva (degrees visual angle) extent on screen.
 *
 *   * `motionpatch_speed`:  patch-local-units PER SECOND. The C-side
 *     integration loop multiplies by real `dt` from getStimTime(),
 *     so dot velocity is frame-rate-independent (60 Hz, 120 Hz,
 *     variable-rate displays all produce the same screen-coord
 *     motion). To convert from a desired physical speed in dva/sec:
 *         speed = v_dva_per_sec / patch_size_dva
 *     (no refresh-rate term -- the conversion just maps from dva
 *     to patch-local units, since dot positions span [-0.5, 0.5]
 *     while the metagroup scales by patch_size_dva.)
 *
 *   * `motionpatch_direction`: radians, 0 = +x, pi/2 = +y. Affects
 *     only `coherent` dots; incoherent dots get random directions.
 *
 *   * `motionpatch_directionjitter`: per-dot Gaussian std-dev
 *     (radians) added to each coherent dot's direction at respawn.
 *
 *   * `motionpatch_lifetime`: SECONDS between respawns (float).
 *     A value <= 0 means "no respawn" (dots live forever). Short
 *     lifetimes (< ~80 ms) approach pure flicker; long lifetimes
 *     (> ~500 ms) produce coherent streams. Real-time, so display
 *     rate doesn't change the perceptual lifetime.
 *
 *   * `motionpatch_maskoffset`: patch-local centered coordinates.
 *     |offset| < 0.5 keeps the mask shape on-patch.
 *
 *   * `motionpatch_maskscale`: 1.0 means the mask texture fills the
 *     patch (mask diameter == patch span). 0.2 -> mask spans 20%.
 *
 *   * `motionpatch_pointsize`: GL point size in pixels.
 *
 * ============================================================
 * COMMAND REFERENCE
 * ============================================================
 *
 *   Lifecycle / construction:
 *     motionpatch n speed lifetime    -- construct; returns objid
 *     motionpatch_refreshPositions    -- resample all dot positions
 *
 *   Appearance:
 *     motionpatch_color r g b a       -- uColor1 (primary)
 *     motionpatch_color2 r g b a      -- uColor2 (samplermaskmode 3)
 *     motionpatch_pointsize px        -- GL point size
 *
 *   Motion:
 *     motionpatch_speed s             -- patch-units / frame
 *     motionpatch_direction rad       -- coherent direction
 *     motionpatch_directionjitter rad -- per-dot direction noise
 *     motionpatch_coherence frac      -- 0..1
 *     motionpatch_lifetime frames     -- respawn period
 *
 *   Direction-by-noise (open-simplex flow field):
 *     motionpatch_useNoiseDirection use period [rate]
 *     motionpatch_setSeed seed
 *     motionpatch_setNoiseZ z
 *     motionpatch_noiseUpdateZ flag
 *
 *   Mask shape (sampler 0):
 *     motionpatch_setSampler mp tex 0 -- bind primary mask texture
 *     motionpatch_masktype mt         -- legacy MASK_TYPE enum
 *     motionpatch_maskradius r        -- legacy circle/hexagon radius
 *     motionpatch_samplermaskmode m   -- 0/1/2/3 (see overview)
 *     motionpatch_maskoffset x y      -- centered patch-local
 *     motionpatch_maskscale sx sy     -- 1.0 = fills patch
 *     motionpatch_maskrotation rad
 *     motionpatch_masksoftness s      -- 0 = hard, >0 = soft edge
 *
 *   World map (sampler 1, 4 layers):
 *     motionpatch_setSampler mp tex 1     -- bind world-map texture
 *     motionpatch_layermode  mp ch m      -- 0/1/2/3 per channel
 *     motionpatch_layerdim   mp ch d      -- mode-1 multiplier per ch
 *     motionpatch_layercolor mp ch r g b a -- mode-2 tint per ch
 *       (ch is 0..3 or one of R/G/B/A)
 *
 *   Backward-compat (target alpha channel, layer 3):
 *     motionpatch_worldmaskmode m
 *     motionpatch_worlddim factor
 *     motionpatch_worldcolor r g b a
 *
 * ============================================================
 * COMPOSITION ORDER (fragment shader)
 * ============================================================
 *
 *   1. Transform texcoord by maskOffset/maskScale/maskRotation.
 *   2. Sample tex0 at the transformed coord -> texAlpha, texColor.
 *   3. samplermaskmode chooses (color, alpha) from texAlpha and
 *      uColor1/uColor2.
 *   4. Sample tex1 at the RAW texcoord -> wsamp (RGBA alpha masks,
 *      one per layer).
 *   5. For each channel R/G/B/A in turn, applyLayer modulates
 *      (color, alpha) by wsamp[channel] using layerModes[channel].
 *   6. Output (color, alpha) for the dot.
 *
 *   Implication: tex1 is "in the world frame"; tex0 is "in the
 *   shape frame" (translates with maskOffset). This is what makes
 *   trajectory-aperture demos work -- the ball's mask follows the
 *   trajectory while the planks stay fixed in the patch.
 */


#ifdef WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#undef WIN32_LEAN_AND_MEAN
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

#include "open-simplex-noise.h"

#include <tcl.h>

#include <glad/glad.h>
#include <GLFW/glfw3.h> 

#include <prmutil.h>

/* If you want access to dlsh connectivity, include these */
#include "df.h"
#include "tcl_dl.h"

#include <stim2.h>		/* Stim header      */
#include "shaderutils.h"
#include "objname.h"

#if !defined(PI)
#define PI 3.1415926
#define TWO_PI 6.283185307
#endif

typedef struct _dot {
  float pos[3];
  float lifetime_s;		/* respawn period in SECONDS (negative = no respawn) */
  float elapsed_s;		/* seconds since last respawn */
  int coherent;			/* flag as either coherent or not */
  float theta;			/* per-dot motion angle (radians).
				 * Coherent dots: angular jitter offset
				 *   added to s->direction at integration.
				 * Incoherent dots: absolute angle (the
				 *   global direction is ignored).
				 * Avoids caching cos/sin per dot; vx,vy
				 * are computed on the fly in the update
				 * loop, so direction/speed changes need
				 * no per-dot recomputation pass. */
} DOT;

typedef struct _vao_info {
  GLuint vao;
  int narrays;
  int nindices;
  int npoints;
  GLfloat *points;
  GLuint points_vbo;
  int ntexcoords;
  GLfloat *texcoords;
  GLuint texcoords_vbo;
} VAO_INFO;

typedef enum MASK_TYPE { MASK_NONE, MASK_CIRCLE, MASK_HEXAGON, MASK_LAST }
  MASK_TYPE;

typedef enum SAMPLER_MASK_TYPE { SMASK_NONE, SMASK_ALPHA, SMASK_ONE_MINUS_ALPHA, SMASK_TWO_COLOR, SMASK_LAST }
  SAMPLER_MASK_TYPE;


#define MAX_NOISE_CTX 4
#define NSAMPLERS 2

typedef struct {
  DOT *dots;
  int num_dots;
  MASK_TYPE mask_type;		/* MASK_NONE, MASK_CIRCLE, MASK_HEXAGON */
  float mask_radius;
  float coherence;
  float color1[4];
  float color2[4];
  float pointsize;
  float speed;			/* patch-local-units PER SECOND */
  float direction;
  int   samplermaskmode;
  float lifetime_s;		/* dot respawn period in SECONDS */
  double last_stim_time_ms;	/* StimTime at last update, for dt-based
				 * frame-rate-independent integration. -1
				 * sentinel = first frame (use nominal dt). */
  VAO_INFO *vao_info;		/* to track vertex attributes */
  int64_t noise_seed[MAX_NOISE_CTX];	/* for noise generation       */
  struct osn_context *ctx[MAX_NOISE_CTX];   /* context for noise funcs    */
  float noise_period;           /* for x and y                */
  float noise_z;		/* current z                  */
  int   noise_update_z;		/* update based on StimTime   */
  float noise_update_rate;	/* for the z variable         */
  int set_direction_by_noise;

  GLuint       texid[NSAMPLERS]; /* To use as a mask for the dots */
  UNIFORM_INFO *tex0;		 /* primary mask sampler               */
  UNIFORM_INFO *tex1;		 /* world-map sampler (4 layers, RGBA) */
  /* Per-layer state for the world-map sampler. Each RGBA channel of
     tex1 is an independent alpha mask for one semantic region (e.g.
     planks=A, left zone=R, right zone=G, spare=B). All four layers
     compose at fragment time; each has its own mode/color/dim. */
  int   layer_mode[4];           /* 0:off 1:dim 2:tint 3:hide          */
  float layer_dim[4];             /* multiplier when mode == 1          */
  float layer_color[4][4];       /* RGBA tint when mode == 2           */
  UNIFORM_INFO *layerModes;      /* ivec4                              */
  UNIFORM_INFO *layerDims;       /* vec4                               */
  UNIFORM_INFO *layerColors;     /* mat4 (one column per layer)        */

  UNIFORM_INFO *modelviewMat;   /* set if we have "modelviewMat" uniform */
  UNIFORM_INFO *projMat;        /* set if we have "projMat" uniform */
  UNIFORM_INFO *uColor1;        /* set if we have "uColor" uniform */
  UNIFORM_INFO *uColor2;        /* set if we have "uColor" uniform */
  UNIFORM_INFO *pointSize;
  UNIFORM_INFO *samplerMaskMode;/* 0: ignore, 1: use alpha, 2: use 1-alpha */
  UNIFORM_INFO *maskRotation;   /* rotation angle for mask texture (radians) */
  float mask_rotation;          /* current mask rotation value */
  UNIFORM_INFO *maskOffset;     /* translation of mask (patch-local, centered) */
  float mask_offset[2];         /* current mask offset */
  UNIFORM_INFO *maskScale;      /* scale of mask (1.0 = mask fills patch) */
  float mask_scale[2];          /* current mask scale */
  UNIFORM_INFO *maskSoftness;   /* 0 = hard edge; >0 = smoothstep width */
  float mask_softness;          /* current softness value */
  float direction_jitter;       /* per-dot angular std-dev, radians */
  
  SHADER_PROG *program;
  Tcl_HashTable uniformTable;	/* local unique version */
  Tcl_HashTable attribTable;	/* local unique version */

} MOTIONPATCH;

static int MotionpatchID = -1;	/* unique object id */
SHADER_PROG *MotionpatchShaderProg = NULL;
static GLuint MotionpatchDefaultTex = 0; /* 1x1 white, bound when no user tex */

static float mp_randn(void);

void motionpatchDraw(GR_OBJ *g) 
{
  MOTIONPATCH *s = (MOTIONPATCH *) GR_CLIENTDATA(g);
  SHADER_PROG *sp = (SHADER_PROG *) s->program;
  float *v;

  /* Update uniform table */
  if (s->modelviewMat) {
    v = (float *) s->modelviewMat->val;
    stimGetMatrix(STIM_MODELVIEW_MATRIX, v);
  }
  if (s->projMat) {
    v = (float *) s->projMat->val;
    stimGetMatrix(STIM_PROJECTION_MATRIX, v);
  }
  if (s->uColor1) {
    v = (float *) s->uColor1->val;
    v[0] = s->color1[0];
    v[1] = s->color1[1];
    v[2] = s->color1[2];
    v[3] = s->color1[3];
  }
  if (s->uColor2) {
    v = (float *) s->uColor2->val;
    v[0] = s->color2[0];
    v[1] = s->color2[1];
    v[2] = s->color2[2];
    v[3] = s->color2[3];
  }

  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  if (s->pointSize) {
    glEnable(GL_PROGRAM_POINT_SIZE);
    memcpy(s->pointSize->val, &s->pointsize, sizeof(float));
  }

  if (s->samplerMaskMode) {
    /* so shader knows how to deal with the sampler mask */
    memcpy(s->samplerMaskMode->val, &s->samplermaskmode, sizeof(int));
  }

  if (s->maskRotation) {
    memcpy(s->maskRotation->val, &s->mask_rotation, sizeof(float));
  }

  if (s->maskOffset) {
    memcpy(s->maskOffset->val, s->mask_offset, sizeof(float)*2);
  }

  if (s->maskScale) {
    memcpy(s->maskScale->val, s->mask_scale, sizeof(float)*2);
  }

  if (s->maskSoftness) {
    memcpy(s->maskSoftness->val, &s->mask_softness, sizeof(float));
  }

  if (s->layerModes) {
    memcpy(s->layerModes->val, s->layer_mode, sizeof(int)*4);
  }
  if (s->layerDims) {
    memcpy(s->layerDims->val, s->layer_dim, sizeof(float)*4);
  }
  if (s->layerColors) {
    /* mat4 is column-major: each layer is one column (4 floats).
       layer_color is already laid out as 4 layers x 4 components. */
    memcpy(s->layerColors->val, s->layer_color, sizeof(float)*16);
  }

  /* Bind the texture to unit 0 BEFORE glUseProgram so the Apple GL
     driver's sampler-binding validation sees a valid texture. If no
     user sampler has been set, fall back to a 1x1 white default. */
  glActiveTexture(GL_TEXTURE0);
  if (s->texid[0] >= 0 && s->tex0) {
    switch(s->tex0->type) {
    case GL_SAMPLER_2D:
      glBindTexture(GL_TEXTURE_2D, s->texid[0]);
      break;
    case GL_SAMPLER_2D_ARRAY:
      glBindTexture(GL_TEXTURE_2D_ARRAY, s->texid[0]);
      break;
    }
  } else if (MotionpatchDefaultTex) {
    glBindTexture(GL_TEXTURE_2D, MotionpatchDefaultTex);
  }

  /* Same for unit 1 (the world-map sampler). Falls back to the same
     1x1 white default so the shader always has a valid binding. */
  glActiveTexture(GL_TEXTURE1);
  if (s->texid[1] >= 0 && s->tex1) {
    switch(s->tex1->type) {
    case GL_SAMPLER_2D:
      glBindTexture(GL_TEXTURE_2D, s->texid[1]);
      break;
    case GL_SAMPLER_2D_ARRAY:
      glBindTexture(GL_TEXTURE_2D_ARRAY, s->texid[1]);
      break;
    }
  } else if (MotionpatchDefaultTex) {
    glBindTexture(GL_TEXTURE_2D, MotionpatchDefaultTex);
  }
  glActiveTexture(GL_TEXTURE0);

  glUseProgram(sp->program);
  update_uniforms(&s->uniformTable);


  if (s->vao_info->narrays) {
    glBindVertexArray(s->vao_info->vao);
    glDrawArrays(GL_POINTS, 0, s->vao_info->nindices);
  }
  glUseProgram(0);
}

static void delete_vao_info(VAO_INFO *vinfo)
{
  if (vinfo->npoints) {
    glDeleteBuffers(1, &vinfo->points_vbo);
    free(vinfo->points);
  }
  if (vinfo->ntexcoords) {
    glDeleteBuffers(1, &vinfo->texcoords_vbo);
    free(vinfo->texcoords);
  }
  glDeleteVertexArrays(1, &vinfo->vao);
}


void motionpatchDelete(GR_OBJ *g) 
{
  int i;
  MOTIONPATCH *s = (MOTIONPATCH *) GR_CLIENTDATA(g);
  if (s->dots) free(s->dots);
  for (i = 0; i < MAX_NOISE_CTX; i++) {
    if (s->ctx[i]) open_simplex_noise_free(s->ctx[i]);
  }
  delete_vao_info(s->vao_info);
  free((void *) s);
}

int inHexagon(float in_x, float in_y)
{
  float x, y, l2, px, py;
  x = in_x*2;
  y = in_y*2;
  l2 = x * x + y * y;
  if (l2 > 1.0f) return 0;
  if (l2 < 0.75f) return 1;
  // Check against borders
  px = x * 1.15470053838f; // 2/sqrt(3)
  if (px > 1.0f || px < -1.0f) return 0;
  py = 0.5f * px + y;
  if (py > 1.0f || py < -1.0f) return 0;
  if (px - py > 1.0f || px - py < -1.0f) return 0;
  return 1;
}

void motionpatchUpdate(GR_OBJ *g) 
{
  MOTIONPATCH *s = (MOTIONPATCH *) GR_CLIENTDATA(g);
  int i, npoints = 0, ntexcoords = 0;
  float vx, vy;
  GLfloat *points = s->vao_info->points;
  GLfloat *texcoords = s->vao_info->texcoords;
  float r2;
  float value1, value2;
  
  
  if (s->mask_type == MASK_CIRCLE)
    r2 = s->mask_radius*s->mask_radius;

  /* Compute dt (real seconds) since last update for frame-rate-
   * independent integration. First frame falls back to ~16.67 ms
   * (one nominal 60 Hz frame); abnormally large gaps (>0.5 s,
   * e.g. paused stim) also reset to nominal so dots don't lurch
   * after a long pause. */
  double now_ms = getStimTime();
  float dt;
  if (s->last_stim_time_ms < 0.0) {
    dt = 1.0f / 60.0f;
  } else {
    dt = (float) ((now_ms - s->last_stim_time_ms) / 1000.0);
    if (dt <= 0.0f || dt > 0.5f) dt = 1.0f / 60.0f;
  }
  s->last_stim_time_ms = now_ms;

  if (s->noise_update_z)
    s->noise_z = (float) (now_ms / 1000.0 * s->noise_update_rate);

  for (i = 0; i < s->num_dots; i++) {
    if (s->dots[i].lifetime_s >= 0.0f &&
	s->dots[i].elapsed_s >= s->dots[i].lifetime_s) {
      /* RESPAWN: pick a new position, reset elapsed time, sample
       * a new per-dot angle. Coherent dots store a small jitter
       * offset; incoherent dots store an absolute random angle.
       * (vx,vy) is computed in the integration branch from theta
       * + s->direction + s->speed, so direction/speed changes
       * propagate without an O(N) pass. */
      s->dots[i].pos[0] = ((float) rand()/RAND_MAX) - 0.5f;
      s->dots[i].pos[1] = ((float) rand()/RAND_MAX) - 0.5f;
      s->dots[i].elapsed_s = 0.0f;

      if (s->set_direction_by_noise) {
	value1 = open_simplex_noise3(s->ctx[0],
				     s->dots[i].pos[0] * s->noise_period,
				     s->dots[i].pos[1] * s->noise_period,
				     s->noise_z);
	value2 = open_simplex_noise3(s->ctx[1],
				     s->dots[i].pos[0] * s->noise_period,
				     s->dots[i].pos[1] * s->noise_period,
				     s->noise_z);
	s->direction = atan2(value2, value1);
      }

      if (s->dots[i].coherent) {
	s->dots[i].theta =
	  (s->direction_jitter > 0.0f) ? mp_randn() * s->direction_jitter : 0.0f;
      }
      else {
	s->dots[i].theta = ((float) rand()/RAND_MAX) * 2.0f * (float) PI;
      }
    }
    else {
      /* ALIVE: optionally update global direction from the noise field;
       * for incoherent dots in noise mode, resample theta so each frame
       * picks a fresh random angle. Then compute (vx,vy) on the fly
       * from theta + global state and integrate. */
      if (s->set_direction_by_noise) {
	value1 = open_simplex_noise3(s->ctx[0],
				     s->dots[i].pos[0] * s->noise_period,
				     s->dots[i].pos[1] * s->noise_period,
				     s->noise_z);
	value2 = open_simplex_noise3(s->ctx[1],
				     s->dots[i].pos[0] * s->noise_period,
				     s->dots[i].pos[1] * s->noise_period,
				     s->noise_z);
	s->direction = atan2(value2, value1);
	if (!s->dots[i].coherent) {
	  s->dots[i].theta = ((float) rand()/RAND_MAX) * 2.0f * (float) PI;
	}
      }

      {
	float angle = s->dots[i].coherent
	  ? (s->direction + s->dots[i].theta)
	  : s->dots[i].theta;
	/* s->speed is in patch-local-units PER SECOND. Multiply by
	   real-time dt to get the per-frame increment, independent
	   of actual display refresh rate. GR_SX/GR_SY handle any
	   non-unity metagroup scaling. */
	vx = cosf(angle) * s->speed * dt / GR_SX(g);
	vy = sinf(angle) * s->speed * dt / GR_SY(g);
      }
      s->dots[i].pos[0] += vx;
      s->dots[i].pos[1] += vy;
      /* Toroidal wrap: keep dots inside [-0.5, 0.5] so the patch
         remains bounded. Without this, off-patch dots are still
         drawn at their drifted position, producing a visible halo
         of dots outside the patch — most obvious at low coherence
         when dots scatter in every direction. */
      if (s->dots[i].pos[0] < -0.5f) s->dots[i].pos[0] += 1.0f;
      else if (s->dots[i].pos[0] >  0.5f) s->dots[i].pos[0] -= 1.0f;
      if (s->dots[i].pos[1] < -0.5f) s->dots[i].pos[1] += 1.0f;
      else if (s->dots[i].pos[1] >  0.5f) s->dots[i].pos[1] -= 1.0f;
      s->dots[i].elapsed_s += dt;
    }
    if (s->mask_type == MASK_NONE) {
      *points++ = s->dots[i].pos[0];
      *points++ = s->dots[i].pos[1];
      *points++ = s->dots[i].pos[2];
      npoints+=3;

      *texcoords++ = s->dots[i].pos[0] + 0.5;
      *texcoords++ = s->dots[i].pos[1] + 0.5;
      ntexcoords+=2;
    }
    else if (s->mask_type == MASK_CIRCLE) {
      if ((s->dots[i].pos[0]*s->dots[i].pos[0]+
	   s->dots[i].pos[1]*s->dots[i].pos[1]) < r2) {
	*points++ = s->dots[i].pos[0];
	*points++ = s->dots[i].pos[1];
	*points++ = s->dots[i].pos[2];
	npoints+=3;

	*texcoords++ = s->dots[i].pos[0] + 0.5;
	*texcoords++ = s->dots[i].pos[1] + 0.5;
	ntexcoords+=2;
      }
    }
    else if (s->mask_type == MASK_HEXAGON) {
      if (inHexagon(s->dots[i].pos[0], s->dots[i].pos[1])) {
	*points++ = s->dots[i].pos[0];
	*points++ = s->dots[i].pos[1];
	*points++ = s->dots[i].pos[2];
	npoints+=3;
	
	*texcoords++ = s->dots[i].pos[0] + 0.5;
	*texcoords++ = s->dots[i].pos[1] + 0.5;
	ntexcoords+=2;
      }
    }
  }

  s->vao_info->nindices = npoints/3;
  glBindBuffer(GL_ARRAY_BUFFER, s->vao_info->points_vbo);
  glBufferData(GL_ARRAY_BUFFER, npoints*sizeof(GLfloat),
	       s->vao_info->points, GL_STATIC_DRAW);

  glBindBuffer(GL_ARRAY_BUFFER, s->vao_info->texcoords_vbo);
  glBufferData(GL_ARRAY_BUFFER, ntexcoords*sizeof(GLfloat),
	       s->vao_info->texcoords, GL_STATIC_DRAW);
}

static int setPositions(MOTIONPATCH *s)
{
  int i;
  for (i = 0; i < s->num_dots; i++) {
    s->dots[i].pos[0] = ((float) rand()/RAND_MAX)-0.5;
    s->dots[i].pos[1] = ((float) rand()/RAND_MAX)-0.5;
    s->dots[i].pos[2] = 0;
  }
  return TCL_OK;
}

/* Box-Muller standard normal */
static float mp_randn(void)
{
  float u1, u2;
  do {
    u1 = (float) rand() / (float) RAND_MAX;
  } while (u1 < 1e-9f);
  u2 = (float) rand() / (float) RAND_MAX;
  return sqrtf(-2.0f * logf(u1)) * cosf(2.0f * (float) PI * u2);
}

static int setLifetimes(MOTIONPATCH *s, float lifetime_s)
{
  int i;
  for (i = 0; i < s->num_dots; i++) {
    s->dots[i].lifetime_s = lifetime_s;
    /* Birth-time randomization: each dot gets a fresh elapsed value
     * uniformly within [0, lifetime_s) so respawns aren't synchronized
     * across the population. With lifetime_s <= 0 (no respawn) we
     * still set elapsed_s to 0. */
    if (lifetime_s > 0.0f) {
      s->dots[i].elapsed_s = ((float) rand() / (float) RAND_MAX) * lifetime_s;
    } else {
      s->dots[i].elapsed_s = 0.0f;
    }
  }
  return TCL_OK;
}

/* Reflag dots as coherent / incoherent so that the coherent-fraction
 * matches the requested ratio, with STABLE MEMBERSHIP across calls:
 * dots already in the correct group keep their flag and their theta
 * (no per-call reshuffle, no per-call jitter resample). Only the
 * minimum number of dots needed to reach the target ratio get
 * flipped, and only those flipped dots get a fresh theta.
 *
 * This makes per-frame coherence modulation (e.g. envelope-pulsing
 * for tiled-snapshot stimuli) clean: same coherence value -> zero
 * dot updates and zero perceptual change. Smoothly varying coherence
 * -> smoothly varying membership with no extra flicker on top.
 *
 * For applications that DO want a fresh random shuffle (e.g. trial
 * onset), call motionpatch_resampleCoherence which forces a full
 * re-roll regardless of current state.
 */
static int setCoherences(MOTIONPATCH *s, float coherence)
{
  int N = s->num_dots;
  int target = (int) (coherence * N + 0.5f);
  if (target < 0) target = 0;
  if (target > N) target = N;

  /* Count currently coherent dots. */
  int curr = 0;
  for (int i = 0; i < N; i++) {
    if (s->dots[i].coherent) curr++;
  }

  if (target > curr) {
    /* Need to flip (target - curr) incoherent dots to coherent.
     * Walk from a random start so we don't always pick the same
     * indices when ratios change in small steps. */
    int need = target - curr;
    int start = (N > 0) ? (rand() % N) : 0;
    for (int k = 0; k < N && need > 0; k++) {
      int i = (start + k) % N;
      if (!s->dots[i].coherent) {
	s->dots[i].coherent = 1;
	s->dots[i].theta =
	  (s->direction_jitter > 0.0f) ? mp_randn() * s->direction_jitter : 0.0f;
	need--;
      }
    }
  }
  else if (target < curr) {
    /* Need to flip (curr - target) coherent dots to incoherent. */
    int need = curr - target;
    int start = (N > 0) ? (rand() % N) : 0;
    for (int k = 0; k < N && need > 0; k++) {
      int i = (start + k) % N;
      if (s->dots[i].coherent) {
	s->dots[i].coherent = 0;
	s->dots[i].theta = ((float) rand()/RAND_MAX) * 2.0f * (float) PI;
	need--;
      }
    }
  }
  /* target == curr: no flips, no theta resamples. */

  return TCL_OK;
}

/* Force a fresh random shuffle of coherent/incoherent membership.
 * Useful at trial onset when you want a new random sample, even if
 * the coherence ratio is unchanged. Differs from setCoherences only
 * in that it always touches every dot. */
static int resampleCoherences(MOTIONPATCH *s)
{
  int N = s->num_dots;
  float coherence = s->coherence;
  for (int i = 0; i < N; i++) {
    s->dots[i].coherent = (((float) rand()/RAND_MAX) < coherence);
    if (s->dots[i].coherent) {
      s->dots[i].theta =
	(s->direction_jitter > 0.0f) ? mp_randn() * s->direction_jitter : 0.0f;
    }
    else {
      s->dots[i].theta = ((float) rand()/RAND_MAX) * 2.0f * (float) PI;
    }
  }
  return TCL_OK;
}

int motionpatchCreate(OBJ_LIST *objlist, SHADER_PROG *sp,
		      int n, float speed, float lifetime_s)
{
  const char *name = "Motionpatch";
  GR_OBJ *obj;
  MOTIONPATCH *s;
  Tcl_HashEntry *entryPtr;

  obj = gobjCreateObj();
  if (!obj) return -1;

  strcpy(GR_NAME(obj), name);
  GR_OBJTYPE(obj) = MotionpatchID;

  GR_ACTIONFUNCP(obj) = motionpatchDraw;
  GR_DELETEFUNCP(obj) = motionpatchDelete;
  GR_UPDATEFUNCP(obj) = motionpatchUpdate;

  s = (MOTIONPATCH *) calloc(1, sizeof(MOTIONPATCH));
  GR_CLIENTDATA(obj) = s;

  /* Default to white */
  s->color1[0] = s->color1[1] = s->color1[2] = s->color1[3] = 1.;
  s->color2[0] = s->color2[1] = s->color2[2] = s->color2[3] = 1.;
  s->pointsize = 1.0;
  s->mask_rotation = 0.0;
  s->mask_offset[0] = 0.0;
  s->mask_offset[1] = 0.0;
  s->mask_scale[0] = 1.0;
  s->mask_scale[1] = 1.0;
  s->mask_softness = 0.0;
  s->direction_jitter = 0.0;
  s->samplermaskmode = SMASK_NONE;
  
  s->num_dots = n;
  s->dots = (DOT *) calloc(n, sizeof(DOT));
  s->speed = speed;
  s->direction = 0.0;
  s->lifetime_s = lifetime_s;
  s->coherence = 1.0;
  s->last_stim_time_ms = -1.0;	/* sentinel: first update sees no prior frame */
  setPositions(s);
  setLifetimes(s, lifetime_s);
  setCoherences(s, s->coherence);  /* also seeds each dot's theta */

  s->mask_type = MASK_NONE;
  s->mask_radius = 0.5;
  
  s->noise_seed[0] = 77374;
  s->noise_seed[1] = 32452153;
  open_simplex_noise(s->noise_seed[0], &s->ctx[0]);
  open_simplex_noise(s->noise_seed[1], &s->ctx[1]);

  s->program = sp;
  copy_uniform_table(&sp->uniformTable, &s->uniformTable);
  copy_attrib_table(&sp->attribTable, &s->attribTable);

  /* Create vertex array object to hold buffer of verts to send to shader */
  s->vao_info = (VAO_INFO *) calloc(1, sizeof(VAO_INFO));
  s->vao_info->narrays = 0;

  glGenVertexArrays(1, &s->vao_info->vao);
  glBindVertexArray(s->vao_info->vao);  
  
  if ((entryPtr = Tcl_FindHashEntry(&s->attribTable, "vertex_position"))) {
    s->vao_info->npoints = s->num_dots;
    s->vao_info->points = 
      (GLfloat *) calloc(s->vao_info->npoints*3, sizeof(GLfloat));
    
    ATTRIB_INFO *ainfo = Tcl_GetHashValue(entryPtr);
    glGenBuffers(1, &s->vao_info->points_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, s->vao_info->points_vbo);
    glBufferData(GL_ARRAY_BUFFER, s->vao_info->npoints*3*sizeof(GLfloat),
		 s->vao_info->points, GL_STATIC_DRAW);
    glVertexAttribPointer(ainfo->location, 3, GL_FLOAT, GL_FALSE, 0, NULL);
    glEnableVertexAttribArray(ainfo->location);
    s->vao_info->nindices = s->num_dots;
    s->vao_info->narrays++;
  }

  if ((entryPtr = Tcl_FindHashEntry(&s->attribTable, "vertex_texcoord"))) {
    ATTRIB_INFO *ainfo = Tcl_GetHashValue(entryPtr);
    s->vao_info->ntexcoords = s->num_dots;
    s->vao_info->texcoords = 
      (GLfloat *) calloc(s->vao_info->ntexcoords*2, sizeof(GLfloat));
    
    glGenBuffers(1, &s->vao_info->texcoords_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, s->vao_info->texcoords_vbo);
    glBufferData(GL_ARRAY_BUFFER, s->vao_info->ntexcoords*sizeof(GLfloat),
		 s->vao_info->texcoords, GL_STATIC_DRAW);

    glVertexAttribPointer(ainfo->location, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    glEnableVertexAttribArray(ainfo->location);
    s->vao_info->narrays++;
  }

  if ((entryPtr = Tcl_FindHashEntry(&s->uniformTable, "modelviewMat"))) {
    s->modelviewMat = Tcl_GetHashValue(entryPtr);
    s->modelviewMat->val = malloc(sizeof(float)*16);
  }

  if ((entryPtr = Tcl_FindHashEntry(&s->uniformTable, "projMat"))) {
    s->projMat = Tcl_GetHashValue(entryPtr);
    s->projMat->val = malloc(sizeof(float)*16);
  }

  if ((entryPtr = Tcl_FindHashEntry(&s->uniformTable, "tex0"))) {
    s->tex0 = Tcl_GetHashValue(entryPtr);
    s->tex0->val = malloc(sizeof(int));
    *((int *)(s->tex0->val)) = 0;
  }
  
  if ((entryPtr = Tcl_FindHashEntry(&s->uniformTable, "uColor1"))) {
    s->uColor1 = Tcl_GetHashValue(entryPtr);
    s->uColor1->val = malloc(sizeof(float)*4);
  }
  if ((entryPtr = Tcl_FindHashEntry(&s->uniformTable, "uColor2"))) {
    s->uColor2 = Tcl_GetHashValue(entryPtr);
    s->uColor2->val = malloc(sizeof(float)*4);
  }
  if ((entryPtr = Tcl_FindHashEntry(&s->uniformTable, "pointSize"))) {
    s->pointSize = Tcl_GetHashValue(entryPtr);
    s->pointSize->val = calloc(1,sizeof(float));
  }
  if ((entryPtr = Tcl_FindHashEntry(&s->uniformTable, "samplerMaskMode"))) {
    s->samplerMaskMode = Tcl_GetHashValue(entryPtr);
    s->samplerMaskMode->val = calloc(1,sizeof(int));
  }
   if ((entryPtr = Tcl_FindHashEntry(&s->uniformTable, "maskRotation"))) {
     s->maskRotation = Tcl_GetHashValue(entryPtr);
     s->maskRotation->val = calloc(1, sizeof(float));
   }
   if ((entryPtr = Tcl_FindHashEntry(&s->uniformTable, "maskOffset"))) {
     s->maskOffset = Tcl_GetHashValue(entryPtr);
     s->maskOffset->val = calloc(2, sizeof(float));
   }
   if ((entryPtr = Tcl_FindHashEntry(&s->uniformTable, "maskScale"))) {
     s->maskScale = Tcl_GetHashValue(entryPtr);
     s->maskScale->val = calloc(2, sizeof(float));
     ((float *)s->maskScale->val)[0] = 1.0;
     ((float *)s->maskScale->val)[1] = 1.0;
   }
   if ((entryPtr = Tcl_FindHashEntry(&s->uniformTable, "maskSoftness"))) {
     s->maskSoftness = Tcl_GetHashValue(entryPtr);
     s->maskSoftness->val = calloc(1, sizeof(float));
   }
   if ((entryPtr = Tcl_FindHashEntry(&s->uniformTable, "tex1"))) {
     s->tex1 = Tcl_GetHashValue(entryPtr);
     s->tex1->val = malloc(sizeof(int));
     *((int *)(s->tex1->val)) = 1;	/* texture unit 1 */
   }
   if ((entryPtr = Tcl_FindHashEntry(&s->uniformTable, "layerModes"))) {
     s->layerModes = Tcl_GetHashValue(entryPtr);
     s->layerModes->val = calloc(4, sizeof(int));
   }
   if ((entryPtr = Tcl_FindHashEntry(&s->uniformTable, "layerDims"))) {
     s->layerDims = Tcl_GetHashValue(entryPtr);
     s->layerDims->val = calloc(4, sizeof(float));
   }
   if ((entryPtr = Tcl_FindHashEntry(&s->uniformTable, "layerColors"))) {
     s->layerColors = Tcl_GetHashValue(entryPtr);
     s->layerColors->val = calloc(16, sizeof(float));
   }
  s->texid[0] = -1;		/* initialize to no texture sampler */
  s->texid[1] = -1;		/* world-map sampler unset by default */
  /* All four layers default to mode=0 (off). When a layer is enabled,
     dim defaults to 0.25 and color to opaque white. */
  for (int li = 0; li < 4; li++) {
    s->layer_mode[li]    = 0;
    s->layer_dim[li]     = 0.25f;
    s->layer_color[li][0] = 1.0f;
    s->layer_color[li][1] = 1.0f;
    s->layer_color[li][2] = 1.0f;
    s->layer_color[li][3] = 1.0f;
  }

  return(gobjAddObj(objlist, obj));
}


static int motionpatchCmd(ClientData clientData, Tcl_Interp *interp,
			int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  int id, n;
  double speed, lifetime_s;
  char *handle;

  if (argc < 4) {
    Tcl_AppendResult(interp, "usage: ", argv[0],
		     " n speed_per_sec lifetime_seconds", NULL);
    return TCL_ERROR;
  }
  handle = argv[1];
  if (Tcl_GetInt(interp, argv[1], &n) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetDouble(interp, argv[2], &speed) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetDouble(interp, argv[3], &lifetime_s) != TCL_OK) return TCL_ERROR;
  if (lifetime_s <= 0.0 && lifetime_s != -1.0) lifetime_s = -1.0;

  if ((id = motionpatchCreate(olist, MotionpatchShaderProg,
			      n, (float) speed, (float) lifetime_s)) < 0) {
    Tcl_SetResult(interp, "error creating motionpatch", TCL_STATIC);
    return(TCL_ERROR);
  }

  Tcl_SetObjResult(interp, Tcl_NewIntObj(id));
  return(TCL_OK);
}


static int motionpatchSetSamplerCmd(ClientData clientData, Tcl_Interp *interp,
				   int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  MOTIONPATCH *g;
  int id;
  int texid;
  int sampler = 0;
  
  if (argc < 2) {
    Tcl_AppendResult(interp, "usage: ", argv[0],
		     " motionpatch [textureID] [sampler]", NULL);
    return TCL_ERROR;
  }

  if ((id = resolveObjId(interp, OL_NAMEINFO(olist), argv[1], MotionpatchID, "motionpatch")) < 0)
    return TCL_ERROR;

  if (argc > 2) {
    if (Tcl_GetInt(interp, argv[2], &texid) != TCL_OK) return TCL_ERROR;
  }

  if (argc > 3) {
    if (Tcl_GetInt(interp, argv[3], &sampler) != TCL_OK) return TCL_ERROR;
  }
  if (sampler < 0 || sampler >= NSAMPLERS) {
    Tcl_AppendResult(interp, argv[0], ": sampler out of range", NULL);
    return TCL_ERROR;
  }

  g = GR_CLIENTDATA(OL_OBJ(olist,id));

  g->texid[sampler] = texid;

  return(TCL_OK);
}


static int motionpatchSpeedCmd(ClientData clientData, Tcl_Interp *interp,
			     int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  MOTIONPATCH *s;
  int id;
  double speed;
  
  if (argc < 3) {
    Tcl_AppendResult(interp,
		     "usage: ", argv[0],
		     " motionpatch_speed motionpatch speed", NULL);
    return TCL_ERROR;
  }

  if ((id = resolveObjId(interp, OL_NAMEINFO(olist), argv[1], MotionpatchID, "motionpatch")) < 0)
    return TCL_ERROR;
  s = GR_CLIENTDATA(OL_OBJ(olist,id));

  if (Tcl_GetDouble(interp, argv[2], &speed) != TCL_OK) return TCL_ERROR;
  s->speed = speed;

  return(TCL_OK);

}

static int motionpatchUseNoiseDirectionCmd(ClientData clientData, Tcl_Interp *interp,
					   int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  MOTIONPATCH *s;
  int id;
  double period;
  int use_noise;
  double rate = 0.0;
  
  if (argc < 4) {
    Tcl_AppendResult(interp, "usage: ", argv[0],
		     " motionpatch useNoise period [rate]", NULL);
    return TCL_ERROR;
  }

  if ((id = resolveObjId(interp, OL_NAMEINFO(olist), argv[1], MotionpatchID, "motionpatch")) < 0)
    return TCL_ERROR;
  s = GR_CLIENTDATA(OL_OBJ(olist,id));

  if (Tcl_GetInt(interp, argv[2], &use_noise) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetDouble(interp, argv[3], &period) != TCL_OK) return TCL_ERROR;
  if (argc > 4) {
    if (Tcl_GetDouble(interp, argv[4], &rate) != TCL_OK) return TCL_ERROR;
  }
  s->noise_period = period;
  s->noise_update_rate = rate;
  s->set_direction_by_noise = use_noise;

  return(TCL_OK);
  
}

static int motionpatchSetSeedCmd(ClientData clientData, Tcl_Interp *interp,
				 int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  MOTIONPATCH *s;
  int id;
  int ctxid;
  int seed;
  
  if (argc < 4) {
    Tcl_AppendResult(interp, "usage: ", argv[0],
		     " motionpatch context_id seed", NULL);
    return TCL_ERROR;
  }

  if ((id = resolveObjId(interp, OL_NAMEINFO(olist), argv[1], MotionpatchID, "motionpatch")) < 0)
    return TCL_ERROR;
  s = GR_CLIENTDATA(OL_OBJ(olist,id));

  if (Tcl_GetInt(interp, argv[2], &ctxid) != TCL_OK) return TCL_ERROR;
  if (ctxid < 0 || ctxid >= MAX_NOISE_CTX) {
    Tcl_AppendResult(interp, argv[0], ": invalid noise context", NULL);
    return TCL_ERROR;
  }

  if (Tcl_GetInt(interp, argv[3], &seed) != TCL_OK) return TCL_ERROR;

  if (s->ctx[ctxid]) 
    open_simplex_noise_free(s->ctx[ctxid]);
  s->noise_seed[ctxid] = seed;
  open_simplex_noise(s->noise_seed[ctxid], &s->ctx[ctxid]);

  return(TCL_OK);
}

static int motionpatchSetNoiseZCmd(ClientData clientData, Tcl_Interp *interp,
				   int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  MOTIONPATCH *s;
  int id;
  double noise_z;
  
  if (argc < 3) {
    Tcl_AppendResult(interp, "usage: ", argv[0],
		     " motionpatch noise_z", NULL);
    return TCL_ERROR;
  }

  if ((id = resolveObjId(interp, OL_NAMEINFO(olist), argv[1], MotionpatchID, "motionpatch")) < 0)
    return TCL_ERROR;
  s = GR_CLIENTDATA(OL_OBJ(olist,id));

  if (Tcl_GetDouble(interp, argv[2], &noise_z) != TCL_OK) return TCL_ERROR;
  s->noise_z = noise_z;

  return(TCL_OK);
}

static int motionpatchNoiseUpdateZCmd(ClientData clientData, Tcl_Interp *interp,
				      int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  MOTIONPATCH *s;
  int id;
  int do_update;
  
  if (argc < 3) {
    Tcl_AppendResult(interp, "usage: ", argv[0],
		     " motionpatch update_by_stimtime", NULL);
    return TCL_ERROR;
  }

  if ((id = resolveObjId(interp, OL_NAMEINFO(olist), argv[1], MotionpatchID, "motionpatch")) < 0)
    return TCL_ERROR;
  s = GR_CLIENTDATA(OL_OBJ(olist,id));

  if (Tcl_GetInt(interp, argv[2], &do_update) != TCL_OK) return TCL_ERROR;
  s->noise_update_z = do_update;

  return(TCL_OK);
}


static int motionpatchDirectionCmd(ClientData clientData, Tcl_Interp *interp,
				   int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  MOTIONPATCH *s;
  int id;
  double direction;
  
  if (argc < 3) {
    Tcl_AppendResult(interp, "usage: ", argv[0],
		     " motionpatch direction", NULL);
    return TCL_ERROR;
  }

  if ((id = resolveObjId(interp, OL_NAMEINFO(olist), argv[1], MotionpatchID, "motionpatch")) < 0)
    return TCL_ERROR;
  s = GR_CLIENTDATA(OL_OBJ(olist,id));

  if (Tcl_GetDouble(interp, argv[2], &direction) != TCL_OK) return TCL_ERROR;
  s->direction = direction;

  return(TCL_OK);
}

static int motionpatchCoherenceCmd(ClientData clientData, Tcl_Interp *interp,
				   int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  MOTIONPATCH *s;
  int id;
  double coherence;
  
  if (argc < 3) {
    Tcl_AppendResult(interp, "usage: ", argv[0],
		     " motionpatch coherence", NULL);
    return TCL_ERROR;
  }

  if ((id = resolveObjId(interp, OL_NAMEINFO(olist), argv[1], MotionpatchID, "motionpatch")) < 0)
    return TCL_ERROR;
  s = GR_CLIENTDATA(OL_OBJ(olist,id));

  if (Tcl_GetDouble(interp, argv[2], &coherence) != TCL_OK) return TCL_ERROR;
  if (coherence < 0.0 || coherence > 1.0) {
    Tcl_AppendResult(interp, argv[0],
		     ": coherence must between between 0.0 and 1.0", NULL);
    return TCL_ERROR;
  }
  s->coherence = coherence;
  setCoherences(s, coherence);
  return(TCL_OK);
}

/* Force a full re-roll of which dots are coherent and re-seed every
 * dot's theta. Use at trial onset to get a fresh random sample;
 * setCoherences (motionpatch_coherence) is stable-membership and
 * preserves existing assignments where possible. */
static int motionpatchResampleCoherenceCmd(ClientData clientData, Tcl_Interp *interp,
					   int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  MOTIONPATCH *s;
  int id;

  if (argc < 2) {
    Tcl_AppendResult(interp, "usage: ", argv[0], " motionpatch", NULL);
    return TCL_ERROR;
  }
  if ((id = resolveObjId(interp, OL_NAMEINFO(olist), argv[1],
			 MotionpatchID, "motionpatch")) < 0)
    return TCL_ERROR;
  s = GR_CLIENTDATA(OL_OBJ(olist,id));

  resampleCoherences(s);
  return TCL_OK;
}


static int motionpatchPointsizeCmd(ClientData clientData, Tcl_Interp *interp,
			int argc, char *argv[])
{

  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  MOTIONPATCH *s;
  int id;
  double pointsize;
  
  if (argc < 3) {
    Tcl_AppendResult(interp, "usage: ", argv[0],
		     " motionpatch pointsize", NULL);
    return TCL_ERROR;
  }

  if ((id = resolveObjId(interp, OL_NAMEINFO(olist), argv[1], MotionpatchID, "motionpatch")) < 0)
    return TCL_ERROR;
  s = GR_CLIENTDATA(OL_OBJ(olist,id));

  if (Tcl_GetDouble(interp, argv[2], &pointsize) != TCL_OK) return TCL_ERROR;
  s->pointsize = pointsize;

  return(TCL_OK);
}


static int motionpatchMaskTypeCmd(ClientData clientData, Tcl_Interp *interp,
				  int argc, char *argv[])
{
  
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  MOTIONPATCH *s;
  int id;
  int type;
  
  if (argc < 3) {
    Tcl_AppendResult(interp, "usage: ", argv[0],
		     " motionpatch type", NULL);
    return TCL_ERROR;
  }

  if ((id = resolveObjId(interp, OL_NAMEINFO(olist), argv[1], MotionpatchID, "motionpatch")) < 0)
    return TCL_ERROR;
  s = GR_CLIENTDATA(OL_OBJ(olist,id));

  if (Tcl_GetInt(interp, argv[2], &type) != TCL_OK) return TCL_ERROR;
  if (type < 0 || type >= MASK_LAST) {
    Tcl_AppendResult(interp, argv[0], ": invalid mask type specified", NULL);
    return TCL_ERROR;
  }
  s->mask_type = type;

  return(TCL_OK);
}

static int motionpatchSamplerMaskModeCmd(ClientData clientData, Tcl_Interp *interp,
					 int argc, char *argv[])
{
  
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  MOTIONPATCH *s;
  int id;
  int mode;
  
  if (argc < 3) {
    Tcl_AppendResult(interp, "usage: ", argv[0],
		     " motionpatch mode", NULL);
    return TCL_ERROR;
  }

  if ((id = resolveObjId(interp, OL_NAMEINFO(olist), argv[1], MotionpatchID, "motionpatch")) < 0)
    return TCL_ERROR;
  s = GR_CLIENTDATA(OL_OBJ(olist,id));

  if (Tcl_GetInt(interp, argv[2], &mode) != TCL_OK) return TCL_ERROR;
  if (mode < 0 || mode >= SMASK_LAST) {
    Tcl_AppendResult(interp, argv[0], ": invalid sampler mask mode specified", NULL);
    return TCL_ERROR;
  }
  s->samplermaskmode = mode;

  return(TCL_OK);
}

/* Resolve a channel name or index to 0..3. Accepts "R"/"G"/"B"/"A"
 * (and lowercase) or integers 0..3. */
static int motionpatchResolveLayer(Tcl_Interp *interp, const char *spec, int *out)
{
  if (spec[0] != '\0' && spec[1] == '\0') {
    switch (spec[0]) {
    case 'R': case 'r': *out = 0; return TCL_OK;
    case 'G': case 'g': *out = 1; return TCL_OK;
    case 'B': case 'b': *out = 2; return TCL_OK;
    case 'A': case 'a': *out = 3; return TCL_OK;
    }
  }
  /* Integer fallback. */
  int v;
  if (Tcl_GetInt(interp, spec, &v) != TCL_OK) return TCL_ERROR;
  if (v < 0 || v > 3) {
    Tcl_AppendResult(interp,
		     "channel must be 0..3 or one of R/G/B/A", NULL);
    return TCL_ERROR;
  }
  *out = v;
  return TCL_OK;
}

/* motionpatch_layermode mp channel mode  (channel: 0..3 or R/G/B/A)
 *   mode: 0=off, 1=dim, 2=tint, 3=hide */
static int motionpatchLayerModeCmd(ClientData clientData, Tcl_Interp *interp,
				   int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  MOTIONPATCH *s;
  int id, channel, mode;

  if (argc < 4) {
    Tcl_AppendResult(interp, "usage: ", argv[0],
		     " motionpatch channel mode", NULL);
    return TCL_ERROR;
  }
  if ((id = resolveObjId(interp, OL_NAMEINFO(olist), argv[1],
			 MotionpatchID, "motionpatch")) < 0)
    return TCL_ERROR;
  s = GR_CLIENTDATA(OL_OBJ(olist,id));

  if (motionpatchResolveLayer(interp, argv[2], &channel) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetInt(interp, argv[3], &mode) != TCL_OK) return TCL_ERROR;
  if (mode < 0 || mode > 3) {
    Tcl_AppendResult(interp, argv[0], ": invalid mode (expected 0..3)", NULL);
    return TCL_ERROR;
  }
  s->layer_mode[channel] = mode;
  return TCL_OK;
}

static int motionpatchLayerDimCmd(ClientData clientData, Tcl_Interp *interp,
				  int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  MOTIONPATCH *s;
  int id, channel;
  double dim;

  if (argc < 4) {
    Tcl_AppendResult(interp, "usage: ", argv[0],
		     " motionpatch channel dim_factor", NULL);
    return TCL_ERROR;
  }
  if ((id = resolveObjId(interp, OL_NAMEINFO(olist), argv[1],
			 MotionpatchID, "motionpatch")) < 0)
    return TCL_ERROR;
  s = GR_CLIENTDATA(OL_OBJ(olist,id));

  if (motionpatchResolveLayer(interp, argv[2], &channel) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetDouble(interp, argv[3], &dim) != TCL_OK) return TCL_ERROR;
  s->layer_dim[channel] = (float) dim;
  return TCL_OK;
}

static int motionpatchLayerColorCmd(ClientData clientData, Tcl_Interp *interp,
				    int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  MOTIONPATCH *s;
  int id, channel;
  double r, g, b, a;

  if (argc < 7) {
    Tcl_AppendResult(interp, "usage: ", argv[0],
		     " motionpatch channel r g b a", NULL);
    return TCL_ERROR;
  }
  if ((id = resolveObjId(interp, OL_NAMEINFO(olist), argv[1],
			 MotionpatchID, "motionpatch")) < 0)
    return TCL_ERROR;
  s = GR_CLIENTDATA(OL_OBJ(olist,id));

  if (motionpatchResolveLayer(interp, argv[2], &channel) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetDouble(interp, argv[3], &r) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetDouble(interp, argv[4], &g) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetDouble(interp, argv[5], &b) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetDouble(interp, argv[6], &a) != TCL_OK) return TCL_ERROR;
  s->layer_color[channel][0] = (float) r;
  s->layer_color[channel][1] = (float) g;
  s->layer_color[channel][2] = (float) b;
  s->layer_color[channel][3] = (float) a;
  return TCL_OK;
}

/* Backward-compat: motionpatch_worldmaskmode/dim/color operate on the
 * alpha channel (layer index 3). Existing demos continue to work
 * unchanged; new code can target other channels via the layer*
 * commands above. */
static int motionpatchWorldMaskModeCmd(ClientData clientData, Tcl_Interp *interp,
				       int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  MOTIONPATCH *s;
  int id, mode;

  if (argc < 3) {
    Tcl_AppendResult(interp, "usage: ", argv[0],
		     " motionpatch mode", NULL);
    return TCL_ERROR;
  }
  if ((id = resolveObjId(interp, OL_NAMEINFO(olist), argv[1], MotionpatchID, "motionpatch")) < 0)
    return TCL_ERROR;
  s = GR_CLIENTDATA(OL_OBJ(olist,id));

  if (Tcl_GetInt(interp, argv[2], &mode) != TCL_OK) return TCL_ERROR;
  if (mode < 0 || mode > 3) {
    Tcl_AppendResult(interp, argv[0], ": invalid worldmaskmode (expected 0..3)", NULL);
    return TCL_ERROR;
  }
  s->layer_mode[3] = mode;
  return(TCL_OK);
}

static int motionpatchWorldDimCmd(ClientData clientData, Tcl_Interp *interp,
				  int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  MOTIONPATCH *s;
  int id;
  double dim;

  if (argc < 3) {
    Tcl_AppendResult(interp, "usage: ", argv[0],
		     " motionpatch dim_factor", NULL);
    return TCL_ERROR;
  }
  if ((id = resolveObjId(interp, OL_NAMEINFO(olist), argv[1], MotionpatchID, "motionpatch")) < 0)
    return TCL_ERROR;
  s = GR_CLIENTDATA(OL_OBJ(olist,id));

  if (Tcl_GetDouble(interp, argv[2], &dim) != TCL_OK) return TCL_ERROR;
  s->layer_dim[3] = (float) dim;
  return(TCL_OK);
}

static int motionpatchWorldColorCmd(ClientData clientData, Tcl_Interp *interp,
				    int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  MOTIONPATCH *s;
  int id;
  double r, g, b, a;

  if (argc < 6) {
    Tcl_AppendResult(interp, "usage: ", argv[0],
		     " motionpatch r g b a", NULL);
    return TCL_ERROR;
  }
  if ((id = resolveObjId(interp, OL_NAMEINFO(olist), argv[1], MotionpatchID, "motionpatch")) < 0)
    return TCL_ERROR;
  s = GR_CLIENTDATA(OL_OBJ(olist,id));

  if (Tcl_GetDouble(interp, argv[2], &r) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetDouble(interp, argv[3], &g) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetDouble(interp, argv[4], &b) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetDouble(interp, argv[5], &a) != TCL_OK) return TCL_ERROR;
  s->layer_color[3][0] = (float) r;
  s->layer_color[3][1] = (float) g;
  s->layer_color[3][2] = (float) b;
  s->layer_color[3][3] = (float) a;
  return(TCL_OK);
}

static int motionpatchMaskRadiusCmd(ClientData clientData, Tcl_Interp *interp,
				    int argc, char *argv[])
{
  
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  MOTIONPATCH *s;
  int id;
  double radius;
  
  if (argc < 3) {
    Tcl_AppendResult(interp, "usage: ", argv[0],
		     " motionpatch radius", NULL);
    return TCL_ERROR;
  }

  if ((id = resolveObjId(interp, OL_NAMEINFO(olist), argv[1], MotionpatchID, "motionpatch")) < 0)
    return TCL_ERROR;
  s = GR_CLIENTDATA(OL_OBJ(olist,id));

  if (Tcl_GetDouble(interp, argv[2], &radius) != TCL_OK) return TCL_ERROR;
  s->mask_radius = radius;

  return(TCL_OK);
}


static int motionpatchColorCmd(ClientData clientData, Tcl_Interp *interp,
			    int argc, char *argv[])
{

  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  MOTIONPATCH *s;
  double r, g, b, a;
  int id;

  if (argc < 5) {
    Tcl_AppendResult(interp, "usage: ", argv[0],
		     " motionpatch r g b ?a?", NULL);
    return TCL_ERROR;
  }

  if ((id = resolveObjId(interp, OL_NAMEINFO(olist), argv[1], MotionpatchID, "motionpatch")) < 0)
    return TCL_ERROR;
  s = GR_CLIENTDATA(OL_OBJ(olist,id));

  if (Tcl_GetDouble(interp, argv[2], &r) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetDouble(interp, argv[3], &g) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetDouble(interp, argv[4], &b) != TCL_OK) return TCL_ERROR;
  if (argc > 5) {
    if (Tcl_GetDouble(interp, argv[5], &a) != TCL_OK) return TCL_ERROR;
  }
  else {
    a = 1.0;
  }

  if (!strcmp(argv[0], "motionpatch_color")) {
    s->color1[0] = r;
    s->color1[1] = g;
    s->color1[2] = b;
    s->color1[3] = a;
  }
  else {
    s->color2[0] = r;
    s->color2[1] = g;
    s->color2[2] = b;
    s->color2[3] = a;
  }

  return(TCL_OK);
}

static int motionpatchMaskRotationCmd(ClientData clientData, Tcl_Interp *interp,
                                      int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  MOTIONPATCH *s;
  int id;
  double rotation;
  
  if (argc < 3) {
    Tcl_AppendResult(interp, "usage: ", argv[0],
                     " motionpatch rotation_radians", NULL);
    return TCL_ERROR;
  }
  
  if ((id = resolveObjId(interp, OL_NAMEINFO(olist), argv[1],
			 MotionpatchID, "motionpatch")) < 0)
    return TCL_ERROR;
  s = GR_CLIENTDATA(OL_OBJ(olist,id));
  
  if (Tcl_GetDouble(interp, argv[2], &rotation) != TCL_OK) return TCL_ERROR;
  s->mask_rotation = rotation;

  return(TCL_OK);
}

static int motionpatchRefreshPositionsCmd(ClientData clientData, Tcl_Interp *interp,
                                          int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  MOTIONPATCH *s;
  int id, i;

  if (argc < 2) {
    Tcl_AppendResult(interp, "usage: ", argv[0], " motionpatch", NULL);
    return TCL_ERROR;
  }
  if ((id = resolveObjId(interp, OL_NAMEINFO(olist), argv[1],
                         MotionpatchID, "motionpatch")) < 0)
    return TCL_ERROR;
  s = GR_CLIENTDATA(OL_OBJ(olist,id));

  /* Re-sample every dot's position uniformly, and re-stagger the
     respawn phase so short-lifetime patches don't flicker en masse.
     Speeds/coherence/direction are left untouched. */
  setPositions(s);
  if (s->lifetime_s > 0.0f) {
    for (i = 0; i < s->num_dots; i++) {
      s->dots[i].elapsed_s =
	((float) rand() / (float) RAND_MAX) * s->lifetime_s;
    }
  }
  return TCL_OK;
}

static int motionpatchMaskSoftnessCmd(ClientData clientData, Tcl_Interp *interp,
                                      int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  MOTIONPATCH *s;
  int id;
  double softness;

  if (argc < 3) {
    Tcl_AppendResult(interp, "usage: ", argv[0],
                     " motionpatch softness (0 = hard, up to 1)", NULL);
    return TCL_ERROR;
  }
  if ((id = resolveObjId(interp, OL_NAMEINFO(olist), argv[1],
                         MotionpatchID, "motionpatch")) < 0)
    return TCL_ERROR;
  s = GR_CLIENTDATA(OL_OBJ(olist,id));

  if (Tcl_GetDouble(interp, argv[2], &softness) != TCL_OK) return TCL_ERROR;
  if (softness < 0.0) softness = 0.0;
  if (softness > 1.0) softness = 1.0;
  s->mask_softness = (float) softness;
  return(TCL_OK);
}

static int motionpatchDirectionJitterCmd(ClientData clientData, Tcl_Interp *interp,
                                         int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  MOTIONPATCH *s;
  int id, i;
  double sigma;

  if (argc < 3) {
    Tcl_AppendResult(interp, "usage: ", argv[0],
                     " motionpatch sigma_radians", NULL);
    return TCL_ERROR;
  }
  if ((id = resolveObjId(interp, OL_NAMEINFO(olist), argv[1],
                         MotionpatchID, "motionpatch")) < 0)
    return TCL_ERROR;
  s = GR_CLIENTDATA(OL_OBJ(olist,id));

  if (Tcl_GetDouble(interp, argv[2], &sigma) != TCL_OK) return TCL_ERROR;
  if (sigma < 0.0) sigma = 0.0;
  s->direction_jitter = (float) sigma;

  /* Resample per-dot theta immediately so the change is visible
     without waiting for respawns. For coherent dots, theta is the
     small jitter offset around s->direction; incoherent dots keep
     their existing random absolute angle. */
  for (i = 0; i < s->num_dots; i++) {
    if (s->dots[i].coherent) {
      s->dots[i].theta = (sigma > 0.0) ? mp_randn() * (float) sigma : 0.0f;
    }
  }
  return(TCL_OK);
}

static int motionpatchLifetimeCmd(ClientData clientData, Tcl_Interp *interp,
                                  int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  MOTIONPATCH *s;
  int id;
  double lifetime_s;

  if (argc < 3) {
    Tcl_AppendResult(interp, "usage: ", argv[0],
                     " motionpatch lifetime_seconds", NULL);
    return TCL_ERROR;
  }

  if ((id = resolveObjId(interp, OL_NAMEINFO(olist), argv[1],
                         MotionpatchID, "motionpatch")) < 0)
    return TCL_ERROR;
  s = GR_CLIENTDATA(OL_OBJ(olist,id));

  if (Tcl_GetDouble(interp, argv[2], &lifetime_s) != TCL_OK) return TCL_ERROR;
  /* Negative = no respawn (dots live forever). Caller can use -1 or
   * 0 for that; we accept any non-positive value as "no respawn". */
  if (lifetime_s <= 0.0 && lifetime_s != -1.0) lifetime_s = -1.0;

  s->lifetime_s = (float) lifetime_s;
  setLifetimes(s, (float) lifetime_s);

  return(TCL_OK);
}

static int motionpatchMaskOffsetCmd(ClientData clientData, Tcl_Interp *interp,
                                    int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  MOTIONPATCH *s;
  int id;
  double ox, oy;

  if (argc < 4) {
    Tcl_AppendResult(interp, "usage: ", argv[0],
                     " motionpatch offset_x offset_y", NULL);
    return TCL_ERROR;
  }

  if ((id = resolveObjId(interp, OL_NAMEINFO(olist), argv[1],
                         MotionpatchID, "motionpatch")) < 0)
    return TCL_ERROR;
  s = GR_CLIENTDATA(OL_OBJ(olist,id));

  if (Tcl_GetDouble(interp, argv[2], &ox) != TCL_OK) return TCL_ERROR;
  if (Tcl_GetDouble(interp, argv[3], &oy) != TCL_OK) return TCL_ERROR;
  s->mask_offset[0] = ox;
  s->mask_offset[1] = oy;

  return(TCL_OK);
}

static int motionpatchMaskScaleCmd(ClientData clientData, Tcl_Interp *interp,
                                   int argc, char *argv[])
{
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  MOTIONPATCH *s;
  int id;
  double sx, sy;

  if (argc < 3) {
    Tcl_AppendResult(interp, "usage: ", argv[0],
                     " motionpatch scale ?scale_y?", NULL);
    return TCL_ERROR;
  }

  if ((id = resolveObjId(interp, OL_NAMEINFO(olist), argv[1],
                         MotionpatchID, "motionpatch")) < 0)
    return TCL_ERROR;
  s = GR_CLIENTDATA(OL_OBJ(olist,id));

  if (Tcl_GetDouble(interp, argv[2], &sx) != TCL_OK) return TCL_ERROR;
  if (argc > 3) {
    if (Tcl_GetDouble(interp, argv[3], &sy) != TCL_OK) return TCL_ERROR;
  } else {
    sy = sx;
  }
  if (sx == 0.0 || sy == 0.0) {
    Tcl_AppendResult(interp, argv[0], ": scale must be non-zero", NULL);
    return TCL_ERROR;
  }
  s->mask_scale[0] = sx;
  s->mask_scale[1] = sy;

  return(TCL_OK);
}

int motionpatchShaderCreate(Tcl_Interp *interp)
{
  MotionpatchShaderProg = (SHADER_PROG *) calloc(1, sizeof(SHADER_PROG));

  const char* vertex_shader =
  #ifndef STIM2_USE_GLES
    "# version 330\n"
  #else
    "# version 300 es\n"  
  #endif
    "in vec3 vertex_position;"
    "in vec2 vertex_texcoord;"
    "out vec2 texcoord;"
    "uniform mat4 projMat;"
    "uniform mat4 modelviewMat;"
    "uniform float pointSize;"

    "void main () {"
    " gl_PointSize = pointSize;"
    " texcoord  = vertex_texcoord;"
    " gl_Position = projMat * modelviewMat * vec4(vertex_position, 1.0);"
    "}";

  const char* fragment_shader =
  #ifndef STIM2_USE_GLES
    "# version 330\n"
  #else
    "# version 300 es\n"  
  #endif
  
    "#ifdef GL_ES\n"
    "precision mediump float;"
    "precision mediump int;\n"
    "#endif\n"

    "uniform sampler2D tex0;"
    "uniform sampler2D tex1;"
    "uniform int samplerMaskMode;"
    "uniform float maskRotation;"
    "uniform vec2 maskOffset;"
    "uniform vec2 maskScale;"
    "uniform float maskSoftness;"
    "in vec2 texcoord;"
    "uniform vec4 uColor1;"
    "uniform vec4 uColor2;"
    /* Per-layer state for the world-map sampler (tex1). Each RGBA
       channel is an independent alpha mask interpreted as a separate
       semantic region (mode/color/dim per channel).
       layerModes[i]: 0=off, 1=dim, 2=tint, 3=hide (per channel i)
       layerDims[i]:  alpha multiplier for mode 1
       layerColors[i]: tint for mode 2 (one column per layer) */
    "uniform ivec4 layerModes;"
    "uniform vec4  layerDims;"
    "uniform mat4  layerColors;"
    "out vec4 frag_color;"

    "void applyLayer(in int mode, in float ch, in float dim, in vec4 col,"
    "                inout vec3 color, inout float alpha) {"
    "  if (mode == 1) { alpha *= mix(1.0, dim, ch); }"
    "  else if (mode == 2) {"
    "    color = mix(color, col.rgb, ch);"
    "    alpha *= mix(1.0, col.a, ch);"
    "  }"
    "  else if (mode == 3) { alpha *= (1.0 - ch); }"
    "}"
    "void main () {"
    " vec2 tc = (texcoord - 0.5 - maskOffset) / maskScale;"
    " float c = cos(maskRotation);"
    " float s = sin(maskRotation);"
    " vec2 rotated = vec2(c * tc.x - s * tc.y, s * tc.x + c * tc.y) + 0.5;"
    " float texAlpha;"
    " vec3 texColor;"
    " if (any(lessThan(rotated, vec2(0.0))) || any(greaterThan(rotated, vec2(1.0)))) {"
    "   texAlpha = 0.0; texColor = vec3(0.0);"
    " } else {"
    "   vec4 samp = texture(tex0, vec2(rotated.s, 1.0-rotated.t));"
    "   texColor = samp.rgb;"
    "   texAlpha = samp.a;"
    " }"
    " if (maskSoftness > 0.0) {"
    "   float w = maskSoftness * 0.5;"
    "   texAlpha = smoothstep(0.5 - w, 0.5 + w, texAlpha);"
    " }"
    " float alpha = 1.0;"
    " vec3 color;"
    " if (samplerMaskMode == 0) { alpha = uColor1.a; color = uColor1.rgb; }"
    " else if (samplerMaskMode == 1) { alpha = texAlpha * uColor1.a; color = uColor1.rgb; }"
    " else if (samplerMaskMode == 2) { alpha = (1.0-texAlpha) * uColor1.a; color = uColor1.rgb; }"
    " else if (samplerMaskMode == 3) { if (texAlpha < 0.5) { alpha = uColor1.a; color = uColor1.rgb; }"
    "                                  else { alpha = uColor2.a; color = uColor2.rgb;} }"
    /* World-map (tex1) modulation. tex1 is sampled at the dot's raw
       patch-local texcoord (no offset/scale/rotation), so the world
       map stays fixed in the patch frame even when the primary mask
       translates. Each RGBA channel is an independent alpha mask;
       applyLayer composes one channel into (color, alpha) per call.
       Layers compose in channel order R, G, B, A -- later layers
       overwrite earlier ones in tint mode. */
    " vec4 wsamp = texture(tex1, vec2(texcoord.s, 1.0-texcoord.t));"
    " applyLayer(layerModes.r, wsamp.r, layerDims.r, layerColors[0], color, alpha);"
    " applyLayer(layerModes.g, wsamp.g, layerDims.g, layerColors[1], color, alpha);"
    " applyLayer(layerModes.b, wsamp.b, layerDims.b, layerColors[2], color, alpha);"
    " applyLayer(layerModes.a, wsamp.a, layerDims.a, layerColors[3], color, alpha);"
    " frag_color = vec4 (color, alpha);"
    "}";
  
  if (build_prog(MotionpatchShaderProg,
		 vertex_shader, fragment_shader, 0) == -1) {
    Tcl_AppendResult(interp,
		     "motionpatch : error building motionpatch shader", NULL);
    return TCL_ERROR;
  }

  /* Now add uniforms into master table */
  Tcl_InitHashTable(&MotionpatchShaderProg->uniformTable, TCL_STRING_KEYS);
  add_uniforms_to_table(&MotionpatchShaderProg->uniformTable,
			MotionpatchShaderProg);

  /* Now add attribs into master table */
  Tcl_InitHashTable(&MotionpatchShaderProg->attribTable, TCL_STRING_KEYS);
  add_attribs_to_table(&MotionpatchShaderProg->attribTable,
		       MotionpatchShaderProg);

  /* 1x1 white RGBA texture bound when no user sampler is set, so
     the fragment shader's tex0 / tex1 samplers are always backed by
     valid storage. Bind to BOTH texture unit 0 AND unit 1 at create
     time so the first draw -- and any link-time validation -- finds
     valid textures on every sampler unit the shader references.
     Without binding to unit 1, Apple's GL driver logs an "unit 1
     unloadable, using zero texture" warning the first time the
     shader program is used, before motionpatchDraw has a chance to
     run its per-frame bind. */
  if (!MotionpatchDefaultTex) {
    unsigned char white[4] = { 255, 255, 255, 255 };
    glGenTextures(1, &MotionpatchDefaultTex);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, MotionpatchDefaultTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, white);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    /* Pre-bind to unit 1 as well so it isn't in an unloadable state
       before the first per-frame bind. */
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, MotionpatchDefaultTex);
    glActiveTexture(GL_TEXTURE0);
  }

  return TCL_OK;
}


#ifdef WIN32
EXPORT(int,Motionpatch_Init) (Tcl_Interp *interp)
#else
int Motionpatch_Init(Tcl_Interp *interp)
#endif
{
  OBJ_LIST *OBJList = getOBJList();

  if (
#ifdef USE_TCL_STUBS
      Tcl_InitStubs(interp, "8.5-", 0)
#else
      Tcl_PkgRequire(interp, "Tcl", "8.5-", 0)
#endif
      == NULL) {
    return TCL_ERROR;
  }
  
  if (MotionpatchID < 0) MotionpatchID = gobjRegisterType("motionpatch");

  gladLoadGL();

  motionpatchShaderCreate(interp);

  srand(time(NULL));
  
  Tcl_CreateCommand(interp, "motionpatch", (Tcl_CmdProc *) motionpatchCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "motionpatch_speed",
		    (Tcl_CmdProc *) motionpatchSpeedCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "motionpatch_direction",
		    (Tcl_CmdProc *) motionpatchDirectionCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "motionpatch_useNoiseDirection",
		    (Tcl_CmdProc *) motionpatchUseNoiseDirectionCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "motionpatch_setSampler",
		    (Tcl_CmdProc *) motionpatchSetSamplerCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "motionpatch_setSeed",
		    (Tcl_CmdProc *) motionpatchSetSeedCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "motionpatch_setNoiseZ",
		    (Tcl_CmdProc *) motionpatchSetNoiseZCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "motionpatch_noiseUpdateZ",
		    (Tcl_CmdProc *) motionpatchNoiseUpdateZCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "motionpatch_resampleCoherence",
		    (Tcl_CmdProc *) motionpatchResampleCoherenceCmd,
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "motionpatch_coherence",
		    (Tcl_CmdProc *) motionpatchCoherenceCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "motionpatch_pointsize", 
		    (Tcl_CmdProc *) motionpatchPointsizeCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "motionpatch_masktype", 
		    (Tcl_CmdProc *) motionpatchMaskTypeCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "motionpatch_samplermaskmode",
		    (Tcl_CmdProc *) motionpatchSamplerMaskModeCmd,
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "motionpatch_worldmaskmode",
		    (Tcl_CmdProc *) motionpatchWorldMaskModeCmd,
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "motionpatch_worlddim",
		    (Tcl_CmdProc *) motionpatchWorldDimCmd,
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "motionpatch_worldcolor",
		    (Tcl_CmdProc *) motionpatchWorldColorCmd,
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "motionpatch_layermode",
		    (Tcl_CmdProc *) motionpatchLayerModeCmd,
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "motionpatch_layerdim",
		    (Tcl_CmdProc *) motionpatchLayerDimCmd,
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "motionpatch_layercolor",
		    (Tcl_CmdProc *) motionpatchLayerColorCmd,
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "motionpatch_maskrotation",
		    (Tcl_CmdProc *) motionpatchMaskRotationCmd,
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "motionpatch_lifetime",
		    (Tcl_CmdProc *) motionpatchLifetimeCmd,
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "motionpatch_refreshPositions",
		    (Tcl_CmdProc *) motionpatchRefreshPositionsCmd,
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "motionpatch_masksoftness",
		    (Tcl_CmdProc *) motionpatchMaskSoftnessCmd,
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "motionpatch_directionjitter",
		    (Tcl_CmdProc *) motionpatchDirectionJitterCmd,
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "motionpatch_maskoffset",
		    (Tcl_CmdProc *) motionpatchMaskOffsetCmd,
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "motionpatch_maskscale",
		    (Tcl_CmdProc *) motionpatchMaskScaleCmd,
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "motionpatch_maskradius", 
		    (Tcl_CmdProc *) motionpatchMaskRadiusCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "motionpatch_color", 
		    (Tcl_CmdProc *) motionpatchColorCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  Tcl_CreateCommand(interp, "motionpatch_color2", 
		    (Tcl_CmdProc *) motionpatchColorCmd, 
		    (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
  return TCL_OK;
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
