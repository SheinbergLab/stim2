/*
 * animate.cpp
 * Animation module for stim2
 *
 * Provides declarative animation primitives with minimal overhead.
 * If no animations are attached to an object, animateUpdateObj() returns
 * immediately with just a NULL pointer check.
 */

#ifdef WIN32
#include <windows.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <tcl.h>
#include "animate.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* We use a dedicated anim_state field in GR_OBJ to store animation state */
#define GR_ANIM_STATE(o) ((AnimState *)((o)->anim_state))
#define GR_SET_ANIM_STATE(o, s) ((o)->anim_state = (void *)(s))

/*
 * Forward declarations for Tcl commands
 */
static int animateRotationCmd(ClientData, Tcl_Interp *, int, const char **);
static int animateOpacityCmd(ClientData, Tcl_Interp *, int, const char **);
static int animateScaleCmd(ClientData, Tcl_Interp *, int, const char **);
static int animatePositionCmd(ClientData, Tcl_Interp *, int, const char **);
static int animateColorCmd(ClientData, Tcl_Interp *, int, const char **);
static int animateSequenceCmd(ClientData, Tcl_Interp *, int, const char **);
static int animateCustomCmd(ClientData, Tcl_Interp *, int, const char **);
static int animatePauseCmd(ClientData, Tcl_Interp *, int, const char **);
static int animateResumeCmd(ClientData, Tcl_Interp *, int, const char **);
static int animateResetCmd(ClientData, Tcl_Interp *, int, const char **);
static int animateClearCmd(ClientData, Tcl_Interp *, int, const char **);
static int animateInfoCmd(ClientData, Tcl_Interp *, int, const char **);

/* Utility Tcl commands */
static int animateOscillateCmd(ClientData, Tcl_Interp *, int, const char **);
static int animateHSVCmd(ClientData, Tcl_Interp *, int, const char **);
static int animateEaseCmd(ClientData, Tcl_Interp *, int, const char **);

/*
 * Global Tcl interpreter reference (set during init)
 */
static Tcl_Interp *AnimInterp = NULL;

/********************************************************************
 *                    Utility Functions
 ********************************************************************/

void animateHSVtoRGB(float h, float s, float v, float *r, float *g, float *b)
{
    if (s <= 0.0f) {
        *r = *g = *b = v;
        return;
    }
    
    float hh = fmodf(h, 1.0f) * 6.0f;
    int i = (int)hh;
    float f = hh - i;
    float p = v * (1.0f - s);
    float q = v * (1.0f - s * f);
    float t = v * (1.0f - s * (1.0f - f));
    
    switch (i) {
        case 0:  *r = v; *g = t; *b = p; break;
        case 1:  *r = q; *g = v; *b = p; break;
        case 2:  *r = p; *g = v; *b = t; break;
        case 3:  *r = p; *g = q; *b = v; break;
        case 4:  *r = t; *g = p; *b = v; break;
        default: *r = v; *g = p; *b = q; break;
    }
}

float animateEaseLinear(float t) { return t; }
float animateEaseInQuad(float t) { return t * t; }
float animateEaseOutQuad(float t) { return t * (2.0f - t); }
float animateEaseInOutQuad(float t) {
    return t < 0.5f ? 2.0f * t * t : -1.0f + (4.0f - 2.0f * t) * t;
}
float animateEaseInSine(float t) { return 1.0f - cosf(t * M_PI * 0.5f); }
float animateEaseOutSine(float t) { return sinf(t * M_PI * 0.5f); }
float animateEaseInOutSine(float t) { return 0.5f * (1.0f - cosf(M_PI * t)); }

float animateOscillate(float t, float freq, float min_val, float max_val)
{
    float phase = t * freq * 2.0f * M_PI;
    float normalized = 0.5f + 0.5f * sinf(phase);  /* 0 to 1 */
    return min_val + normalized * (max_val - min_val);
}

float animatePulse(float t, float freq, float duty, float min_val, float max_val)
{
    float period = 1.0f / freq;
    float phase = fmodf(t, period) / period;
    return (phase < duty) ? max_val : min_val;
}

/********************************************************************
 *                    Animation State Management
 ********************************************************************/

static AnimState *createAnimState(GR_OBJ *obj)
{
    AnimState *state = (AnimState *)calloc(1, sizeof(AnimState));
    if (!state) return NULL;
    
    state->obj = obj;
    state->properties = NULL;
    state->start_time = StimTicks;  /* Use Ticks (never resets) for stable timing */
    state->frame_count = 0;
    
    GR_SET_ANIM_STATE(obj, state);
    return state;
}

static AnimState *getOrCreateAnimState(GR_OBJ *obj)
{
    AnimState *state = GR_ANIM_STATE(obj);
    if (!state) {
        state = createAnimState(obj);
    }
    return state;
}

static AnimProperty *createAnimProperty(AnimType type)
{
    AnimProperty *prop = (AnimProperty *)calloc(1, sizeof(AnimProperty));
    if (!prop) return NULL;
    
    prop->type = type;
    prop->active = 1;
    prop->perframe = 0;
    prop->min_val = 0.0f;
    prop->max_val = 1.0f;
    prop->next = NULL;
    
    return prop;
}

static void freeAnimProperty(AnimProperty *prop)
{
    if (!prop) return;
    if (prop->sequence) free(prop->sequence);
    if (prop->script) free(prop->script);
    if (prop->proc_name) free(prop->proc_name);
    if (prop->params) free(prop->params);
    free(prop);
}

static AnimProperty *findAnimProperty(AnimState *state, AnimType type)
{
    if (!state) return NULL;
    
    AnimProperty *p = state->properties;
    while (p) {
        if (p->type == type) return p;
        p = p->next;
    }
    return NULL;
}

static AnimProperty *addAnimProperty(AnimState *state, AnimType type)
{
    /* Replace existing property of same type */
    AnimProperty *existing = findAnimProperty(state, type);
    if (existing) {
        /* Reset it */
        if (existing->sequence) { free(existing->sequence); existing->sequence = NULL; }
        if (existing->script) { free(existing->script); existing->script = NULL; }
        existing->active = 1;
        existing->phase = 0;
        existing->seq_index = 0;
        return existing;
    }
    
    AnimProperty *prop = createAnimProperty(type);
    if (!prop) return NULL;
    
    /* Add to front of list */
    prop->next = state->properties;
    state->properties = prop;
    
    return prop;
}

/* Get existing property or create new one (doesn't reset existing) */
static AnimProperty *getOrAddAnimProperty(AnimState *state, AnimType type, int *is_new)
{
    AnimProperty *existing = findAnimProperty(state, type);
    if (existing) {
        if (is_new) *is_new = 0;
        existing->active = 1;  /* reactivate if paused */
        return existing;
    }
    
    AnimProperty *prop = createAnimProperty(type);
    if (!prop) return NULL;
    
    /* Add to front of list */
    prop->next = state->properties;
    state->properties = prop;
    
    if (is_new) *is_new = 1;
    return prop;
}

static void removeAnimProperty(AnimState *state, AnimType type)
{
    if (!state) return;
    
    AnimProperty **pp = &state->properties;
    while (*pp) {
        if ((*pp)->type == type) {
            AnimProperty *to_free = *pp;
            *pp = (*pp)->next;
            freeAnimProperty(to_free);
            return;
        }
        pp = &(*pp)->next;
    }
}

/********************************************************************
 *                    Core Update Function
 ********************************************************************/

void animateUpdateObj(GR_OBJ *obj, unsigned int ticks_ms, unsigned int dt_ms)
{
    if (!obj) return;
    
    AnimState *state = GR_ANIM_STATE(obj);
    if (!state) return;  /* No animations - fast exit */
    
    /* Use ticks (never resets) for stable animation timing */
    float t = (ticks_ms - state->start_time) / 1000.0f;  /* seconds since start */
    float dt = dt_ms / 1000.0f;
    unsigned int frame = state->frame_count;
    
    AnimProperty *prop = state->properties;
    while (prop) {
        if (!prop->active) {
            prop = prop->next;
            continue;
        }
        
        switch (prop->type) {
        case ANIM_ROTATION:
            {
                float angle;
                if (prop->freq > 0) {
                    /* Oscillating rotation */
                    angle = prop->amplitude * sinf(t * prop->freq * 2.0f * M_PI + prop->phase);
                } else {
                    /* Continuous rotation */
                    float rate = prop->perframe ? prop->speed * frame : prop->speed * t;
                    angle = fmodf(rate + prop->phase, 360.0f);
                }
                /* Use axis stored in property (default z-axis for 2D) */
                gobjRotateObj(obj, angle, prop->vx, prop->vy, prop->vz);
            }
            break;
            
        case ANIM_OPACITY:
            {
                float opacity;
                if (prop->freq > 0) {
                    /* Pulsing opacity */
                    opacity = animateOscillate(t, prop->freq, prop->min_val, prop->max_val);
                } else if (prop->speed > 0) {
                    /* Fade in/out */
                    float progress = t / prop->speed;
                    if (progress > 1.0f) progress = 1.0f;
                    opacity = prop->min_val + progress * (prop->max_val - prop->min_val);
                } else {
                    opacity = prop->max_val;
                }
                /* Need module-specific opacity setter - use property or callback */
                /* For now, store in a way the module can query, or use Tcl */
                if (AnimInterp) {
                    char cmd[128];
                    snprintf(cmd, sizeof(cmd), "catch {svgOpacity %d %.4f}", 
                             (int)(obj - OBJList->objects[0]), opacity);
                    /* This is a hack - better to have generic opacity support */
                }
            }
            break;
            
        case ANIM_SCALE:
            {
                float scale;
                if (prop->freq > 0) {
                    scale = animateOscillate(t, prop->freq, prop->min_val, prop->max_val);
                } else {
                    scale = prop->max_val;
                }
                gobjScaleObj(obj, scale, scale, scale);
            }
            break;
            
        case ANIM_POSITION:
            {
                if (prop->speed != 0 && prop->amplitude != 0) {
                    /* Orbital motion */
                    float angle_rad = (prop->perframe ? prop->speed * frame : prop->speed * t) 
                                      * M_PI / 180.0f;
                    float x = prop->amplitude * cosf(angle_rad);
                    float y = prop->amplitude * sinf(angle_rad);
                    gobjTranslateObj(obj, x, y, GR_TZ(obj));
                } else if (prop->vx != 0 || prop->vy != 0 || prop->vz != 0) {
                    /* Linear velocity */
                    float dx = prop->perframe ? prop->vx : prop->vx * dt;
                    float dy = prop->perframe ? prop->vy : prop->vy * dt;
                    float dz = prop->perframe ? prop->vz : prop->vz * dt;
                    float x = GR_TX(obj) + dx;
                    float y = GR_TY(obj) + dy;
                    float z = GR_TZ(obj) + dz;
                    gobjTranslateObj(obj, x, y, z);
                }
            }
            break;
            
        case ANIM_COLOR:
            {
                if (prop->freq > 0) {
                    float hue = fmodf(t * prop->freq, 1.0f);
                    float r, g, b;
                    animateHSVtoRGB(hue, 1.0f, 1.0f, &r, &g, &b);
                    /* Need module-specific color setter */
                    if (AnimInterp) {
                        char cmd[128];
                        snprintf(cmd, sizeof(cmd), 
                                 "catch {svgColor %d %d %.4f %.4f %.4f 1.0}",
                                 (int)(obj - OBJList->objects[0]), prop->color_mode, r, g, b);
                        Tcl_Eval(AnimInterp, cmd);
                    }
                }
            }
            break;
            
        case ANIM_BLINK:
            {
                /* 
                 * Blink uses: freq (rate in Hz), min_val (duty cycle 0-1)
                 * For frame-based: freq is frames per cycle
                 */
                float period = prop->perframe ? prop->freq : (1.0f / prop->freq);
                float duty = prop->min_val;  /* duty cycle: fraction of time "on" */
                float phase_in_cycle;
                
                if (prop->perframe) {
                    phase_in_cycle = fmodf((float)frame, period) / period;
                } else {
                    phase_in_cycle = fmodf(t, period) / period;
                }
                
                int visible = (phase_in_cycle < duty) ? 1 : 0;
                GR_VISIBLE(obj) = visible;
            }
            break;
            
        case ANIM_SEQUENCE:
            {
                if (prop->sequence && prop->seq_length > 0) {
                    int idx = prop->perframe ? (frame % prop->seq_length) 
                                             : ((int)(t * prop->freq) % prop->seq_length);
                    if (!prop->seq_loop && idx >= prop->seq_length) {
                        idx = prop->seq_length - 1;
                        prop->active = 0;
                    }
                    /* Apply sequence value - need to know what property */
                    /* For now, assume opacity */
                    float val = prop->sequence[idx];
                    if (AnimInterp) {
                        char cmd[128];
                        snprintf(cmd, sizeof(cmd), "catch {svgOpacity %d %.4f}",
                                 (int)(obj - OBJList->objects[0]), val);
                        Tcl_Eval(AnimInterp, cmd);
                    }
                }
            }
            break;
            
        case ANIM_CUSTOM:
            {
                if (!AnimInterp || !prop->proc_name) break;
                
                /* Build command: procname t dt frame objname ?param values...? */
                Tcl_DString cmd;
                Tcl_DStringInit(&cmd);
                Tcl_DStringAppend(&cmd, prop->proc_name, -1);
                
                /* Append standard args */
                char buf[64];
                snprintf(buf, sizeof(buf), " %.6f %.6f %u ", t, dt, frame);
                Tcl_DStringAppend(&cmd, buf, -1);
                Tcl_DStringAppend(&cmd, GR_NAME(obj), -1);
                
                /* Append param values from dict */
                if (prop->params) {
                    Tcl_Obj *dictObj = Tcl_NewStringObj(prop->params, -1);
                    Tcl_IncrRefCount(dictObj);
                    
                    Tcl_DictSearch search;
                    Tcl_Obj *key, *value;
                    int done;
                    
                    if (Tcl_DictObjFirst(AnimInterp, dictObj, &search, 
                                         &key, &value, &done) == TCL_OK) {
                        while (!done) {
                            Tcl_DStringAppend(&cmd, " ", 1);
                            Tcl_DStringAppend(&cmd, Tcl_GetString(value), -1);
                            Tcl_DictObjNext(&search, &key, &value, &done);
                        }
                        Tcl_DictObjDone(&search);
                    }
                    Tcl_DecrRefCount(dictObj);
                }
                
                Tcl_Eval(AnimInterp, Tcl_DStringValue(&cmd));
                Tcl_DStringFree(&cmd);
            }
            break;
            
        default:
            break;
        }
        
        prop = prop->next;
    }
    
    /* Increment frame count for next call */
    state->frame_count++;
}

void animateClearObj(GR_OBJ *obj)
{
    if (!obj) return;
    
    AnimState *state = GR_ANIM_STATE(obj);
    if (!state) return;
    
    /* Free all properties */
    AnimProperty *p = state->properties;
    while (p) {
        AnimProperty *next = p->next;
        freeAnimProperty(p);
        p = next;
    }
    
    free(state);
    GR_SET_ANIM_STATE(obj, NULL);
}

void animateInit(void)
{
    /* Nothing to do currently */
}

void animateShutdown(void)
{
    /* Could iterate all objects and clear animations */
}

/********************************************************************
 *                    Tcl Commands
 ********************************************************************/

static GR_OBJ *getObjFromArg(Tcl_Interp *interp, const char *arg)
{
    OBJ_LIST *olist = getOBJList();
    int id;
    
    if (gobjFindObj(olist, (char *)arg, &id)) {
        return OL_OBJ(olist, id);
    }
    
    Tcl_SetResult(interp, (char *)"invalid object", TCL_STATIC);
    return NULL;
}

/*
 * Helper to build result dict for rotation animation
 */
static void rotationToResult(Tcl_Interp *interp, AnimProperty *prop)
{
    Tcl_Obj *dict = Tcl_NewDictObj();
    Tcl_DictObjPut(interp, dict, Tcl_NewStringObj("type", -1), 
                   Tcl_NewStringObj("rotation", -1));
    Tcl_DictObjPut(interp, dict, Tcl_NewStringObj("speed", -1), 
                   Tcl_NewDoubleObj(prop->speed));
    Tcl_DictObjPut(interp, dict, Tcl_NewStringObj("freq", -1), 
                   Tcl_NewDoubleObj(prop->freq));
    Tcl_DictObjPut(interp, dict, Tcl_NewStringObj("amplitude", -1), 
                   Tcl_NewDoubleObj(prop->amplitude));
    Tcl_DictObjPut(interp, dict, Tcl_NewStringObj("phase", -1), 
                   Tcl_NewDoubleObj(prop->phase * 180.0 / M_PI));  /* back to degrees */
    
    Tcl_Obj *axis = Tcl_NewListObj(0, NULL);
    Tcl_ListObjAppendElement(interp, axis, Tcl_NewDoubleObj(prop->vx));
    Tcl_ListObjAppendElement(interp, axis, Tcl_NewDoubleObj(prop->vy));
    Tcl_ListObjAppendElement(interp, axis, Tcl_NewDoubleObj(prop->vz));
    Tcl_DictObjPut(interp, dict, Tcl_NewStringObj("axis", -1), axis);
    
    Tcl_DictObjPut(interp, dict, Tcl_NewStringObj("perframe", -1), 
                   Tcl_NewIntObj(prop->perframe));
    Tcl_DictObjPut(interp, dict, Tcl_NewStringObj("active", -1), 
                   Tcl_NewIntObj(prop->active));
    Tcl_SetObjResult(interp, dict);
}

/*
 * animateRotation obj ?-speed deg/sec? ?-oscillate amp? ?-freq hz? ?-phase deg? ?-axis {x y z}? ?-perframe?
 * 
 * With options: sets/updates animation parameters (preserves unspecified values)
 * Without options: returns current state (getter)
 * Always returns current state dict
 */
static int animateRotationCmd(ClientData clientData, Tcl_Interp *interp,
                              int argc, const char **argv)
{
    if (argc < 2) {
        Tcl_SetResult(interp, (char *)"usage: animateRotation obj ?options?", TCL_STATIC);
        return TCL_ERROR;
    }
    
    GR_OBJ *obj = getObjFromArg(interp, argv[1]);
    if (!obj) return TCL_ERROR;
    
    AnimState *state = GR_ANIM_STATE(obj);
    AnimProperty *prop = state ? findAnimProperty(state, ANIM_ROTATION) : NULL;
    
    /* Getter mode: no options, just return current state */
    if (argc == 2) {
        if (!prop) {
            /* No rotation animation - return empty dict */
            Tcl_SetObjResult(interp, Tcl_NewDictObj());
            return TCL_OK;
        }
        rotationToResult(interp, prop);
        return TCL_OK;
    }
    
    /* Setter mode: create if needed, update specified values only */
    state = getOrCreateAnimState(obj);
    
    int is_new = 0;
    prop = getOrAddAnimProperty(state, ANIM_ROTATION, &is_new);
    
    if (is_new) {
        /* Set defaults only for new animation */
        prop->speed = 45.0f;
        prop->freq = 0;
        prop->amplitude = 0;
        prop->phase = 0;
        prop->perframe = 0;
        prop->vx = 0.0f;
        prop->vy = 0.0f;
        prop->vz = 1.0f;
    }
    
    /* Parse options - only update specified values */
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-speed") == 0 && i+1 < argc) {
            prop->speed = atof(argv[++i]);
        } else if (strcmp(argv[i], "-oscillate") == 0 && i+1 < argc) {
            prop->amplitude = atof(argv[++i]);
            if (prop->freq <= 0) prop->freq = 1.0f;
        } else if (strcmp(argv[i], "-freq") == 0 && i+1 < argc) {
            prop->freq = atof(argv[++i]);
        } else if (strcmp(argv[i], "-phase") == 0 && i+1 < argc) {
            prop->phase = atof(argv[++i]) * M_PI / 180.0f;
        } else if (strcmp(argv[i], "-axis") == 0 && i+1 < argc) {
            Tcl_Size listc;
            const char **listv;
            if (Tcl_SplitList(interp, argv[++i], &listc, &listv) == TCL_OK) {
                if (listc >= 3) {
                    prop->vx = atof(listv[0]);
                    prop->vy = atof(listv[1]);
                    prop->vz = atof(listv[2]);
                }
                Tcl_Free((char *)listv);
            }
        } else if (strcmp(argv[i], "-perframe") == 0) {
            prop->perframe = 1;
        }
    }
    
    /* Return current state */
    rotationToResult(interp, prop);
    return TCL_OK;
}

/*
 * animateOpacity obj ?-pulse freq? ?-min val? ?-max val? ?-fade duration?
 */
static int animateOpacityCmd(ClientData clientData, Tcl_Interp *interp,
                             int argc, const char **argv)
{
    if (argc < 2) {
        Tcl_SetResult(interp, (char *)"usage: animateOpacity obj ?options?", TCL_STATIC);
        return TCL_ERROR;
    }
    
    GR_OBJ *obj = getObjFromArg(interp, argv[1]);
    if (!obj) return TCL_ERROR;
    
    AnimState *state = getOrCreateAnimState(obj);
    AnimProperty *prop = addAnimProperty(state, ANIM_OPACITY);
    
    /* Defaults */
    prop->freq = 0;
    prop->speed = 0;
    prop->min_val = 0.0f;
    prop->max_val = 1.0f;
    
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-pulse") == 0 && i+1 < argc) {
            prop->freq = atof(argv[++i]);
        } else if (strcmp(argv[i], "-min") == 0 && i+1 < argc) {
            prop->min_val = atof(argv[++i]);
        } else if (strcmp(argv[i], "-max") == 0 && i+1 < argc) {
            prop->max_val = atof(argv[++i]);
        } else if (strcmp(argv[i], "-fade") == 0 && i+1 < argc) {
            prop->speed = atof(argv[++i]);  /* duration in seconds */
        }
    }
    
    return TCL_OK;
}

/*
 * Helper to build result dict for scale animation
 */
static void scaleToResult(Tcl_Interp *interp, AnimProperty *prop)
{
    Tcl_Obj *dict = Tcl_NewDictObj();
    Tcl_DictObjPut(interp, dict, Tcl_NewStringObj("type", -1), 
                   Tcl_NewStringObj("scale", -1));
    Tcl_DictObjPut(interp, dict, Tcl_NewStringObj("freq", -1), 
                   Tcl_NewDoubleObj(prop->freq));
    Tcl_DictObjPut(interp, dict, Tcl_NewStringObj("min", -1), 
                   Tcl_NewDoubleObj(prop->min_val));
    Tcl_DictObjPut(interp, dict, Tcl_NewStringObj("max", -1), 
                   Tcl_NewDoubleObj(prop->max_val));
    Tcl_DictObjPut(interp, dict, Tcl_NewStringObj("perframe", -1), 
                   Tcl_NewIntObj(prop->perframe));
    Tcl_DictObjPut(interp, dict, Tcl_NewStringObj("active", -1), 
                   Tcl_NewIntObj(prop->active));
    Tcl_SetObjResult(interp, dict);
}

/*
 * animateScale obj ?-pulse freq? ?-min val? ?-max val? ?-perframe?
 */
static int animateScaleCmd(ClientData clientData, Tcl_Interp *interp,
                           int argc, const char **argv)
{
    if (argc < 2) {
        Tcl_SetResult(interp, (char *)"usage: animateScale obj ?options?", TCL_STATIC);
        return TCL_ERROR;
    }
    
    GR_OBJ *obj = getObjFromArg(interp, argv[1]);
    if (!obj) return TCL_ERROR;
    
    AnimState *state = GR_ANIM_STATE(obj);
    AnimProperty *prop = state ? findAnimProperty(state, ANIM_SCALE) : NULL;
    
    /* Getter mode */
    if (argc == 2) {
        if (!prop) {
            Tcl_SetObjResult(interp, Tcl_NewDictObj());
            return TCL_OK;
        }
        scaleToResult(interp, prop);
        return TCL_OK;
    }
    
    /* Setter mode */
    state = getOrCreateAnimState(obj);
    
    int is_new = 0;
    prop = getOrAddAnimProperty(state, ANIM_SCALE, &is_new);
    
    if (is_new) {
        /* Defaults only for new animation */
        prop->freq = 1.0f;
        prop->min_val = 0.5f;
        prop->max_val = 1.5f;
        prop->perframe = 0;
    }
    
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-pulse") == 0 && i+1 < argc) {
            prop->freq = atof(argv[++i]);
        } else if (strcmp(argv[i], "-min") == 0 && i+1 < argc) {
            prop->min_val = atof(argv[++i]);
        } else if (strcmp(argv[i], "-max") == 0 && i+1 < argc) {
            prop->max_val = atof(argv[++i]);
        } else if (strcmp(argv[i], "-perframe") == 0) {
            prop->perframe = 1;
        }
    }
    
    scaleToResult(interp, prop);
    return TCL_OK;
}

/*
 * Helper to build result dict for position animation
 */
static void positionToResult(Tcl_Interp *interp, AnimProperty *prop)
{
    Tcl_Obj *dict = Tcl_NewDictObj();
    Tcl_DictObjPut(interp, dict, Tcl_NewStringObj("type", -1), 
                   Tcl_NewStringObj("position", -1));
    
    Tcl_Obj *vel = Tcl_NewListObj(0, NULL);
    Tcl_ListObjAppendElement(interp, vel, Tcl_NewDoubleObj(prop->vx));
    Tcl_ListObjAppendElement(interp, vel, Tcl_NewDoubleObj(prop->vy));
    Tcl_ListObjAppendElement(interp, vel, Tcl_NewDoubleObj(prop->vz));
    Tcl_DictObjPut(interp, dict, Tcl_NewStringObj("velocity", -1), vel);
    
    Tcl_DictObjPut(interp, dict, Tcl_NewStringObj("orbit", -1), 
                   Tcl_NewDoubleObj(prop->speed));
    Tcl_DictObjPut(interp, dict, Tcl_NewStringObj("radius", -1), 
                   Tcl_NewDoubleObj(prop->amplitude));
    Tcl_DictObjPut(interp, dict, Tcl_NewStringObj("perframe", -1), 
                   Tcl_NewIntObj(prop->perframe));
    Tcl_DictObjPut(interp, dict, Tcl_NewStringObj("active", -1), 
                   Tcl_NewIntObj(prop->active));
    Tcl_SetObjResult(interp, dict);
}

/*
 * animatePosition obj ?-velocity {vx vy}? ?-orbit speed? ?-radius r? ?-perframe?
 */
static int animatePositionCmd(ClientData clientData, Tcl_Interp *interp,
                              int argc, const char **argv)
{
    if (argc < 2) {
        Tcl_SetResult(interp, (char *)"usage: animatePosition obj ?options?", TCL_STATIC);
        return TCL_ERROR;
    }
    
    GR_OBJ *obj = getObjFromArg(interp, argv[1]);
    if (!obj) return TCL_ERROR;
    
    AnimState *state = GR_ANIM_STATE(obj);
    AnimProperty *prop = state ? findAnimProperty(state, ANIM_POSITION) : NULL;
    
    /* Getter mode */
    if (argc == 2) {
        if (!prop) {
            Tcl_SetObjResult(interp, Tcl_NewDictObj());
            return TCL_OK;
        }
        positionToResult(interp, prop);
        return TCL_OK;
    }
    
    /* Setter mode */
    state = getOrCreateAnimState(obj);
    
    int is_new = 0;
    prop = getOrAddAnimProperty(state, ANIM_POSITION, &is_new);
    
    if (is_new) {
        /* Defaults only for new animation */
        prop->vx = 0;
        prop->vy = 0;
        prop->vz = 0;
        prop->speed = 0;       /* orbit speed (deg/sec) */
        prop->amplitude = 0;   /* orbit radius */
        prop->perframe = 0;
    }
    
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-velocity") == 0 && i+1 < argc) {
            /* Parse {vx vy} or {vx vy vz} list */
            Tcl_Size listc;
            const char **listv;
            if (Tcl_SplitList(interp, argv[++i], &listc, &listv) == TCL_OK) {
                if (listc >= 2) {
                    prop->vx = atof(listv[0]);
                    prop->vy = atof(listv[1]);
                    if (listc >= 3) prop->vz = atof(listv[2]);
                }
                Tcl_Free((char *)listv);
            }
        } else if (strcmp(argv[i], "-orbit") == 0 && i+1 < argc) {
            prop->speed = atof(argv[++i]);  /* deg/sec */
        } else if (strcmp(argv[i], "-radius") == 0 && i+1 < argc) {
            prop->amplitude = atof(argv[++i]);
        } else if (strcmp(argv[i], "-perframe") == 0) {
            prop->perframe = 1;
        }
    }
    
    positionToResult(interp, prop);
    return TCL_OK;
}

/*
 * animateColor obj ?-cycle freq? ?-mode 0|1|2?
 */
static int animateColorCmd(ClientData clientData, Tcl_Interp *interp,
                           int argc, const char **argv)
{
    if (argc < 2) {
        Tcl_SetResult(interp, (char *)"usage: animateColor obj ?options?", TCL_STATIC);
        return TCL_ERROR;
    }
    
    GR_OBJ *obj = getObjFromArg(interp, argv[1]);
    if (!obj) return TCL_ERROR;
    
    AnimState *state = getOrCreateAnimState(obj);
    AnimProperty *prop = addAnimProperty(state, ANIM_COLOR);
    
    prop->freq = 0.5f;
    prop->color_mode = 2;  /* multiply by default */
    
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-cycle") == 0 && i+1 < argc) {
            prop->freq = atof(argv[++i]);
        } else if (strcmp(argv[i], "-mode") == 0 && i+1 < argc) {
            prop->color_mode = atoi(argv[++i]);
        }
    }
    
    return TCL_OK;
}

/*
 * Helper to build result dict for blink animation
 */
static void blinkToResult(Tcl_Interp *interp, AnimProperty *prop)
{
    Tcl_Obj *dict = Tcl_NewDictObj();
    Tcl_DictObjPut(interp, dict, Tcl_NewStringObj("type", -1), 
                   Tcl_NewStringObj("blink", -1));
    Tcl_DictObjPut(interp, dict, Tcl_NewStringObj("rate", -1), 
                   Tcl_NewDoubleObj(prop->freq));
    Tcl_DictObjPut(interp, dict, Tcl_NewStringObj("duty", -1), 
                   Tcl_NewDoubleObj(prop->min_val));
    Tcl_DictObjPut(interp, dict, Tcl_NewStringObj("perframe", -1), 
                   Tcl_NewIntObj(prop->perframe));
    Tcl_DictObjPut(interp, dict, Tcl_NewStringObj("active", -1), 
                   Tcl_NewIntObj(prop->active));
    Tcl_SetObjResult(interp, dict);
}

/*
 * animateBlink obj ?-rate hz? ?-duty 0.0-1.0? ?-perframe?
 *
 * Blinks object visibility on/off at specified rate.
 * -rate: blinks per second (Hz), default 1.0
 * -duty: fraction of cycle spent visible (0.0-1.0), default 0.5
 * -perframe: rate is frames per cycle instead of Hz
 */
static int animateBlinkCmd(ClientData clientData, Tcl_Interp *interp,
                           int argc, const char **argv)
{
    if (argc < 2) {
        Tcl_SetResult(interp, (char *)"usage: animateBlink obj ?-rate hz? ?-duty val? ?-perframe?", TCL_STATIC);
        return TCL_ERROR;
    }
    
    GR_OBJ *obj = getObjFromArg(interp, argv[1]);
    if (!obj) return TCL_ERROR;
    
    AnimState *state = GR_ANIM_STATE(obj);
    AnimProperty *prop = state ? findAnimProperty(state, ANIM_BLINK) : NULL;
    
    /* Getter mode */
    if (argc == 2) {
        if (!prop) {
            Tcl_SetObjResult(interp, Tcl_NewDictObj());
            return TCL_OK;
        }
        blinkToResult(interp, prop);
        return TCL_OK;
    }
    
    /* Setter mode */
    state = getOrCreateAnimState(obj);
    
    int is_new = 0;
    prop = getOrAddAnimProperty(state, ANIM_BLINK, &is_new);
    
    if (is_new) {
        /* Defaults */
        prop->freq = 1.0f;      /* 1 Hz */
        prop->min_val = 0.5f;   /* 50% duty cycle */
        prop->perframe = 0;
    }
    
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-rate") == 0 && i+1 < argc) {
            prop->freq = atof(argv[++i]);
        } else if (strcmp(argv[i], "-duty") == 0 && i+1 < argc) {
            prop->min_val = atof(argv[++i]);
            if (prop->min_val < 0.0f) prop->min_val = 0.0f;
            if (prop->min_val > 1.0f) prop->min_val = 1.0f;
        } else if (strcmp(argv[i], "-perframe") == 0) {
            prop->perframe = 1;
        }
    }
    
    blinkToResult(interp, prop);
    return TCL_OK;
}

/*
 * Helper to build result dict for custom animation
 */
static void customToResult(Tcl_Interp *interp, AnimProperty *prop)
{
    Tcl_Obj *dict = Tcl_NewDictObj();
    Tcl_DictObjPut(interp, dict, Tcl_NewStringObj("type", -1), 
                   Tcl_NewStringObj("custom", -1));
    Tcl_DictObjPut(interp, dict, Tcl_NewStringObj("proc", -1), 
                   Tcl_NewStringObj(prop->proc_name ? prop->proc_name : "", -1));
    
    /* Parse params string back to dict */
    if (prop->params) {
        Tcl_Obj *paramsObj = Tcl_NewStringObj(prop->params, -1);
        Tcl_DictObjPut(interp, dict, Tcl_NewStringObj("params", -1), paramsObj);
    } else {
        Tcl_DictObjPut(interp, dict, Tcl_NewStringObj("params", -1), 
                       Tcl_NewDictObj());
    }
    
    Tcl_DictObjPut(interp, dict, Tcl_NewStringObj("active", -1), 
                   Tcl_NewIntObj(prop->active));
    Tcl_SetObjResult(interp, dict);
}

/*
 * animateCustom obj ?-proc procname? ?-params {key val ...}?
 *
 * Proc signature: procname t dt frame obj ?param1 param2 ...?
 * System args (t dt frame obj) are passed first, then param values in dict order.
 *
 * With options: sets/updates animation
 * Without options: returns current state
 */
static int animateCustomCmd(ClientData clientData, Tcl_Interp *interp,
                            int argc, const char **argv)
{
    if (argc < 2) {
        Tcl_SetResult(interp, (char *)"usage: animateCustom obj ?-proc name? ?-params dict?", TCL_STATIC);
        return TCL_ERROR;
    }
    
    GR_OBJ *obj = getObjFromArg(interp, argv[1]);
    if (!obj) return TCL_ERROR;
    
    AnimState *state = GR_ANIM_STATE(obj);
    AnimProperty *prop = state ? findAnimProperty(state, ANIM_CUSTOM) : NULL;
    
    /* Getter mode */
    if (argc == 2) {
        if (!prop) {
            Tcl_SetObjResult(interp, Tcl_NewDictObj());
            return TCL_OK;
        }
        customToResult(interp, prop);
        return TCL_OK;
    }
    
    /* Setter mode */
    state = getOrCreateAnimState(obj);
    
    int is_new = 0;
    prop = getOrAddAnimProperty(state, ANIM_CUSTOM, &is_new);
    
    /* Parse options */
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-proc") == 0 && i+1 < argc) {
            if (prop->proc_name) free(prop->proc_name);
            prop->proc_name = strdup(argv[++i]);
        } else if (strcmp(argv[i], "-params") == 0 && i+1 < argc) {
            if (prop->params) free(prop->params);
            prop->params = strdup(argv[++i]);
        }
    }
    
    customToResult(interp, prop);
    return TCL_OK;
}

/*
 * animateClear obj ?property?
 */
static int animateClearCmd(ClientData clientData, Tcl_Interp *interp,
                           int argc, const char **argv)
{
    if (argc < 2) {
        Tcl_SetResult(interp, (char *)"usage: animateClear obj ?property?", TCL_STATIC);
        return TCL_ERROR;
    }
    
    GR_OBJ *obj = getObjFromArg(interp, argv[1]);
    if (!obj) return TCL_ERROR;
    
    if (argc > 2) {
        /* Clear specific property */
        AnimState *state = GR_ANIM_STATE(obj);
        if (state) {
            const char *prop_name = argv[2];
            AnimType type = ANIM_NONE;
            if (strcmp(prop_name, "rotation") == 0) type = ANIM_ROTATION;
            else if (strcmp(prop_name, "opacity") == 0) type = ANIM_OPACITY;
            else if (strcmp(prop_name, "scale") == 0) type = ANIM_SCALE;
            else if (strcmp(prop_name, "position") == 0) type = ANIM_POSITION;
            else if (strcmp(prop_name, "color") == 0) type = ANIM_COLOR;
            else if (strcmp(prop_name, "custom") == 0) type = ANIM_CUSTOM;
            
            if (type != ANIM_NONE) {
                removeAnimProperty(state, type);
            }
        }
    } else {
        /* Clear all */
        animateClearObj(obj);
    }
    
    return TCL_OK;
}

/*
 * animatePause obj ?property?
 */
static int animatePauseCmd(ClientData clientData, Tcl_Interp *interp,
                           int argc, const char **argv)
{
    if (argc < 2) {
        Tcl_SetResult(interp, (char *)"usage: animatePause obj ?property?", TCL_STATIC);
        return TCL_ERROR;
    }
    
    GR_OBJ *obj = getObjFromArg(interp, argv[1]);
    if (!obj) return TCL_ERROR;
    
    AnimState *state = GR_ANIM_STATE(obj);
    if (!state) return TCL_OK;
    
    AnimProperty *p = state->properties;
    while (p) {
        p->active = 0;
        p = p->next;
    }
    
    return TCL_OK;
}

/*
 * animateResume obj ?property?
 */
static int animateResumeCmd(ClientData clientData, Tcl_Interp *interp,
                            int argc, const char **argv)
{
    if (argc < 2) {
        Tcl_SetResult(interp, (char *)"usage: animateResume obj ?property?", TCL_STATIC);
        return TCL_ERROR;
    }
    
    GR_OBJ *obj = getObjFromArg(interp, argv[1]);
    if (!obj) return TCL_ERROR;
    
    AnimState *state = GR_ANIM_STATE(obj);
    if (!state) return TCL_OK;
    
    AnimProperty *p = state->properties;
    while (p) {
        p->active = 1;
        p = p->next;
    }
    
    return TCL_OK;
}

/*
 * animateReset obj - reset animation time and frame to zero
 */
static int animateResetCmd(ClientData clientData, Tcl_Interp *interp,
                           int argc, const char **argv)
{
    if (argc < 2) {
        Tcl_SetResult(interp, (char *)"usage: animateReset obj", TCL_STATIC);
        return TCL_ERROR;
    }
    
    GR_OBJ *obj = getObjFromArg(interp, argv[1]);
    if (!obj) return TCL_ERROR;
    
    AnimState *state = GR_ANIM_STATE(obj);
    if (!state) return TCL_OK;
    
    /* Reset timing - use StimTicks for stable reference */
    state->start_time = StimTicks;
    state->frame_count = 0;
    
    /* Reset phase on all properties */
    AnimProperty *p = state->properties;
    while (p) {
        p->phase = 0;
        p->seq_index = 0;
        p = p->next;
    }
    
    return TCL_OK;
}

/*
 * oscillate t freq min max -> value
 */
static int animateOscillateCmd(ClientData clientData, Tcl_Interp *interp,
                               int argc, const char **argv)
{
    if (argc != 5) {
        Tcl_SetResult(interp, (char *)"usage: oscillate t freq min max", TCL_STATIC);
        return TCL_ERROR;
    }
    
    float t = atof(argv[1]);
    float freq = atof(argv[2]);
    float min_val = atof(argv[3]);
    float max_val = atof(argv[4]);
    
    float result = animateOscillate(t, freq, min_val, max_val);
    
    Tcl_SetObjResult(interp, Tcl_NewDoubleObj(result));
    return TCL_OK;
}

/*
 * hsv2rgb h s v -> {r g b}
 */
static int animateHSVCmd(ClientData clientData, Tcl_Interp *interp,
                         int argc, const char **argv)
{
    if (argc != 4) {
        Tcl_SetResult(interp, (char *)"usage: hsv2rgb h s v", TCL_STATIC);
        return TCL_ERROR;
    }
    
    float h = atof(argv[1]);
    float s = atof(argv[2]);
    float v = atof(argv[3]);
    float r, g, b;
    
    animateHSVtoRGB(h, s, v, &r, &g, &b);
    
    Tcl_Obj *list = Tcl_NewListObj(0, NULL);
    Tcl_ListObjAppendElement(interp, list, Tcl_NewDoubleObj(r));
    Tcl_ListObjAppendElement(interp, list, Tcl_NewDoubleObj(g));
    Tcl_ListObjAppendElement(interp, list, Tcl_NewDoubleObj(b));
    Tcl_SetObjResult(interp, list);
    
    return TCL_OK;
}

/********************************************************************
 *                    Module Initialization
 ********************************************************************/

#ifdef WIN32
EXPORT(int, Animate_Init)(Tcl_Interp *interp)
#else
extern "C" int Animate_Init(Tcl_Interp *interp)
#endif
{
    OBJ_LIST *olist = getOBJList();
    
#ifdef USE_TCL_STUBS
    if (Tcl_InitStubs(interp, "8.5-", 0) == NULL) {
        return TCL_ERROR;
    }
#endif
    
    AnimInterp = interp;
    
    /* Universal animation commands (work on any GR_OBJ) */
    Tcl_CreateCommand(interp, "animateRotation", 
                      (Tcl_CmdProc *)animateRotationCmd, (ClientData)olist, NULL);
    Tcl_CreateCommand(interp, "animateScale",
                      (Tcl_CmdProc *)animateScaleCmd, (ClientData)olist, NULL);
    Tcl_CreateCommand(interp, "animatePosition",
                      (Tcl_CmdProc *)animatePositionCmd, (ClientData)olist, NULL);
    Tcl_CreateCommand(interp, "animateBlink",
                      (Tcl_CmdProc *)animateBlinkCmd, (ClientData)olist, NULL);
    
    /* Custom script-based animation (for module-specific properties) */
    Tcl_CreateCommand(interp, "animateCustom",
                      (Tcl_CmdProc *)animateCustomCmd, (ClientData)olist, NULL);
    
    /* Control commands */
    Tcl_CreateCommand(interp, "animatePause",
                      (Tcl_CmdProc *)animatePauseCmd, (ClientData)olist, NULL);
    Tcl_CreateCommand(interp, "animateResume",
                      (Tcl_CmdProc *)animateResumeCmd, (ClientData)olist, NULL);
    Tcl_CreateCommand(interp, "animateReset",
                      (Tcl_CmdProc *)animateResetCmd, (ClientData)olist, NULL);
    Tcl_CreateCommand(interp, "animateClear",
                      (Tcl_CmdProc *)animateClearCmd, (ClientData)olist, NULL);
    
    /* Utility commands - useful in custom scripts */
    Tcl_CreateCommand(interp, "oscillate",
                      (Tcl_CmdProc *)animateOscillateCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "hsv2rgb",
                      (Tcl_CmdProc *)animateHSVCmd, NULL, NULL);
    
    /* Link StimTime for Tcl access (may already be linked) */
    Tcl_LinkVar(interp, "StimTime", (char *)&StimTime, TCL_LINK_INT | TCL_LINK_READ_ONLY);
    
    animateInit();
    
    return TCL_OK;
}
