/*
 * animate.h
 * Animation module for stim2
 *
 * Provides declarative animation primitives for graphics objects.
 * Animations are processed per-frame in the render loop with minimal overhead.
 *
 * Usage from Tcl:
 *   animateRotation $obj -speed 45.0           ;# rotate at 45 deg/sec
 *   animateOpacity $obj -pulse 1.0 -min 0.3    ;# pulse opacity at 1Hz
 *   animateColor $obj -cycle 0.5               ;# cycle hue at 0.5Hz
 *   animateCustom $obj { rotateObj $obj [expr {sin($t) * 30}] }
 *
 * Frame-based for psychophysics:
 *   animateRotation $obj -speed 0.5 -perframe  ;# 0.5 deg/frame
 *   animateSequence $obj opacity {1 1 0.5 0.5 0 0} -loop
 */

#ifndef ANIMATE_H
#define ANIMATE_H

#include "stim2.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Animation types
 */
typedef enum {
    ANIM_NONE = 0,
    ANIM_ROTATION,
    ANIM_OPACITY,
    ANIM_SCALE,
    ANIM_POSITION,
    ANIM_COLOR,
    ANIM_BLINK,
    ANIM_SEQUENCE,
    ANIM_CUSTOM
} AnimType;

/*
 * Animation state for a single property
 */
typedef struct _anim_property {
    AnimType type;
    int active;
    int perframe;           /* 0 = time-based, 1 = frame-based */
    
    /* Common parameters */
    float speed;            /* rate of change (per sec or per frame) */
    float freq;             /* oscillation frequency */
    float phase;            /* current phase (radians) */
    float min_val;
    float max_val;
    float amplitude;
    
    /* For position/velocity */
    float vx, vy, vz;
    
    /* For color cycling */
    int color_mode;         /* 0=off, 1=replace, 2=multiply */
    
    /* For sequences */
    float *sequence;
    int seq_length;
    int seq_index;
    int seq_loop;
    
    /* For custom scripts/procs */
    char *script;           /* inline script (legacy) */
    char *proc_name;        /* proc name for structured custom */
    char *params;           /* Tcl dict string of param values */
    
    struct _anim_property *next;  /* linked list for multiple anims per obj */
} AnimProperty;

/*
 * Animation state attached to an object
 */
typedef struct _anim_state {
    GR_OBJ *obj;               /* back-pointer to object */
    AnimProperty *properties;  /* linked list of animated properties */
    unsigned int start_time;   /* StimTime when animation started (ms) */
    unsigned int frame_count;  /* frames since animation started */
} AnimState;

/*
 * Core functions called from stim2 render loop
 */

/* Update all animations on an object - call before pre-scripts 
 * Uses StimTicks (never resets) for stable timing.
 * ticks_ms: current StimTicks value
 * dt_ms: StimDeltaTime (time since last frame)
 */
void animateUpdateObj(GR_OBJ *obj, unsigned int ticks_ms, unsigned int dt_ms);

/* Clear all animations on an object - call when object destroyed */
void animateClearObj(GR_OBJ *obj);

/* Global init/shutdown */
void animateInit(void);
void animateShutdown(void);

/*
 * Tcl command registration
 */
int Animate_Init(Tcl_Interp *interp);

/*
 * Utility functions
 */

/* HSV to RGB conversion (h in 0-1, s in 0-1, v in 0-1) */
void animateHSVtoRGB(float h, float s, float v, float *r, float *g, float *b);

/* Easing functions - input/output in 0-1 range */
float animateEaseLinear(float t);
float animateEaseInQuad(float t);
float animateEaseOutQuad(float t);
float animateEaseInOutQuad(float t);
float animateEaseInSine(float t);
float animateEaseOutSine(float t);
float animateEaseInOutSine(float t);

/* Oscillators - return value in min-max range */
float animateOscillate(float t, float freq, float min_val, float max_val);
float animatePulse(float t, float freq, float duty, float min_val, float max_val);

#ifdef __cplusplus
}
#endif

#endif /* ANIMATE_H */
