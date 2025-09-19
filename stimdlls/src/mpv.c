/*
 * mpv.c
 *  Use libmpv to render video animations using OpenGL
 */

#ifdef __linux__
#define _GNU_SOURCE
#endif

#ifdef WIN32
#include <windows.h>
#define __arm__
#endif

#include <assert.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

#ifndef WIN32
#include <dlfcn.h>
#endif

#include <mpv/client.h>
#include <mpv/render_gl.h>
#include <tcl.h>

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <stim2.h>
#include <prmutil.h>

typedef struct _mpv_video {
  mpv_handle *mpv;
  mpv_render_context *mpv_gl;
  int width;
  int height;
  unsigned int frame_count;
  int repeat_mode;
  int visible;
  int hidden;  // Continue processing but don't show
  int paused;
  int user_paused;
  int redraw;
  int start_frame;
  int cur_frame;
  int stop_frame;
  char *timer_script;
  
  // OpenGL resources  
  GLuint fbo;
  GLuint texture;
  GLuint vertex_buffer;
  GLuint vao;
  
  // Video properties
  double duration;
  double current_time;
  int eof_reached;
} MPV_VIDEO;

static int MpvID = -1;  /* unique mpv object id */
static GLuint MpvShaderProgram = 0;  /* shared shader program */
static GLint MpvUniformTexture = -1;
static GLint MpvUniformModelview = -1;
static GLint MpvUniformProjection = -1;

// OpenGL shader sources
#ifdef STIM2_USE_GLES
static const char* vertex_shader_source = 
"#version 300 es\n"
"precision mediump float;\n"
"layout (location = 0) in vec3 aPos;\n"
"layout (location = 1) in vec2 aTexCoord;\n"
"out vec2 TexCoord;\n"
"uniform mat4 projMat;\n"
"uniform mat4 modelviewMat;\n"
"void main() {\n"
"    gl_Position = projMat * modelviewMat * vec4(aPos, 1.0);\n"
"    TexCoord = aTexCoord;\n"
"}\n";

static const char* fragment_shader_source = 
"#version 300 es\n"
"precision mediump float;\n"
"out vec4 FragColor;\n"
"in vec2 TexCoord;\n"
"uniform sampler2D ourTexture;\n"
"void main() {\n"
"    FragColor = texture(ourTexture, TexCoord);\n"
"}\n";
#else
static const char* vertex_shader_source = 
"#version 330 core\n"
"layout (location = 0) in vec3 aPos;\n"
"layout (location = 1) in vec2 aTexCoord;\n"
"out vec2 TexCoord;\n"
"uniform mat4 projMat;\n"
"uniform mat4 modelviewMat;\n"
"void main() {\n"
"    gl_Position = projMat * modelviewMat * vec4(aPos, 1.0);\n"
"    TexCoord = aTexCoord;\n"
"}\n";

static const char* fragment_shader_source = 
"#version 330 core\n"
"out vec4 FragColor;\n"
"in vec2 TexCoord;\n"
"uniform sampler2D ourTexture;\n"
"void main() {\n"
"    FragColor = texture(ourTexture, TexCoord);\n"
"}\n";
#endif

// Quad vertices for rendering video (matching polygon's format - vec3 + vec2)
static float quad_vertices[] = {
    // positions (vec3)  // texture coords (vec2)
    -0.5f,  0.5f, 0.0f,  0.0f, 1.0f,
    -0.5f, -0.5f, 0.0f,  0.0f, 0.0f,
     0.5f, -0.5f, 0.0f,  1.0f, 0.0f,
    -0.5f,  0.5f, 0.0f,  0.0f, 1.0f,
     0.5f, -0.5f, 0.0f,  1.0f, 0.0f,
     0.5f,  0.5f, 0.0f,  1.0f, 1.0f
};

// Callback for mpv to request OpenGL context updates
static void on_mpv_render_update(void *cb_ctx) {
    MPV_VIDEO *v = (MPV_VIDEO *)cb_ctx;
    v->redraw = 1;
}

// Helper function to get OpenGL procedure address
static void *get_proc_address(void *ctx, const char *name) {
    // Just use dlsym since GLAD has already loaded the functions
    return dlsym(RTLD_DEFAULT, name);
}



typedef struct {
    GLint program;
    GLint active_texture;
    GLint texture_2d_bindings[8]; // For multiple texture units
    GLint vao;
    GLint array_buffer;
    GLint element_array_buffer;
    GLboolean blend_enabled;
    GLint blend_src_rgb, blend_dst_rgb;
    GLint blend_src_alpha, blend_dst_alpha;
    GLboolean depth_test;
    GLint depth_func;
    GLboolean cull_face;
    GLint front_face;
    GLint cull_face_mode;
    GLfloat clear_color[4];
    GLboolean scissor_test;
    GLint scissor_box[4];
    GLint viewport[4];
    GLint framebuffer;
    // Add more as needed
} gl_state_backup_t;

static void save_gl_state(gl_state_backup_t *state) {
  glGetIntegerv(GL_CURRENT_PROGRAM, &state->program);
  glGetIntegerv(GL_ACTIVE_TEXTURE, &state->active_texture);
  
  // Save texture bindings for multiple units
  for (int i = 0; i < 8; i++) {
    glActiveTexture(GL_TEXTURE0 + i);
    glGetIntegerv(GL_TEXTURE_BINDING_2D,
		  &state->texture_2d_bindings[i]);
  }
  glActiveTexture(state->active_texture); // Restore active texture
  
  glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &state->vao);
  glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &state->array_buffer);
  glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING,
		&state->element_array_buffer);
  
  state->blend_enabled = glIsEnabled(GL_BLEND);
  glGetIntegerv(GL_BLEND_SRC_RGB, &state->blend_src_rgb);
  glGetIntegerv(GL_BLEND_DST_RGB, &state->blend_dst_rgb);
  glGetIntegerv(GL_BLEND_SRC_ALPHA, &state->blend_src_alpha);
  glGetIntegerv(GL_BLEND_DST_ALPHA, &state->blend_dst_alpha);
  
  state->depth_test = glIsEnabled(GL_DEPTH_TEST);
  glGetIntegerv(GL_DEPTH_FUNC, &state->depth_func);
  
  state->cull_face = glIsEnabled(GL_CULL_FACE);
  glGetIntegerv(GL_FRONT_FACE, &state->front_face);
  glGetIntegerv(GL_CULL_FACE_MODE, &state->cull_face_mode);
  
  glGetFloatv(GL_COLOR_CLEAR_VALUE, state->clear_color);
  
  state->scissor_test = glIsEnabled(GL_SCISSOR_TEST);
  glGetIntegerv(GL_SCISSOR_BOX, state->scissor_box);
  
  glGetIntegerv(GL_VIEWPORT, state->viewport);
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &state->framebuffer);
}

static void restore_gl_state(const gl_state_backup_t *state) {
  glUseProgram(state->program);
  
  // Restore texture bindings
  for (int i = 0; i < 8; i++) {
    glActiveTexture(GL_TEXTURE0 + i);
    glBindTexture(GL_TEXTURE_2D, state->texture_2d_bindings[i]);
  }
  glActiveTexture(state->active_texture);
  
  glBindVertexArray(state->vao);
  glBindBuffer(GL_ARRAY_BUFFER, state->array_buffer);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, state->element_array_buffer);
  
  if (state->blend_enabled) {
    glEnable(GL_BLEND);
    glBlendFuncSeparate(state->blend_src_rgb,
			state->blend_dst_rgb,
			state->blend_src_alpha,
			state->blend_dst_alpha);
  } else {
    glDisable(GL_BLEND);
  }
  
  if (state->depth_test) {
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(state->depth_func);
  } else {
    glDisable(GL_DEPTH_TEST);
  }
  
  if (state->cull_face) {
    glEnable(GL_CULL_FACE);
    glFrontFace(state->front_face);
    glCullFace(state->cull_face_mode);
  } else {
    glDisable(GL_CULL_FACE);
  }
  
  glClearColor(state->clear_color[0], state->clear_color[1], 
	       state->clear_color[2], state->clear_color[3]);
  
  if (state->scissor_test) {
    glEnable(GL_SCISSOR_TEST);
    glScissor(state->scissor_box[0], state->scissor_box[1],
	      state->scissor_box[2], state->scissor_box[3]);
  } else {
    glDisable(GL_SCISSOR_TEST);
  }
  
  glViewport(state->viewport[0], state->viewport[1], 
	     state->viewport[2], state->viewport[3]);
  glBindFramebuffer(GL_FRAMEBUFFER, state->framebuffer);
}


void videoOff(GR_OBJ *gobj) {
    MPV_VIDEO *v = (MPV_VIDEO *) GR_CLIENTDATA(gobj);
    if (v->mpv) {
        int pause = 1;
        mpv_set_property(v->mpv, "pause", MPV_FORMAT_FLAG, &pause);
        v->paused = 1;
    }
}

void videoShow(GR_OBJ *gobj) {
    MPV_VIDEO *v = (MPV_VIDEO *) GR_CLIENTDATA(gobj);
    
    if (!v->visible || !v->mpv_gl) return;

    GLfloat saved_clear_color[4];
    glGetFloatv(GL_COLOR_CLEAR_VALUE, saved_clear_color);
    
    // Save essential state for FBO operations
    GLint prev_framebuffer;
    GLint prev_viewport[4];
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev_framebuffer);
    glGetIntegerv(GL_VIEWPORT, prev_viewport);
    
    // Render to FBO with minimal state changes
    glBindFramebuffer(GL_FRAMEBUFFER, v->fbo);
    glViewport(0, 0, v->width, v->height);
    
    mpv_opengl_fbo fbo = {
        .fbo = v->fbo,
        .w = v->width,
        .h = v->height,
        .internal_format = GL_RGBA8
    };
    
    int flip_y = 1;
    mpv_render_param params[] = {
        {MPV_RENDER_PARAM_OPENGL_FBO, &fbo},
        {MPV_RENDER_PARAM_FLIP_Y, &flip_y},
        {0}
    };
    
    mpv_render_context_render(v->mpv_gl, params);
    
    // Restore framebuffer state
    glBindFramebuffer(GL_FRAMEBUFFER, prev_framebuffer);
    glViewport(prev_viewport[0], prev_viewport[1], prev_viewport[2], prev_viewport[3]);

    glClearColor(saved_clear_color[0], saved_clear_color[1], 
                 saved_clear_color[2], saved_clear_color[3]);
    
    if (v->hidden) {
        v->redraw = 0;
        return;
    }
    
    // Draw quad with fresh state setup
    float modelview[16], projection[16];
    stimGetMatrix(STIM_MODELVIEW_MATRIX, modelview);
    stimGetMatrix(STIM_PROJECTION_MATRIX, projection);
    
    glUseProgram(MpvShaderProgram);
    glUniformMatrix4fv(MpvUniformModelview, 1, GL_FALSE, modelview);
    glUniformMatrix4fv(MpvUniformProjection, 1, GL_FALSE, projection);
    
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, v->texture);
    glUniform1i(MpvUniformTexture, 0);
    
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    
    glBindVertexArray(v->vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    
    // Clean up
    glBindVertexArray(0);
    glUseProgram(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_BLEND);
    
    v->redraw = 0;
}

void videoUpdate(GR_OBJ *gobj) {
    MPV_VIDEO *v = (MPV_VIDEO *) GR_CLIENTDATA(gobj);
    
    if (v->timer_script) sendTclCommand(v->timer_script);
    
    // Check for mpv events
    while (1) {
        mpv_event *event = mpv_wait_event(v->mpv, 0);
        if (event->event_id == MPV_EVENT_NONE) break;
        
        switch (event->event_id) {
            case MPV_EVENT_END_FILE:
                v->eof_reached = 1;
                if (v->repeat_mode) {
                    // Loop back to beginning
                    double time = 0.0;
                    mpv_set_property(v->mpv, "time-pos", MPV_FORMAT_DOUBLE, &time);
                    int pause = 0;
                    mpv_set_property(v->mpv, "pause", MPV_FORMAT_FLAG, &pause);
                    v->eof_reached = 0;
                    v->paused = 0;
                } else {
                    // Pause at end if not repeating
                    int pause = 1;
                    mpv_set_property(v->mpv, "pause", MPV_FORMAT_FLAG, &pause);
                    v->paused = 1;
                }
                break;
            case MPV_EVENT_FILE_LOADED:
                v->eof_reached = 0;
                break;
            case MPV_EVENT_PLAYBACK_RESTART:
                v->eof_reached = 0;
                break;
            default:
                break;
        }
    }
    
    // Get current time
    double time_pos;
    if (mpv_get_property(v->mpv, "time-pos", MPV_FORMAT_DOUBLE, &time_pos) >= 0) {
        v->current_time = time_pos;
        // Rough frame calculation (would need fps for accuracy)
        v->cur_frame = (int)(time_pos * 30); // assuming 30fps
    }
    
    v->redraw = 1;
    kickAnimation();
}

void videoDelete(GR_OBJ *gobj) {
    MPV_VIDEO *v = (MPV_VIDEO *) GR_CLIENTDATA(gobj);

    // Clean up render context first (while mpv is still valid)
    if (v->mpv_gl) {
        mpv_render_context_free(v->mpv_gl);
        v->mpv_gl = NULL;
    }
    
    // Simple mpv cleanup - let mpv_terminate_destroy handle everything
    if (v->mpv) {
        mpv_terminate_destroy(v->mpv);
        v->mpv = NULL;
    }
    
    if (v->timer_script) free(v->timer_script);
    
    // Clean up OpenGL resources
    if (v->fbo) glDeleteFramebuffers(1, &v->fbo);
    if (v->texture) glDeleteTextures(1, &v->texture);
    if (v->vertex_buffer) glDeleteBuffers(1, &v->vertex_buffer);
    if (v->vao) glDeleteVertexArrays(1, &v->vao);
    
    free((void *) v);
}

void videoReset(GR_OBJ *gobj) {
    MPV_VIDEO *v = (MPV_VIDEO *) GR_CLIENTDATA(gobj);
    if (v->mpv) {
        // Seek to beginning
        double time = 0.0;
        mpv_set_property(v->mpv, "time-pos", MPV_FORMAT_DOUBLE, &time);
        
        // Clear EOF state and unpause
        v->eof_reached = 0;
        v->cur_frame = v->start_frame;
        v->current_time = 0.0;
        
        // Leave it paused - let videoShow handle unpausing
        // This matches expected behavior where reset prepares but doesn't auto-play
        int pause = 1;
        mpv_set_property(v->mpv, "pause", MPV_FORMAT_FLAG, &pause);
        v->paused = 1;
	v->user_paused = 0;
	
        // Force a redraw
        v->redraw = 1;
    }
}

// Helper function to compile shader
static GLuint compile_shader(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    
    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetShaderInfoLog(shader, 512, NULL, infoLog);
        fprintf(stderr, "Shader compilation error: %s\n", infoLog);
        return 0;
    }
    return shader;
}

// Initialize OpenGL resources
static int init_gl_resources(MPV_VIDEO *v) {
    // Create VAO and VBO
    glGenVertexArrays(1, &v->vao);
    glBindVertexArray(v->vao);
    
    glGenBuffers(1, &v->vertex_buffer);
    glBindBuffer(GL_ARRAY_BUFFER, v->vertex_buffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad_vertices), quad_vertices, GL_STATIC_DRAW);
    
    // Set up vertex attributes (vec3 position + vec2 texcoord)
    // Position attribute (location = 0)
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
    
    // Texture coord attribute (location = 1)
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
    
    glBindVertexArray(0);
    
    // Create framebuffer and texture for mpv rendering
    glGenFramebuffers(1, &v->fbo);
    glGenTextures(1, &v->texture);
    
    glBindTexture(GL_TEXTURE_2D, v->texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, v->width, v->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    
    glBindFramebuffer(GL_FRAMEBUFFER, v->fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, v->texture, 0);
    
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "Framebuffer not complete!\n");
        return -1;
    }
    
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    
    return 0;
}

// Create shader program once at module initialization
static int create_mpv_shader_program() {
    // Create and compile shaders
    GLuint vertex_shader = compile_shader(GL_VERTEX_SHADER, vertex_shader_source);
    GLuint fragment_shader = compile_shader(GL_FRAGMENT_SHADER, fragment_shader_source);
    
    if (!vertex_shader || !fragment_shader) return -1;
    
    // Create shader program
    MpvShaderProgram = glCreateProgram();
    glAttachShader(MpvShaderProgram, vertex_shader);
    glAttachShader(MpvShaderProgram, fragment_shader);
    glLinkProgram(MpvShaderProgram);
    
    GLint success;
    glGetProgramiv(MpvShaderProgram, GL_LINK_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetProgramInfoLog(MpvShaderProgram, 512, NULL, infoLog);
        fprintf(stderr, "Mpv shader program linking error: %s\n", infoLog);
        return -1;
    }
    
    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);
    
    // Get uniform locations once
    MpvUniformTexture = glGetUniformLocation(MpvShaderProgram, "ourTexture");
    MpvUniformModelview = glGetUniformLocation(MpvShaderProgram, "modelviewMat");
    MpvUniformProjection = glGetUniformLocation(MpvShaderProgram, "projMat");
    
    return 0;
}

int videoCreate(OBJ_LIST *objlist, char *filename, double rate, int play_audio) {
    const char *name = "Mpv";
    GR_OBJ *obj;
    MPV_VIDEO *v;

    obj = gobjCreateObj();
    if (!obj) return -1;

    strcpy(GR_NAME(obj), name);
    GR_OBJTYPE(obj) = MpvID;

    GR_UPDATEFUNCP(obj) = videoUpdate;
    GR_DELETEFUNCP(obj) = videoDelete;
    GR_RESETFUNCP(obj) = videoReset;
    GR_OFFFUNCP(obj) = videoOff;
    GR_ACTIONFUNCP(obj) = videoShow;

    v = (MPV_VIDEO *) calloc(1, sizeof(MPV_VIDEO));
    GR_CLIENTDATA(obj) = v;

    // Initialize mpv
    v->mpv = mpv_create();
    if (!v->mpv) {
        fprintf(getConsoleFP(), "error creating mpv context\n");
        free(v);
        return -1;
    }

    // Set mpv options for off-screen rendering
    mpv_set_option_string(v->mpv, "terminal", "no");
    mpv_set_option_string(v->mpv, "msg-level", "all=warn");
    mpv_set_option_string(v->mpv, "vo", "libmpv");
    mpv_set_option_string(v->mpv, "hwdec", "auto");
    mpv_set_option_string(v->mpv, "pause", "yes");
    mpv_set_option_string(v->mpv, "loop-file", "no");

    // Cache settings
    mpv_set_option_string(v->mpv, "cache", "yes");
    mpv_set_option_string(v->mpv, "cache-secs", "10");
    mpv_set_option_string(v->mpv, "demuxer-max-bytes", "50M");
    mpv_set_option_string(v->mpv, "demuxer-max-back-bytes", "25M");
    
    
    if (!play_audio) {
        mpv_set_option_string(v->mpv, "audio", "no");
    }

    // Initialize mpv
    if (mpv_initialize(v->mpv) < 0) {
        fprintf(getConsoleFP(), "error initializing mpv\n");
        mpv_terminate_destroy(v->mpv);
        free(v);
        return -1;
    }

    // Set default video dimensions (will be updated after file load)
    v->width = 1920;
    v->height = 1080;

    // Initialize state
    v->cur_frame = 1;
    v->start_frame = 1;
    v->stop_frame = 0;
    v->visible = 1;
    v->hidden = 0;  // Not hidden by default
    v->paused = 1;
    v->redraw = 1;
    v->repeat_mode = 0;
    v->eof_reached = 0;
    v->duration = 0.0;
    v->current_time = 0.0;
    v->timer_script = NULL;

    // Initialize OpenGL resources
    if (init_gl_resources(v) < 0) {
        fprintf(getConsoleFP(), "error initializing OpenGL resources\n");
        mpv_terminate_destroy(v->mpv);
        free(v);
        return -1;
    }

    // Set up mpv OpenGL context
    mpv_opengl_init_params gl_init_params = {
        .get_proc_address = get_proc_address,
        .get_proc_address_ctx = NULL
    };
    
    mpv_render_param params[] = {
        {MPV_RENDER_PARAM_API_TYPE, MPV_RENDER_API_TYPE_OPENGL},
        {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &gl_init_params},
        {0}
    };

    if (mpv_render_context_create(&v->mpv_gl, v->mpv, params) < 0) {
        fprintf(getConsoleFP(), "error creating mpv OpenGL context\n");
        mpv_terminate_destroy(v->mpv);
        free(v);
        return -1;
    }

    // Set render update callback
    mpv_render_context_set_update_callback(v->mpv_gl, on_mpv_render_update, v);

    // Load the file
    const char* cmd[] = {"loadfile", filename, NULL};
    if (mpv_command(v->mpv, cmd) < 0) {
        fprintf(getConsoleFP(), "error loading file: %s\n", filename);
        mpv_render_context_free(v->mpv_gl);
        mpv_terminate_destroy(v->mpv);
        free(v);
        return -1;
    }

    // Wait for file to be loaded and get actual dimensions
    int loaded = 0;
    while (!loaded) {
        mpv_event *event = mpv_wait_event(v->mpv, 1.0);
        if (event->event_id == MPV_EVENT_FILE_LOADED) {
            loaded = 1;
            
            // Try to get actual video dimensions
            int64_t w = 0, h = 0;
            if (mpv_get_property(v->mpv, "width", MPV_FORMAT_INT64, &w) >= 0 &&
                mpv_get_property(v->mpv, "height", MPV_FORMAT_INT64, &h) >= 0) {
                if (w > 0 && h > 0) {
                    v->width = (int)w;
                    v->height = (int)h;
                    
                    // Recreate texture with correct dimensions
                    glBindTexture(GL_TEXTURE_2D, v->texture);
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, v->width, v->height, 0, 
                                GL_RGBA, GL_UNSIGNED_BYTE, NULL);
                    glBindTexture(GL_TEXTURE_2D, 0);
                }
            }
            
            // Get duration
            mpv_get_property(v->mpv, "duration", MPV_FORMAT_DOUBLE, &v->duration);
        } else if (event->event_id == MPV_EVENT_SHUTDOWN) {
            break;
        }
    }

    return gobjAddObj(objlist, obj);
}

// Tcl command implementations
static int videoCmd(ClientData clientData, Tcl_Interp *interp,
                    int argc, char *argv[]) {
    OBJ_LIST *olist = (OBJ_LIST *) clientData;
    int id;
    double rate = 0;
    int play_audio = 1;

    if (argc < 2) {
        Tcl_AppendResult(interp, "usage: ", argv[0],
                         " videofile ?play_audio? ?rate?", NULL);
        return TCL_ERROR;
    }

    if (argc > 2) {
        if (Tcl_GetInt(interp, argv[2], &play_audio) != TCL_OK)
            return TCL_ERROR;
    }

    if (argc > 3) {
        if (Tcl_GetDouble(interp, argv[3], &rate) != TCL_OK)
            return TCL_ERROR;
    }

    if ((id = videoCreate(olist, argv[1], rate, play_audio)) < 0) {
        Tcl_SetResult(interp, "error loading mpv video", TCL_STATIC);
        return TCL_ERROR;
    }

    Tcl_SetObjResult(interp, Tcl_NewIntObj(id));
    return TCL_OK;
}

static int videopauseCmd(ClientData clientData, Tcl_Interp *interp,
                        int argc, char *argv[]) {
    OBJ_LIST *olist = (OBJ_LIST *) clientData;
    MPV_VIDEO *v;
    int id, pause;

    if (argc < 3) {
        Tcl_AppendResult(interp, "usage: ", argv[0], " id pause(0/1)", NULL);
        return TCL_ERROR;
    }

    if (Tcl_GetInt(interp, argv[1], &id) != TCL_OK) return TCL_ERROR;
    if (id >= OL_NOBJS(olist)) {
        Tcl_AppendResult(interp, argv[0], ": objid out of range", NULL);
        return TCL_ERROR;
    }

    if (GR_OBJTYPE(OL_OBJ(olist, id)) != MpvID) {
        Tcl_AppendResult(interp, argv[0], ": object not of type mpv video", NULL);
        return TCL_ERROR;
    }

    v = GR_CLIENTDATA(OL_OBJ(olist, id));
    
    if (Tcl_GetInt(interp, argv[2], &pause) != TCL_OK) return TCL_ERROR;
    
    if (v->mpv) {
        mpv_set_property(v->mpv, "pause", MPV_FORMAT_FLAG, &pause);
        v->paused = pause;
	v->user_paused = pause;	
    }
    
    return TCL_OK;
}

static int videorepeatCmd(ClientData clientData, Tcl_Interp *interp,
                         int argc, char *argv[]) {
    OBJ_LIST *olist = (OBJ_LIST *) clientData;
    MPV_VIDEO *v;
    int id, repeat;

    if (argc < 3) {
        Tcl_AppendResult(interp, "usage: ", argv[0], " id repeat(0/1)", NULL);
        return TCL_ERROR;
    }

    if (Tcl_GetInt(interp, argv[1], &id) != TCL_OK) return TCL_ERROR;
    if (id >= OL_NOBJS(olist)) {
        Tcl_AppendResult(interp, argv[0], ": objid out of range", NULL);
        return TCL_ERROR;
    }

    if (GR_OBJTYPE(OL_OBJ(olist, id)) != MpvID) {
        Tcl_AppendResult(interp, argv[0], ": object not of type mpv video", NULL);
        return TCL_ERROR;
    }

    v = GR_CLIENTDATA(OL_OBJ(olist, id));
    
    if (Tcl_GetInt(interp, argv[2], &repeat) != TCL_OK) return TCL_ERROR;
    
    v->repeat_mode = repeat;
    
    return TCL_OK;
}

static int videohideCmd(ClientData clientData, Tcl_Interp *interp,
                        int argc, char *argv[]) {
    OBJ_LIST *olist = (OBJ_LIST *) clientData;
    MPV_VIDEO *v;
    int id, hide;

    if (argc < 3) {
        Tcl_AppendResult(interp, "usage: ", argv[0], " id hide(0/1)", NULL);
        return TCL_ERROR;
    }

    if (Tcl_GetInt(interp, argv[1], &id) != TCL_OK) return TCL_ERROR;
    if (id >= OL_NOBJS(olist)) {
        Tcl_AppendResult(interp, argv[0], ": objid out of range", NULL);
        return TCL_ERROR;
    }

    if (GR_OBJTYPE(OL_OBJ(olist, id)) != MpvID) {
        Tcl_AppendResult(interp, argv[0], ": object not of type mpv video", NULL);
        return TCL_ERROR;
    }

    v = GR_CLIENTDATA(OL_OBJ(olist, id));
    
    if (Tcl_GetInt(interp, argv[2], &hide) != TCL_OK) return TCL_ERROR;
    
    v->hidden = hide;
    
    // Mute/unmute audio when hiding/showing
    if (v->mpv) {
        mpv_set_property(v->mpv, "mute", MPV_FORMAT_FLAG, &hide);
    }
    
    return TCL_OK;
}

static int videoseekCmd(ClientData clientData, Tcl_Interp *interp,
                       int argc, char *argv[]) {
  OBJ_LIST *olist = (OBJ_LIST *) clientData;
  MPV_VIDEO *v;
  int id;
  double time;
  
  if (argc < 3) {
    Tcl_AppendResult(interp, "usage: ", argv[0], " id time_in_seconds", NULL);
    return TCL_ERROR;
  }
  
  if (Tcl_GetInt(interp, argv[1], &id) != TCL_OK) return TCL_ERROR;
  if (id >= OL_NOBJS(olist)) {
    Tcl_AppendResult(interp, argv[0], ": objid out of range", NULL);
    return TCL_ERROR;
  }
  
  if (GR_OBJTYPE(OL_OBJ(olist, id)) != MpvID) {
    Tcl_AppendResult(interp, argv[0], ": object not of type mpv video", NULL);
    return TCL_ERROR;
  }
  
  v = GR_CLIENTDATA(OL_OBJ(olist, id));
  
  if (Tcl_GetDouble(interp, argv[2], &time) != TCL_OK) return TCL_ERROR;
  
  if (v->mpv) {
    // Seek to specified time
    mpv_set_property(v->mpv, "time-pos", MPV_FORMAT_DOUBLE, &time);
    
    // If paused, force decode one frame
    if (v->paused) {
      int frame_step = 1;
      mpv_set_property(v->mpv, "frame-step", MPV_FORMAT_FLAG, &frame_step);
    }
    
    v->redraw = 1;
  }
  
  return TCL_OK;
}

#ifdef _WIN32
EXPORT(int, Mpvvideo_Init) (Tcl_Interp *interp)
#else
#ifdef __cplusplus
extern "C" {
#endif
extern int Mpvvideo_Init(Tcl_Interp *interp);
#ifdef __cplusplus
}
#endif
int Mpvvideo_Init(Tcl_Interp *interp)
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

    if (MpvID < 0) {
        MpvID = gobjRegisterType();
        
        // Load OpenGL functions once (safe to call multiple times)
        gladLoadGL();
        
        // Create shader program once for all video instances
        if (create_mpv_shader_program() < 0) {
            Tcl_SetResult(interp, "error creating mpv shader program", TCL_STATIC);
            return TCL_ERROR;
        }
    }
    else return TCL_OK;

    Tcl_CreateCommand(interp, "video", (Tcl_CmdProc *) videoCmd,
                      (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
    Tcl_CreateCommand(interp, "videoPause", (Tcl_CmdProc *) videopauseCmd,
                      (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
    Tcl_CreateCommand(interp, "videoRepeat", (Tcl_CmdProc *) videorepeatCmd,
                      (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
    Tcl_CreateCommand(interp, "videoHide", (Tcl_CmdProc *) videohideCmd,
                      (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
    Tcl_CreateCommand(interp, "videoSeek", (Tcl_CmdProc *) videoseekCmd,
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
