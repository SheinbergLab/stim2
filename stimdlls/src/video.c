/*
 * video.c
 *  Use FFmpeg to render video animations using OpenGL
 *  Designed for psychophysics experiments requiring minimal overhead
 */

#ifdef __linux__
#define _GNU_SOURCE
#endif

#ifdef WIN32
#include <windows.h>
#endif

#include <assert.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>

#include <tcl.h>

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <stim2.h>
#include <prmutil.h>

typedef struct _ffmpeg_video {
    AVFormatContext *format_ctx;
    AVCodecContext *codec_ctx;
    AVFrame *frame;
    AVFrame *rgb_frame;
    struct SwsContext *sws_ctx;
    AVPacket *packet;
    
    int video_stream_idx;
    int width;
    int height;
    double duration;
    double frame_rate;
    AVRational time_base;
    
    // Playback state
    double current_time;
    int64_t current_pts;
    int paused;
    int user_paused;
    int eof_reached;
    int repeat_mode;
    int visible;
    int hidden;
    
    // OpenGL resources  
    GLuint texture;
    GLuint vertex_buffer;
    GLuint vao;
    
    // Frame timing
    double video_start_time;    // When video started playing (stimulus time)
    double target_frame_time;   // When current frame should be displayed
    int frames_decoded;         // Total frames decoded since start
    int needs_frame_update;
    
    char *timer_script;
} FFMPEG_VIDEO;

static int VideoID = -1;  /* unique video object id */
static GLuint VideoShaderProgram = 0;  /* shared shader program */
static GLint VideoUniformTexture = -1;
static GLint VideoUniformModelview = -1;
static GLint VideoUniformProjection = -1;

// OpenGL shader sources (identical to mpv version)
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

static float quad_vertices[] = {
    // positions (vec3)  // texture coords (vec2)
    -0.5f,  0.5f, 0.0f,  0.0f, 0.0f,  // top-left -> top of texture
    -0.5f, -0.5f, 0.0f,  0.0f, 1.0f,  // bottom-left -> bottom of texture
     0.5f, -0.5f, 0.0f,  1.0f, 1.0f,  // bottom-right -> bottom of texture
    -0.5f,  0.5f, 0.0f,  0.0f, 0.0f,  // top-left -> top of texture
     0.5f, -0.5f, 0.0f,  1.0f, 1.0f,  // bottom-right -> bottom of texture
     0.5f,  0.5f, 0.0f,  1.0f, 0.0f   // top-right -> top of texture
};

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

// Create shader program once at module initialization
static int create_video_shader_program() {
    GLuint vertex_shader = compile_shader(GL_VERTEX_SHADER, vertex_shader_source);
    GLuint fragment_shader = compile_shader(GL_FRAGMENT_SHADER, fragment_shader_source);
    
    if (!vertex_shader || !fragment_shader) return -1;
    
    VideoShaderProgram = glCreateProgram();
    glAttachShader(VideoShaderProgram, vertex_shader);
    glAttachShader(VideoShaderProgram, fragment_shader);
    glLinkProgram(VideoShaderProgram);
    
    GLint success;
    glGetProgramiv(VideoShaderProgram, GL_LINK_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetProgramInfoLog(VideoShaderProgram, 512, NULL, infoLog);
        fprintf(stderr, "Video shader program linking error: %s\n", infoLog);
        return -1;
    }
    
    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);
    
    VideoUniformTexture = glGetUniformLocation(VideoShaderProgram, "ourTexture");
    VideoUniformModelview = glGetUniformLocation(VideoShaderProgram, "modelviewMat");
    VideoUniformProjection = glGetUniformLocation(VideoShaderProgram, "projMat");
    
    return 0;
}

// Initialize OpenGL resources
static int init_gl_resources(FFMPEG_VIDEO *v) {
    // Create VAO and VBO
    glGenVertexArrays(1, &v->vao);
    glBindVertexArray(v->vao);
    
    glGenBuffers(1, &v->vertex_buffer);
    glBindBuffer(GL_ARRAY_BUFFER, v->vertex_buffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad_vertices), quad_vertices, GL_STATIC_DRAW);
    
    // Position attribute (location = 0)
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
    
    // Texture coord attribute (location = 1)
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
    
    glBindVertexArray(0);
    
    // Create texture
    glGenTextures(1, &v->texture);
    glBindTexture(GL_TEXTURE_2D, v->texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    
    return 0;
}

// Upload frame data to OpenGL texture
static void upload_frame_to_texture(FFMPEG_VIDEO *v) {
    glBindTexture(GL_TEXTURE_2D, v->texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, v->width, v->height, 0, 
                 GL_RGB, GL_UNSIGNED_BYTE, v->rgb_frame->data[0]);
    glBindTexture(GL_TEXTURE_2D, 0);
}

// Decode next frame if needed
static int decode_next_frame(FFMPEG_VIDEO *v) {
    int ret;
    
    while ((ret = av_read_frame(v->format_ctx, v->packet)) >= 0) {
        if (v->packet->stream_index == v->video_stream_idx) {
            ret = avcodec_send_packet(v->codec_ctx, v->packet);
            if (ret < 0) {
                av_packet_unref(v->packet);
                continue;
            }
            
            ret = avcodec_receive_frame(v->codec_ctx, v->frame);
            if (ret == 0) {
                // Convert to RGB
                sws_scale(v->sws_ctx, (const uint8_t* const*)v->frame->data,
                         v->frame->linesize, 0, v->codec_ctx->height,
                         v->rgb_frame->data, v->rgb_frame->linesize);
                
                // Update timing
                v->current_pts = v->frame->pts;
                v->current_time = v->current_pts * av_q2d(v->time_base);
                
                av_packet_unref(v->packet);
                return 1; // Got frame
            }
        }
        av_packet_unref(v->packet);
    }
    
    // End of file
    v->eof_reached = 1;
    return 0;
}

void videoOff(GR_OBJ *gobj) {
    FFMPEG_VIDEO *v = (FFMPEG_VIDEO *) GR_CLIENTDATA(gobj);
    v->paused = 1;
}

void videoShow(GR_OBJ *gobj) {
    FFMPEG_VIDEO *v = (FFMPEG_VIDEO *) GR_CLIENTDATA(gobj);
    
    if (!v->visible || v->hidden) return;
    
    // Timer function handles frame decoding, we just render the current frame
    // No frame timing logic needed here - just display what's in the texture
    
    // Render textured quad
    float modelview[16], projection[16];
    stimGetMatrix(STIM_MODELVIEW_MATRIX, modelview);
    stimGetMatrix(STIM_PROJECTION_MATRIX, projection);
    
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    
    glUseProgram(VideoShaderProgram);
    glUniformMatrix4fv(VideoUniformModelview, 1, GL_FALSE, modelview);
    glUniformMatrix4fv(VideoUniformProjection, 1, GL_FALSE, projection);
    
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, v->texture);
    glUniform1i(VideoUniformTexture, 0);
    
    glBindVertexArray(v->vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    
    // Clean up (no state contamination!)
    glBindVertexArray(0);
    glUseProgram(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_BLEND);
}

void videoTimer(GR_OBJ *gobj) {
    FFMPEG_VIDEO *v = (FFMPEG_VIDEO *) GR_CLIENTDATA(gobj);
    
    if (v->timer_script) sendTclCommand(v->timer_script);
    
    if (v->paused || v->eof_reached) {
        return;
    }
    
    // Get current stimulus time in seconds
    double current_time = getStimTime() / 1000.0;
    double elapsed_time = current_time - v->video_start_time;
    
    // Calculate how many frames should have been displayed by now
    int target_frame = (int)(elapsed_time * v->frame_rate);
    
    // If we're behind, decode frames to catch up (with safety limit)
    int frames_to_catch_up = target_frame - v->frames_decoded;
    int max_catchup_frames = 5;  // Don't decode more than 5 frames at once
    
    if (frames_to_catch_up > 0) {
        frames_to_catch_up = (frames_to_catch_up > max_catchup_frames) ? 
                             max_catchup_frames : frames_to_catch_up;
        
        for (int i = 0; i < frames_to_catch_up; i++) {
            if (decode_next_frame(v)) {
                v->frames_decoded++;
                // Only upload the last frame to texture (the one we'll display)
                if (i == frames_to_catch_up - 1) {
                    upload_frame_to_texture(v);
                }
            } else {
                // Hit end of file or error
                break;
            }
        }
        
        // Update target time for next frame
        v->target_frame_time = v->video_start_time + (v->frames_decoded / v->frame_rate);
        
        if (frames_to_catch_up > 1) {
            fprintf(stderr, "Video catchup: decoded %d frames\n", frames_to_catch_up);
        }
    }
    
    // Handle repeat mode
    if (v->eof_reached && v->repeat_mode) {
        av_seek_frame(v->format_ctx, v->video_stream_idx, 0, AVSEEK_FLAG_BACKWARD);
        avcodec_flush_buffers(v->codec_ctx);
        v->eof_reached = 0;
        v->current_time = 0.0;
        v->current_pts = 0;
        v->video_start_time = current_time;  // Reset timing
        v->frames_decoded = 0;
        
        // Decode first frame immediately
        if (decode_next_frame(v)) {
            upload_frame_to_texture(v);
            v->frames_decoded = 1;
        }
    }
    
    kickAnimation();
}

void videoDelete(GR_OBJ *gobj) {
    FFMPEG_VIDEO *v = (FFMPEG_VIDEO *) GR_CLIENTDATA(gobj);

    // Clean up FFmpeg resources
    if (v->sws_ctx) sws_freeContext(v->sws_ctx);
    if (v->rgb_frame) av_frame_free(&v->rgb_frame);
    if (v->frame) av_frame_free(&v->frame);
    if (v->packet) av_packet_free(&v->packet);
    if (v->codec_ctx) avcodec_free_context(&v->codec_ctx);
    if (v->format_ctx) avformat_close_input(&v->format_ctx);
    if (v->timer_script) free(v->timer_script);
    
    // Clean up OpenGL resources
    if (v->texture) glDeleteTextures(1, &v->texture);
    if (v->vertex_buffer) glDeleteBuffers(1, &v->vertex_buffer);
    if (v->vao) glDeleteVertexArrays(1, &v->vao);
    
    free((void *) v);
}

void videoReset(GR_OBJ *gobj) {
    FFMPEG_VIDEO *v = (FFMPEG_VIDEO *) GR_CLIENTDATA(gobj);
    
    // Seek to beginning
    av_seek_frame(v->format_ctx, v->video_stream_idx, 0, AVSEEK_FLAG_BACKWARD);
    avcodec_flush_buffers(v->codec_ctx);
    
    v->eof_reached = 0;
    v->current_time = 0.0;
    v->current_pts = 0;
    v->paused = 1;
    v->user_paused = 0;
    v->needs_frame_update = 1;
}

int videoCreate(OBJ_LIST *objlist, char *filename) {
    const char *name = "Video";
    GR_OBJ *obj;
    FFMPEG_VIDEO *v;
    int ret;

    obj = gobjCreateObj();
    if (!obj) return -1;

    strcpy(GR_NAME(obj), name);
    GR_OBJTYPE(obj) = VideoID;

    GR_TIMERFUNCP(obj) = videoTimer;
    GR_DELETEFUNCP(obj) = videoDelete;
    GR_RESETFUNCP(obj) = videoReset;
    GR_OFFFUNCP(obj) = videoOff;
    GR_ACTIONFUNCP(obj) = videoShow;

    v = (FFMPEG_VIDEO *) calloc(1, sizeof(FFMPEG_VIDEO));
    GR_CLIENTDATA(obj) = v;

    // Open video file
    ret = avformat_open_input(&v->format_ctx, filename, NULL, NULL);
    if (ret < 0) {
        fprintf(getConsoleFP(), "error opening video file: %s\n", filename);
        free(v);
        return -1;
    }

    ret = avformat_find_stream_info(v->format_ctx, NULL);
    if (ret < 0) {
        fprintf(getConsoleFP(), "error finding stream info: %s\n", filename);
        avformat_close_input(&v->format_ctx);
        free(v);
        return -1;
    }

    // Find video stream
    v->video_stream_idx = av_find_best_stream(v->format_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (v->video_stream_idx < 0) {
        fprintf(getConsoleFP(), "no video stream found: %s\n", filename);
        avformat_close_input(&v->format_ctx);
        free(v);
        return -1;
    }

    AVStream *video_stream = v->format_ctx->streams[v->video_stream_idx];
    
    // Set up decoder
    const AVCodec *codec = avcodec_find_decoder(video_stream->codecpar->codec_id);
    if (!codec) {
        fprintf(getConsoleFP(), "unsupported codec: %s\n", filename);
        avformat_close_input(&v->format_ctx);
        free(v);
        return -1;
    }

    v->codec_ctx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(v->codec_ctx, video_stream->codecpar);
    
    ret = avcodec_open2(v->codec_ctx, codec, NULL);
    if (ret < 0) {
        fprintf(getConsoleFP(), "error opening codec: %s\n", filename);
        avcodec_free_context(&v->codec_ctx);
        avformat_close_input(&v->format_ctx);
        free(v);
        return -1;
    }

    // Get video properties
    v->width = v->codec_ctx->width;
    v->height = v->codec_ctx->height;
    v->time_base = video_stream->time_base;
    v->duration = video_stream->duration * av_q2d(v->time_base);
    v->frame_rate = av_q2d(video_stream->r_frame_rate);

    // Allocate frames
    v->frame = av_frame_alloc();
    v->rgb_frame = av_frame_alloc();
    v->packet = av_packet_alloc();

    // Set up RGB frame
    av_image_alloc(v->rgb_frame->data, v->rgb_frame->linesize, 
                   v->width, v->height, AV_PIX_FMT_RGB24, 32);

    // Set up software scaler
    v->sws_ctx = sws_getContext(v->width, v->height, v->codec_ctx->pix_fmt,
                                v->width, v->height, AV_PIX_FMT_RGB24,
                                SWS_BILINEAR, NULL, NULL, NULL);

    // Initialize state
    v->visible = 1;
    v->hidden = 0;
    v->paused = 1;
    v->user_paused = 0;
    v->repeat_mode = 0;
    v->eof_reached = 0;
    v->current_time = 0.0;
    v->current_pts = 0;
    v->video_start_time = 0.0;
    v->target_frame_time = 0.0;
    v->frames_decoded = 0;
    v->needs_frame_update = 1;
    v->timer_script = NULL;

    // Initialize OpenGL resources
    if (init_gl_resources(v) < 0) {
        fprintf(getConsoleFP(), "error initializing OpenGL resources\n");
        videoDelete(obj);
        return -1;
    }

    if (decode_next_frame(v)) {
      upload_frame_to_texture(v);
      v->needs_frame_update = 0;  // First frame is loaded
    } else {
      // Continue anyway - some videos might need special handling
    }
    
    return gobjAddObj(objlist, obj);
}

// Tcl command implementations
static int videoCmd(ClientData clientData, Tcl_Interp *interp,
                    int argc, char *argv[]) {
    OBJ_LIST *olist = (OBJ_LIST *) clientData;
    int id;

    if (argc < 2) {
        Tcl_AppendResult(interp, "usage: ", argv[0], " videofile", NULL);
        return TCL_ERROR;
    }

    if ((id = videoCreate(olist, argv[1])) < 0) {
        Tcl_SetResult(interp, "error loading video", TCL_STATIC);
        return TCL_ERROR;
    }

    Tcl_SetObjResult(interp, Tcl_NewIntObj(id));
    return TCL_OK;
}

static int videopauseCmd(ClientData clientData, Tcl_Interp *interp,
                        int argc, char *argv[]) {
    OBJ_LIST *olist = (OBJ_LIST *) clientData;
    FFMPEG_VIDEO *v;
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

    if (GR_OBJTYPE(OL_OBJ(olist, id)) != VideoID) {
        Tcl_AppendResult(interp, argv[0], ": object not of type video", NULL);
        return TCL_ERROR;
    }

    v = GR_CLIENTDATA(OL_OBJ(olist, id));
    
    if (Tcl_GetInt(interp, argv[2], &pause) != TCL_OK) return TCL_ERROR;
    
    // Handle timing when transitioning from paused to playing
    if (!pause && v->paused) {
        // Starting playback - record the start time
        v->video_start_time = getStimTime() / 1000.0;
        v->frames_decoded = 1;  // We already have first frame loaded
    }
    
    v->paused = pause;
    v->user_paused = pause;
    
    return TCL_OK;
}

static int videorepeatCmd(ClientData clientData, Tcl_Interp *interp,
                         int argc, char *argv[]) {
    OBJ_LIST *olist = (OBJ_LIST *) clientData;
    FFMPEG_VIDEO *v;
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

    if (GR_OBJTYPE(OL_OBJ(olist, id)) != VideoID) {
        Tcl_AppendResult(interp, argv[0], ": object not of type video", NULL);
        return TCL_ERROR;
    }

    v = GR_CLIENTDATA(OL_OBJ(olist, id));
    v->repeat_mode = repeat;
    
    return TCL_OK;
}

static int videohideCmd(ClientData clientData, Tcl_Interp *interp,
                        int argc, char *argv[]) {
    OBJ_LIST *olist = (OBJ_LIST *) clientData;
    FFMPEG_VIDEO *v;
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

    if (GR_OBJTYPE(OL_OBJ(olist, id)) != VideoID) {
        Tcl_AppendResult(interp, argv[0], ": object not of type video", NULL);
        return TCL_ERROR;
    }

    v = GR_CLIENTDATA(OL_OBJ(olist, id));
    v->hidden = hide;
    
    return TCL_OK;
}

static int videoseekCmd(ClientData clientData, Tcl_Interp *interp,
                       int argc, char *argv[]) {
    OBJ_LIST *olist = (OBJ_LIST *) clientData;
    FFMPEG_VIDEO *v;
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
    
    if (GR_OBJTYPE(OL_OBJ(olist, id)) != VideoID) {
        Tcl_AppendResult(interp, argv[0], ": object not of type video", NULL);
        return TCL_ERROR;
    }
    
    v = GR_CLIENTDATA(OL_OBJ(olist, id));
    
    if (Tcl_GetDouble(interp, argv[2], &time) != TCL_OK) return TCL_ERROR;
    
    // Seek to specified time
    int64_t timestamp = av_rescale_q(time * AV_TIME_BASE, 
                                     AV_TIME_BASE_Q, v->time_base);
    av_seek_frame(v->format_ctx, v->video_stream_idx, timestamp, AVSEEK_FLAG_BACKWARD);
    avcodec_flush_buffers(v->codec_ctx);
    
    v->current_time = time;
    v->eof_reached = 0;
    v->needs_frame_update = 1;
    
    return TCL_OK;
}

#ifdef _WIN32
EXPORT(int, Video_Init) (Tcl_Interp *interp)
#else
#ifdef __cplusplus
extern "C" {
#endif
extern int Video_Init(Tcl_Interp *interp);
#ifdef __cplusplus
}
#endif
int Video_Init(Tcl_Interp *interp)
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

    if (VideoID < 0) {
        VideoID = gobjRegisterType();
        
        // Load OpenGL functions
        gladLoadGL();
        
        // Create shader program once for all video instances
        if (create_video_shader_program() < 0) {
            Tcl_SetResult(interp, "error creating video shader program", TCL_STATIC);
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
