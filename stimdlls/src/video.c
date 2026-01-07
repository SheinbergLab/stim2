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
#include <objname.h>
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
  int64_t stream_start_pts;
  
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
  double video_start_time; // When video started (stim time)
  double target_frame_time;// When current frame should be displayed
  int frames_decoded;      // Total frames decoded since start
  int needs_frame_update;
  
  char *timer_script;
  char *eof_script;	   // EOF callback script
  int eof_fired;	   // Flag to ensure one-shot behavior
  
  int grayscale_mode;      // 0=color, 1=grayscale

  float brightness;        // -1.0 to 1.0 (additive)
  float contrast;          // 0.0 to 2.0 (1.0 = normal)
  float gamma;             // 0.1 to 3.0 (1.0 = normal)
  float opacity;           // 0.0 to 1.0 (for alpha blending)
  
  // Color channel manipulation
  float red_gain;          // 0.0 to 2.0 (1.0 = normal)
  float green_gain;        // 0.0 to 2.0 (1.0 = normal) 
  float blue_gain;         // 0.0 to 2.0 (1.0 = normal)
  
  // Special effects
  int invert_mode;         // 0=normal, 1=invert colors
  int threshold_mode;      // 0=off, 1=binary threshold
  float threshold_value;   // 0.0 to 1.0 for threshold
  
  // Gaze-contingent masking (normalized coordinates 0.0-1.0)
  int mask_mode;           // 0=off, 1=circular window, 2=rectangular window, 3=inverse (show outside)
  float mask_center_x;     // Center X (0.0 = left, 1.0 = right)
  float mask_center_y;     // Center Y (0.0 = top, 1.0 = bottom) 
  float mask_radius;       // Radius for circular mask (0.0-1.0)
  float mask_width;        // Width for rectangular mask (0.0-1.0)
  float mask_height;       // Height for rectangular mask (0.0-1.0)
  float mask_feather;      // Soft edge width (0.0 = hard, 0.1 = 10% feather)

  float aspect_ratio;      // width/height ratio
  
} FFMPEG_VIDEO;

static int VideoID = -1;  /* unique video object id */
static GLuint VideoShaderProgram = 0;  /* shared shader program */
static GLint VideoUniformTexture = -1;
static GLint VideoUniformModelview = -1;
static GLint VideoUniformProjection = -1;
static GLint VideoUniformGrayscale = -1;

static GLint VideoUniformBrightness = -1;
static GLint VideoUniformContrast = -1;
static GLint VideoUniformGamma = -1;
static GLint VideoUniformOpacity = -1;
static GLint VideoUniformColorGains = -1;
static GLint VideoUniformInvertMode = -1;
static GLint VideoUniformThresholdMode = -1;
static GLint VideoUniformThresholdValue = -1;

static GLint VideoUniformMaskMode = -1;
static GLint VideoUniformMaskCenter = -1;
static GLint VideoUniformMaskRadius = -1;
static GLint VideoUniformMaskSize = -1;
static GLint VideoUniformMaskFeather = -1;

static GLint VideoUniformAspectRatio = -1;

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
"\n"
"// Basic display controls\n"
"uniform int grayscale;\n"
"uniform float brightness;\n"
"uniform float contrast;\n"
"uniform float gamma;\n"
"uniform float opacity;\n"
"uniform vec3 colorGains;\n"
"uniform int invertMode;\n"
"uniform int thresholdMode;\n"
"uniform float thresholdValue;\n"
"\n"
"// Gaze-contingent masking\n"
"uniform int maskMode;\n"
"uniform vec2 maskCenter;\n"
"uniform float maskRadius;\n"
"uniform vec2 maskSize;\n"
"uniform float maskFeather;\n"
"uniform float aspectRatio;   // NEW: width/height for circular correction\n"
"\n"
"float smoothstep_safe(float edge0, float edge1, float x) {\n"
"    if (edge0 >= edge1) return step(edge0, x);\n"
"    return smoothstep(edge0, edge1, x);\n"
"}\n"
"\n"
"void main() {\n"
"    vec4 color = texture(ourTexture, TexCoord);\n"
"    \n"
"    // Apply color channel gains first\n"
"    color.rgb *= colorGains;\n"
"    \n"
"    // Convert to grayscale if requested\n"
"    if (grayscale == 1) {\n"
"        float gray = dot(color.rgb, vec3(0.299, 0.587, 0.114));\n"
"        color.rgb = vec3(gray);\n"
"    }\n"
"    \n"
"    // Apply contrast (around 0.5 midpoint)\n"
"    color.rgb = ((color.rgb - 0.5) * contrast) + 0.5;\n"
"    \n"
"    // Apply brightness\n"
"    color.rgb += brightness;\n"
"    \n"
"    // Apply gamma correction\n"
"    color.rgb = pow(max(color.rgb, 0.0), vec3(1.0 / gamma));\n"
"    \n"
"    // Apply binary threshold if enabled\n"
"    if (thresholdMode == 1) {\n"
"        float lum = dot(color.rgb, vec3(0.299, 0.587, 0.114));\n"
"        color.rgb = vec3(step(thresholdValue, lum));\n"
"    }\n"
"    \n"
"    // Apply color inversion if enabled\n"
"    if (invertMode == 1) {\n"
"        color.rgb = 1.0 - color.rgb;\n"
"    }\n"
"    \n"
"    // Apply gaze-contingent masking with aspect ratio correction\n"
"    float maskAlpha = 1.0;\n"
"    if (maskMode > 0) {\n"
"        if (maskMode == 1 || maskMode == 3) { // Circular windows\n"
"            // Correct for aspect ratio to make true circles\n"
"            vec2 correctedCoord = TexCoord - maskCenter;\n"
"            correctedCoord.x *= aspectRatio;  // Stretch X coordinate\n"
"            float dist = length(correctedCoord);\n"
"            \n"
"            if (maskMode == 1) { // Normal circular window\n"
"                if (maskFeather > 0.0) {\n"
"                    maskAlpha = 1.0 - smoothstep_safe(maskRadius - maskFeather, maskRadius, dist);\n"
"                } else {\n"
"                    maskAlpha = step(dist, maskRadius);\n"
"                }\n"
"            } else { // Inverse circular (show outside)\n"
"                if (maskFeather > 0.0) {\n"
"                    maskAlpha = smoothstep_safe(maskRadius - maskFeather, maskRadius, dist);\n"
"                } else {\n"
"                    maskAlpha = 1.0 - step(dist, maskRadius);\n"
"                }\n"
"            }\n"
"        } else if (maskMode == 2) { // Rectangular window (no aspect correction needed)\n"
"            vec2 halfSize = maskSize * 0.5;\n"
"            vec2 dist = abs(TexCoord - maskCenter) - halfSize;\n"
"            if (maskFeather > 0.0) {\n"
"                float rectDist = max(dist.x, dist.y);\n"
"                maskAlpha = 1.0 - smoothstep_safe(-maskFeather, 0.0, rectDist);\n"
"            } else {\n"
"                maskAlpha = step(max(dist.x, dist.y), 0.0);\n"
"            }\n"
"        }\n"
"    }\n"
"    \n"
"    // Clamp to valid range\n"
"    color.rgb = clamp(color.rgb, 0.0, 1.0);\n"
"    \n"
"    // Apply opacity and mask\n"
"    FragColor = vec4(color.rgb, color.a * opacity * maskAlpha);\n"
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

// Desktop OpenGL version (same shader code but without precision qualifiers)
static const char* fragment_shader_source = 
"#version 330 core\n"
"out vec4 FragColor;\n"
"in vec2 TexCoord;\n"
"uniform sampler2D ourTexture;\n"
"\n"
"uniform int grayscale;\n"
"uniform float brightness;\n"
"uniform float contrast;\n"
"uniform float gamma;\n"
"uniform float opacity;\n"
"uniform vec3 colorGains;\n"
"uniform int invertMode;\n"
"uniform int thresholdMode;\n"
"uniform float thresholdValue;\n"
"uniform int maskMode;\n"
"uniform vec2 maskCenter;\n"
"uniform float maskRadius;\n"
"uniform vec2 maskSize;\n"
"uniform float maskFeather;\n"
"uniform float aspectRatio;\n"
"\n"
"float smoothstep_safe(float edge0, float edge1, float x) {\n"
"    if (edge0 >= edge1) return step(edge0, x);\n"
"    return smoothstep(edge0, edge1, x);\n"
"}\n"
"\n"
"void main() {\n"
"    vec4 color = texture(ourTexture, TexCoord);\n"
"    color.rgb *= colorGains;\n"
"    \n"
"    if (grayscale == 1) {\n"
"        float gray = dot(color.rgb, vec3(0.299, 0.587, 0.114));\n"
"        color.rgb = vec3(gray);\n"
"    }\n"
"    \n"
"    color.rgb = ((color.rgb - 0.5) * contrast) + 0.5;\n"
"    color.rgb += brightness;\n"
"    color.rgb = pow(max(color.rgb, 0.0), vec3(1.0 / gamma));\n"
"    \n"
"    if (thresholdMode == 1) {\n"
"        float lum = dot(color.rgb, vec3(0.299, 0.587, 0.114));\n"
"        color.rgb = vec3(step(thresholdValue, lum));\n"
"    }\n"
"    \n"
"    if (invertMode == 1) {\n"
"        color.rgb = 1.0 - color.rgb;\n"
"    }\n"
"    \n"
"    // Aspect-corrected masking\n"
"    float maskAlpha = 1.0;\n"
"    if (maskMode > 0) {\n"
"        if (maskMode == 1 || maskMode == 3) {\n"
"            vec2 correctedCoord = TexCoord - maskCenter;\n"
"            correctedCoord.x *= aspectRatio;\n"
"            float dist = length(correctedCoord);\n"
"            \n"
"            if (maskMode == 1) {\n"
"                if (maskFeather > 0.0) {\n"
"                    maskAlpha = 1.0 - smoothstep_safe(maskRadius - maskFeather, maskRadius, dist);\n"
"                } else {\n"
"                    maskAlpha = step(dist, maskRadius);\n"
"                }\n"
"            } else {\n"
"                if (maskFeather > 0.0) {\n"
"                    maskAlpha = smoothstep_safe(maskRadius - maskFeather, maskRadius, dist);\n"
"                } else {\n"
"                    maskAlpha = 1.0 - step(dist, maskRadius);\n"
"                }\n"
"            }\n"
"        } else if (maskMode == 2) {\n"
"            vec2 halfSize = maskSize * 0.5;\n"
"            vec2 dist = abs(TexCoord - maskCenter) - halfSize;\n"
"            if (maskFeather > 0.0) {\n"
"                float rectDist = max(dist.x, dist.y);\n"
"                maskAlpha = 1.0 - smoothstep_safe(-maskFeather, 0.0, rectDist);\n"
"            } else {\n"
"                maskAlpha = step(max(dist.x, dist.y), 0.0);\n"
"            }\n"
"        }\n"
"    }\n"
"    \n"
"    color.rgb = clamp(color.rgb, 0.0, 1.0);\n"
"    FragColor = vec4(color.rgb, color.a * opacity * maskAlpha);\n"
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
    VideoUniformGrayscale = glGetUniformLocation(VideoShaderProgram, "grayscale");

    VideoUniformBrightness = glGetUniformLocation(VideoShaderProgram, "brightness");
    VideoUniformContrast = glGetUniformLocation(VideoShaderProgram, "contrast");
    VideoUniformGamma = glGetUniformLocation(VideoShaderProgram, "gamma");
    VideoUniformOpacity = glGetUniformLocation(VideoShaderProgram, "opacity");
    VideoUniformColorGains = glGetUniformLocation(VideoShaderProgram, "colorGains");
    VideoUniformInvertMode = glGetUniformLocation(VideoShaderProgram, "invertMode");
    VideoUniformThresholdMode = glGetUniformLocation(VideoShaderProgram, "thresholdMode");
    VideoUniformThresholdValue = glGetUniformLocation(VideoShaderProgram, "thresholdValue");

    VideoUniformMaskMode = glGetUniformLocation(VideoShaderProgram, "maskMode");
    VideoUniformMaskCenter = glGetUniformLocation(VideoShaderProgram, "maskCenter");
    VideoUniformMaskRadius = glGetUniformLocation(VideoShaderProgram, "maskRadius");
    VideoUniformMaskSize = glGetUniformLocation(VideoShaderProgram, "maskSize");
    VideoUniformMaskFeather = glGetUniformLocation(VideoShaderProgram, "maskFeather");    

    VideoUniformAspectRatio = glGetUniformLocation(VideoShaderProgram, "aspectRatio");
    
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
  
  // Set pixel store alignment based on linesize
  int alignment = 1;
  if (v->rgb_frame->linesize[0] % 8 == 0) alignment = 8;
  else if (v->rgb_frame->linesize[0] % 4 == 0) alignment = 4;
  else if (v->rgb_frame->linesize[0] % 2 == 0) alignment = 2;
  
  glPixelStorei(GL_UNPACK_ALIGNMENT, alignment);
  glPixelStorei(GL_UNPACK_ROW_LENGTH, v->rgb_frame->linesize[0] / 3);
  
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, v->width, v->height, 0, 
	       GL_RGB, GL_UNSIGNED_BYTE, v->rgb_frame->data[0]);
  
  glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);  // Reset
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

	// Normalize PTS relative to stream start
	v->current_time = (v->current_pts - v->stream_start_pts) *
	  av_q2d(v->time_base);
        
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
    
    // Render textured quad
    float modelview[16], projection[16];
    stimGetMatrix(STIM_MODELVIEW_MATRIX, modelview);
    stimGetMatrix(STIM_PROJECTION_MATRIX, projection);
    
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    
    glUseProgram(VideoShaderProgram);
    glUniformMatrix4fv(VideoUniformModelview, 1, GL_FALSE, modelview);
    glUniformMatrix4fv(VideoUniformProjection, 1, GL_FALSE, projection);
    glUniform1i(VideoUniformGrayscale, v->grayscale_mode); 

    glUniform1f(VideoUniformBrightness, v->brightness);
    glUniform1f(VideoUniformContrast, v->contrast);
    glUniform1f(VideoUniformGamma, v->gamma);
    glUniform1f(VideoUniformOpacity, v->opacity);
    glUniform3f(VideoUniformColorGains, v->red_gain, v->green_gain, v->blue_gain);
    glUniform1i(VideoUniformInvertMode, v->invert_mode);
    glUniform1i(VideoUniformThresholdMode, v->threshold_mode);
    glUniform1f(VideoUniformThresholdValue, v->threshold_value);
 
    glUniform1i(VideoUniformMaskMode, v->mask_mode);
    glUniform2f(VideoUniformMaskCenter, v->mask_center_x, v->mask_center_y);
    glUniform1f(VideoUniformMaskRadius, v->mask_radius);
    glUniform2f(VideoUniformMaskSize, v->mask_width, v->mask_height);
    glUniform1f(VideoUniformMaskFeather, v->mask_feather);

    glUniform1f(VideoUniformAspectRatio, v->aspect_ratio);
 
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
    
    // Always execute timer script if defined (existing behavior)
    if (v->timer_script) {
        sendTclCommand(v->timer_script);
    }
    
    // If paused, don't do any playback processing
    if (v->paused) {
        return;
    }
    
    // Handle EOF reached - one-shot EOF callback logic
    if (v->eof_reached) {
        if (!v->repeat_mode) {
            // Video has ended and not repeating - trigger EOF callback once
            if (v->eof_script && !v->eof_fired) {
                sendTclCommand(v->eof_script);
                v->eof_fired = 1;  // Ensure one-shot behavior
            }
            // Stay at EOF - no further processing needed
            return;
        }
        
        // Handle repeat mode - restart the video
        av_seek_frame(v->format_ctx, v->video_stream_idx, 0, AVSEEK_FLAG_BACKWARD);
        avcodec_flush_buffers(v->codec_ctx);
        v->eof_reached = 0;
        v->eof_fired = 0;      // Reset for potential future EOF
        v->current_time = 0.0;
        v->current_pts = 0;
        v->video_start_time = getStimTime() / 1000.0;  // Reset timing
        v->frames_decoded = 0;
        
        // Decode first frame immediately after restart
        if (decode_next_frame(v)) {
            upload_frame_to_texture(v);
            v->frames_decoded = 1;
        }
        
        kickAnimation();
        return;
    }
    
    // Normal playback processing - get current stimulus time in seconds
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
                // Hit end of file - EOF will be handled on next timer tick
                break;
            }
        }
        
        // Update target time for next frame
        v->target_frame_time = v->video_start_time + (v->frames_decoded / v->frame_rate);
        
        if (frames_to_catch_up > 1) {
            fprintf(stderr, "Video catchup: decoded %d frames\n", frames_to_catch_up);
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
    if (v->eof_script) free(v->eof_script);
    
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
    v->eof_fired = 0;
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
    v->aspect_ratio = (float)v->width / (float)v->height;    
    v->time_base = video_stream->time_base;
    v->duration = video_stream->duration * av_q2d(v->time_base);
    v->frame_rate = av_q2d(video_stream->r_frame_rate);

    // Allocate frames
    v->frame = av_frame_alloc();
    v->rgb_frame = av_frame_alloc();
    v->packet = av_packet_alloc();

    // Set up RGB frame
    v->rgb_frame->format = AV_PIX_FMT_RGB24;
    v->rgb_frame->width = v->width;
    v->rgb_frame->height = v->height;
    av_image_alloc(v->rgb_frame->data, v->rgb_frame->linesize, 
		   v->width, v->height, AV_PIX_FMT_RGB24, 1);

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

    int64_t start_time = video_stream->start_time;
    if (start_time == AV_NOPTS_VALUE) {
      start_time = 0;
    }
    v->stream_start_pts = start_time;

    
    v->target_frame_time = 0.0;
    v->frames_decoded = 0;
    v->needs_frame_update = 1;
    v->timer_script = NULL;
    v->eof_script = NULL;
    v->eof_fired = 0;
    v->grayscale_mode = 0;

    v->brightness = 0.0f;      // No brightness adjustment
    v->contrast = 1.0f;        // Normal contrast  
    v->gamma = 1.0f;           // No gamma correction
    v->opacity = 1.0f;         // Fully opaque
    v->red_gain = 1.0f;        // Normal red
    v->green_gain = 1.0f;      // Normal green
    v->blue_gain = 1.0f;       // Normal blue
    v->invert_mode = 0;        // No inversion
    v->threshold_mode = 0;     // No thresholding
    v->threshold_value = 0.5f; // 50% threshold
 
    v->mask_mode = 0;          // No masking
    v->mask_center_x = 0.5f;   // Center of video
    v->mask_center_y = 0.5f;   // Center of video  
    v->mask_radius = 0.2f;     // 20% of video radius
    v->mask_width = 0.4f;      // 40% of video width
    v->mask_height = 0.3f;     // 30% of video height
    v->mask_feather = 0.05f;   // 5% feathering for soft edges
    
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

// Add this function before the existing Tcl command implementations

static int videoinfoCmd(ClientData clientData, Tcl_Interp *interp,
			int argc, char *argv[]) {
  AVFormatContext *format_ctx = NULL;
  AVStream *video_stream = NULL;
  const AVCodec *codec = NULL;
  int video_stream_idx;
  int ret;
  Tcl_Obj *dictObj;
  
  if (argc < 2) {
    Tcl_AppendResult(interp, "usage: ", argv[0], " videofile", NULL);
    return TCL_ERROR;
  }
  
  // Open video file
  ret = avformat_open_input(&format_ctx, argv[1], NULL, NULL);
  if (ret < 0) {
    char error_buf[256];
    av_strerror(ret, error_buf, sizeof(error_buf));
    Tcl_AppendResult(interp, "error opening video file: ", error_buf, NULL);
    return TCL_ERROR;
  }
  
  ret = avformat_find_stream_info(format_ctx, NULL);
  if (ret < 0) {
    char error_buf[256];
    av_strerror(ret, error_buf, sizeof(error_buf));
    Tcl_AppendResult(interp, "error finding stream info: ", error_buf, NULL);
    avformat_close_input(&format_ctx);
    return TCL_ERROR;
  }
  
  // Find video stream
  video_stream_idx = av_find_best_stream(format_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);
  if (video_stream_idx < 0) {
    Tcl_AppendResult(interp, "no video stream found", NULL);
    avformat_close_input(&format_ctx);
    return TCL_ERROR;
  }
  
  video_stream = format_ctx->streams[video_stream_idx];
  
  // Create Tcl dictionary with video information
  dictObj = Tcl_NewDictObj();
  
  // Basic video properties
  Tcl_DictObjPut(interp, dictObj, Tcl_NewStringObj("width", -1), 
		 Tcl_NewIntObj(video_stream->codecpar->width));
  Tcl_DictObjPut(interp, dictObj, Tcl_NewStringObj("height", -1), 
		 Tcl_NewIntObj(video_stream->codecpar->height));
  
  // Duration in seconds
  double duration = video_stream->duration * av_q2d(video_stream->time_base);
  Tcl_DictObjPut(interp, dictObj, Tcl_NewStringObj("duration", -1), 
		 Tcl_NewDoubleObj(duration));
  
  // Frame rate
  double frame_rate = av_q2d(video_stream->r_frame_rate);
  Tcl_DictObjPut(interp, dictObj, Tcl_NewStringObj("framerate", -1), 
		 Tcl_NewDoubleObj(frame_rate));
  
  // Total frame count (approximate)
  int64_t frame_count = video_stream->nb_frames;
  if (frame_count <= 0) {
    // Estimate from duration and frame rate
    frame_count = (int64_t)(duration * frame_rate);
  }
  Tcl_DictObjPut(interp, dictObj, Tcl_NewStringObj("frames", -1), 
		 Tcl_NewWideIntObj(frame_count));
  
  // Codec information
  Tcl_DictObjPut(interp, dictObj, Tcl_NewStringObj("codec", -1), 
		 Tcl_NewStringObj(codec->name, -1));
  Tcl_DictObjPut(interp, dictObj, Tcl_NewStringObj("codec_long_name", -1), 
		 Tcl_NewStringObj(codec->long_name, -1));
  
  // Pixel format
  const char *pix_fmt_name = av_get_pix_fmt_name(video_stream->codecpar->format);
  if (pix_fmt_name) {
    Tcl_DictObjPut(interp, dictObj, Tcl_NewStringObj("pixel_format", -1), 
		   Tcl_NewStringObj(pix_fmt_name, -1));
  }
  
  // Bitrate (if available)
  if (video_stream->codecpar->bit_rate > 0) {
    Tcl_DictObjPut(interp, dictObj, Tcl_NewStringObj("bitrate", -1), 
		   Tcl_NewWideIntObj(video_stream->codecpar->bit_rate));
  }
  
  // File size
  int64_t file_size = avio_size(format_ctx->pb);
  if (file_size >= 0) {
    Tcl_DictObjPut(interp, dictObj, Tcl_NewStringObj("filesize", -1), 
		   Tcl_NewWideIntObj(file_size));
  }
  
  // Container format
  Tcl_DictObjPut(interp, dictObj, Tcl_NewStringObj("format", -1), 
		 Tcl_NewStringObj(format_ctx->iformat->name, -1));
  Tcl_DictObjPut(interp, dictObj, Tcl_NewStringObj("format_long_name", -1), 
		 Tcl_NewStringObj(format_ctx->iformat->long_name, -1));
  
  // Audio stream count (might be useful)
  int audio_streams = 0;
  for (int i = 0; i < format_ctx->nb_streams; i++) {
    if (format_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
      audio_streams++;
    }
  }
  Tcl_DictObjPut(interp, dictObj, Tcl_NewStringObj("audio_streams", -1), 
		 Tcl_NewIntObj(audio_streams));
  
  // Clean up
  avformat_close_input(&format_ctx);
  
  // Return the dictionary
  Tcl_SetObjResult(interp, dictObj);
  return TCL_OK;
}

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

    if ((id = resolveObjId(interp, (ObjNameInfo *)OL_NAMEINFO(olist),
			   argv[1], VideoID, "video")) < 0)
      return TCL_ERROR;
    
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

    if ((id = resolveObjId(interp, (ObjNameInfo *)OL_NAMEINFO(olist),
			   argv[1], VideoID, "video")) < 0)
      return TCL_ERROR;
    
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

    if ((id = resolveObjId(interp, (ObjNameInfo *)OL_NAMEINFO(olist),
			   argv[1], VideoID, "video")) < 0)
      return TCL_ERROR;
    
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
    
    if ((id = resolveObjId(interp, (ObjNameInfo *)OL_NAMEINFO(olist),
			   argv[1], VideoID, "video")) < 0)
      return TCL_ERROR;
    
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

 // Add this new Tcl command:
static int videoeofcallbackCmd(ClientData clientData, Tcl_Interp *interp,
                              int argc, char *argv[]) {
    OBJ_LIST *olist = (OBJ_LIST *) clientData;
    FFMPEG_VIDEO *v;
    int id;

    if (argc < 2) {
        Tcl_AppendResult(interp, "usage: ", argv[0], " id [script]", NULL);
        return TCL_ERROR;
    }

    if ((id = resolveObjId(interp, (ObjNameInfo *)OL_NAMEINFO(olist),
			   argv[1], VideoID, "video")) < 0)
      return TCL_ERROR;
    
    v = GR_CLIENTDATA(OL_OBJ(olist, id));

    if (argc == 2) {
        // Return current EOF callback script
        if (v->eof_script) {
            Tcl_SetResult(interp, v->eof_script, TCL_VOLATILE);
        }
        return TCL_OK;
    }

    // Set new EOF callback script
    if (v->eof_script) {
        free(v->eof_script);
        v->eof_script = NULL;
    }

    if (strlen(argv[2]) > 0) {
        v->eof_script = strdup(argv[2]);
    }

    return TCL_OK;
}

static int videograyscaleCmd(ClientData clientData, Tcl_Interp *interp,
                            int argc, char *argv[]) {
    OBJ_LIST *olist = (OBJ_LIST *) clientData;
    FFMPEG_VIDEO *v;
    int id, grayscale;

    if (argc < 2) {
        Tcl_AppendResult(interp, "usage: ", argv[0], " id [grayscale(0/1)]", NULL);
        return TCL_ERROR;
    }

    if ((id = resolveObjId(interp, (ObjNameInfo *)OL_NAMEINFO(olist),
			   argv[1], VideoID, "video")) < 0)
      return TCL_ERROR;
    
    v = GR_CLIENTDATA(OL_OBJ(olist, id));
    
    if (argc == 2) {
        // Return current grayscale mode
        Tcl_SetObjResult(interp, Tcl_NewIntObj(v->grayscale_mode));
        return TCL_OK;
    }
    
    if (Tcl_GetInt(interp, argv[2], &grayscale) != TCL_OK) return TCL_ERROR;
    
    v->grayscale_mode = grayscale ? 1 : 0;
    
    return TCL_OK;
}

// Brightness control (-1.0 to 1.0)
static int videobrightnessCmd(ClientData clientData, Tcl_Interp *interp,
                             int argc, char *argv[]) {
    OBJ_LIST *olist = (OBJ_LIST *) clientData;
    FFMPEG_VIDEO *v;
    int id;
    double brightness;

    if (argc < 2) {
        Tcl_AppendResult(interp, "usage: ", argv[0], " id [brightness]", NULL);
        return TCL_ERROR;
    }

    if ((id = resolveObjId(interp, (ObjNameInfo *)OL_NAMEINFO(olist),
			   argv[1], VideoID, "video")) < 0)
      return TCL_ERROR;
    
    v = GR_CLIENTDATA(OL_OBJ(olist, id));
    
    if (argc == 2) {
        Tcl_SetObjResult(interp, Tcl_NewDoubleObj(v->brightness));
        return TCL_OK;
    }
    
    if (Tcl_GetDouble(interp, argv[2], &brightness) != TCL_OK) return TCL_ERROR;
    v->brightness = (float)fmax(-1.0, fmin(1.0, brightness)); // Clamp to valid range
    
    return TCL_OK;
}

// Contrast control (0.0 to 2.0)
static int videocontrastCmd(ClientData clientData, Tcl_Interp *interp,
                           int argc, char *argv[]) {
    OBJ_LIST *olist = (OBJ_LIST *) clientData;
    FFMPEG_VIDEO *v;
    int id;
    double contrast;

    if (argc < 2) {
        Tcl_AppendResult(interp, "usage: ", argv[0], " id [contrast]", NULL);
        return TCL_ERROR;
    }

    if ((id = resolveObjId(interp, (ObjNameInfo *)OL_NAMEINFO(olist),
			   argv[1], VideoID, "video")) < 0)
      return TCL_ERROR;
    
    v = GR_CLIENTDATA(OL_OBJ(olist, id));
    
    if (argc == 2) {
        Tcl_SetObjResult(interp, Tcl_NewDoubleObj(v->contrast));
        return TCL_OK;
    }
    
    if (Tcl_GetDouble(interp, argv[2], &contrast) != TCL_OK) return TCL_ERROR;
    v->contrast = (float)fmax(0.0, fmin(3.0, contrast));
    
    return TCL_OK;
}

// Gamma control (0.1 to 3.0)
static int videogammaCmd(ClientData clientData, Tcl_Interp *interp,
                        int argc, char *argv[]) {
    OBJ_LIST *olist = (OBJ_LIST *) clientData;
    FFMPEG_VIDEO *v;
    int id;
    double gamma;

    if (argc < 2) {
        Tcl_AppendResult(interp, "usage: ", argv[0], " id [gamma]", NULL);
        return TCL_ERROR;
    }

    if ((id = resolveObjId(interp, (ObjNameInfo *)OL_NAMEINFO(olist),
			   argv[1], VideoID, "video")) < 0)
      return TCL_ERROR;
    
    v = GR_CLIENTDATA(OL_OBJ(olist, id));
    
    if (argc == 2) {
        Tcl_SetObjResult(interp, Tcl_NewDoubleObj(v->gamma));
        return TCL_OK;
    }
    
    if (Tcl_GetDouble(interp, argv[2], &gamma) != TCL_OK) return TCL_ERROR;
    v->gamma = (float)fmax(0.1, fmin(3.0, gamma));
    
    return TCL_OK;
}

// Opacity control (0.0 to 1.0)
static int videoopacityCmd(ClientData clientData, Tcl_Interp *interp,
                          int argc, char *argv[]) {
    OBJ_LIST *olist = (OBJ_LIST *) clientData;
    FFMPEG_VIDEO *v;
    int id;
    double opacity;

    if (argc < 2) {
        Tcl_AppendResult(interp, "usage: ", argv[0], " id [opacity]", NULL);
        return TCL_ERROR;
    }

    if ((id = resolveObjId(interp, (ObjNameInfo *)OL_NAMEINFO(olist),
			   argv[1], VideoID, "video")) < 0)
      return TCL_ERROR;
    
    v = GR_CLIENTDATA(OL_OBJ(olist, id));
    
    if (argc == 2) {
        Tcl_SetObjResult(interp, Tcl_NewDoubleObj(v->opacity));
        return TCL_OK;
    }
    
    if (Tcl_GetDouble(interp, argv[2], &opacity) != TCL_OK) return TCL_ERROR;
    v->opacity = (float)fmax(0.0, fmin(1.0, opacity));
    
    return TCL_OK;
}

// Color gains control (RGB gains, 0.0 to 2.0 each)
static int videocolorgainsCmd(ClientData clientData, Tcl_Interp *interp,
                             int argc, char *argv[]) {
    OBJ_LIST *olist = (OBJ_LIST *) clientData;
    FFMPEG_VIDEO *v;
    int id;
    double red, green, blue;

    if (argc < 2) {
        Tcl_AppendResult(interp, "usage: ", argv[0], " id [red green blue]", NULL);
        return TCL_ERROR;
    }

    if ((id = resolveObjId(interp, (ObjNameInfo *)OL_NAMEINFO(olist),
			   argv[1], VideoID, "video")) < 0)
      return TCL_ERROR;
    
    v = GR_CLIENTDATA(OL_OBJ(olist, id));
    
    if (argc == 2) {
        Tcl_Obj *listObj = Tcl_NewListObj(0, NULL);
        Tcl_ListObjAppendElement(interp, listObj, Tcl_NewDoubleObj(v->red_gain));
        Tcl_ListObjAppendElement(interp, listObj, Tcl_NewDoubleObj(v->green_gain));
        Tcl_ListObjAppendElement(interp, listObj, Tcl_NewDoubleObj(v->blue_gain));
        Tcl_SetObjResult(interp, listObj);
        return TCL_OK;
    }
    
    if (argc < 5) {
        Tcl_AppendResult(interp, "usage: ", argv[0], " id red green blue", NULL);
        return TCL_ERROR;
    }
    
    if (Tcl_GetDouble(interp, argv[2], &red) != TCL_OK) return TCL_ERROR;
    if (Tcl_GetDouble(interp, argv[3], &green) != TCL_OK) return TCL_ERROR;
    if (Tcl_GetDouble(interp, argv[4], &blue) != TCL_OK) return TCL_ERROR;
    
    v->red_gain = (float)fmax(0.0, fmin(2.0, red));
    v->green_gain = (float)fmax(0.0, fmin(2.0, green));
    v->blue_gain = (float)fmax(0.0, fmin(2.0, blue));
    
    return TCL_OK;
}

// Invert control (0/1)
static int videoinvertCmd(ClientData clientData, Tcl_Interp *interp,
                         int argc, char *argv[]) {
    OBJ_LIST *olist = (OBJ_LIST *) clientData;
    FFMPEG_VIDEO *v;
    int id, invert;

    if (argc < 2) {
        Tcl_AppendResult(interp, "usage: ", argv[0], " id [invert(0/1)]", NULL);
        return TCL_ERROR;
    }

    if ((id = resolveObjId(interp, (ObjNameInfo *)OL_NAMEINFO(olist),
			   argv[1], VideoID, "video")) < 0)
      return TCL_ERROR;
    
    v = GR_CLIENTDATA(OL_OBJ(olist, id));
    
    if (argc == 2) {
        Tcl_SetObjResult(interp, Tcl_NewIntObj(v->invert_mode));
        return TCL_OK;
    }
    
    if (Tcl_GetInt(interp, argv[2], &invert) != TCL_OK) return TCL_ERROR;
    v->invert_mode = invert ? 1 : 0;
    
    return TCL_OK;
}

// Threshold control (enable and value)
static int videothresholdCmd(ClientData clientData, Tcl_Interp *interp,
                            int argc, char *argv[]) {
    OBJ_LIST *olist = (OBJ_LIST *) clientData;
    FFMPEG_VIDEO *v;
    int id, enable;
    double threshold;

    if (argc < 2) {
        Tcl_AppendResult(interp, "usage: ", argv[0], " id [enable(0/1) threshold]", NULL);
        return TCL_ERROR;
    }

    if ((id = resolveObjId(interp, (ObjNameInfo *)OL_NAMEINFO(olist),
			   argv[1], VideoID, "video")) < 0)
      return TCL_ERROR;
    
    v = GR_CLIENTDATA(OL_OBJ(olist, id));
    
    if (argc == 2) {
        Tcl_Obj *listObj = Tcl_NewListObj(0, NULL);
        Tcl_ListObjAppendElement(interp, listObj, Tcl_NewIntObj(v->threshold_mode));
        Tcl_ListObjAppendElement(interp, listObj, Tcl_NewDoubleObj(v->threshold_value));
        Tcl_SetObjResult(interp, listObj);
        return TCL_OK;
    }
    
    if (argc < 4) {
        Tcl_AppendResult(interp, "usage: ", argv[0], " id enable threshold", NULL);
        return TCL_ERROR;
    }
    
    if (Tcl_GetInt(interp, argv[2], &enable) != TCL_OK) return TCL_ERROR;
    if (Tcl_GetDouble(interp, argv[3], &threshold) != TCL_OK) return TCL_ERROR;
    
    v->threshold_mode = enable ? 1 : 0;
    v->threshold_value = (float)fmax(0.0, fmin(1.0, threshold));
    
    return TCL_OK;
}

// Gaze-contingent mask control
static int videomaskCmd(ClientData clientData, Tcl_Interp *interp,
                       int argc, char *argv[]) {
    OBJ_LIST *olist = (OBJ_LIST *) clientData;
    FFMPEG_VIDEO *v;
    int id, mode;
    double centerX, centerY, radius, width, height, feather;

    if (argc < 2) {
        Tcl_AppendResult(interp, "usage: ", argv[0],
			 " id [mode centerX centerY radius/width height feather]",
			 NULL);
        return TCL_ERROR;
    }

    if ((id = resolveObjId(interp, (ObjNameInfo *)OL_NAMEINFO(olist),
			   argv[1], VideoID, "video")) < 0)
      return TCL_ERROR;
    
    v = GR_CLIENTDATA(OL_OBJ(olist, id));
    
    if (argc == 2) {
        // Return current mask settings
        Tcl_Obj *listObj = Tcl_NewListObj(0, NULL);
        Tcl_ListObjAppendElement(interp, listObj, Tcl_NewIntObj(v->mask_mode));
        Tcl_ListObjAppendElement(interp, listObj, Tcl_NewDoubleObj(v->mask_center_x));
        Tcl_ListObjAppendElement(interp, listObj, Tcl_NewDoubleObj(v->mask_center_y));
        Tcl_ListObjAppendElement(interp, listObj, Tcl_NewDoubleObj(v->mask_radius));
        Tcl_ListObjAppendElement(interp, listObj, Tcl_NewDoubleObj(v->mask_width));
        Tcl_ListObjAppendElement(interp, listObj, Tcl_NewDoubleObj(v->mask_height));
        Tcl_ListObjAppendElement(interp, listObj, Tcl_NewDoubleObj(v->mask_feather));
        Tcl_SetObjResult(interp, listObj);
        return TCL_OK;
    }
    
    if (argc < 8) {
        Tcl_AppendResult(interp, "usage: ", argv[0], " id mode centerX centerY radius/width height feather", NULL);
        return TCL_ERROR;
    }
    
    if (Tcl_GetInt(interp, argv[2], &mode) != TCL_OK) return TCL_ERROR;
    if (Tcl_GetDouble(interp, argv[3], &centerX) != TCL_OK) return TCL_ERROR;
    if (Tcl_GetDouble(interp, argv[4], &centerY) != TCL_OK) return TCL_ERROR;
    if (Tcl_GetDouble(interp, argv[5], &radius) != TCL_OK) return TCL_ERROR;
    if (Tcl_GetDouble(interp, argv[6], &height) != TCL_OK) return TCL_ERROR;
    if (Tcl_GetDouble(interp, argv[7], &feather) != TCL_OK) return TCL_ERROR;
    
    v->mask_mode = fmax(0, fmin(3, mode));
    v->mask_center_x = (float)fmax(0.0, fmin(1.0, centerX));
    v->mask_center_y = (float)fmax(0.0, fmin(1.0, centerY));
    v->mask_radius = (float)fmax(0.0, fmin(1.0, radius));    // For circular or width for rect
    v->mask_width = (float)fmax(0.0, fmin(1.0, radius));     // Store width in radius for rect
    v->mask_height = (float)fmax(0.0, fmin(1.0, height));
    v->mask_feather = (float)fmax(0.0, fmin(0.5, feather));
    
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

    Tcl_CreateCommand(interp, "videoInfo", (Tcl_CmdProc *) videoinfoCmd,
		      (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);

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
    Tcl_CreateCommand(interp, "videoEofCallback", (Tcl_CmdProc *) videoeofcallbackCmd,
		      (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
    Tcl_CreateCommand(interp, "videoGrayscale", (Tcl_CmdProc *) videograyscaleCmd,
		      (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
    Tcl_CreateCommand(interp, "videoBrightness", (Tcl_CmdProc *) videobrightnessCmd,
                      (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
    Tcl_CreateCommand(interp, "videoContrast", (Tcl_CmdProc *) videocontrastCmd,
                      (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
    Tcl_CreateCommand(interp, "videoGamma", (Tcl_CmdProc *) videogammaCmd,
                      (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
    Tcl_CreateCommand(interp, "videoOpacity", (Tcl_CmdProc *) videoopacityCmd,
                      (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
    Tcl_CreateCommand(interp, "videoColorGains", (Tcl_CmdProc *) videocolorgainsCmd,
                      (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
    Tcl_CreateCommand(interp, "videoInvert", (Tcl_CmdProc *) videoinvertCmd,
                      (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
    Tcl_CreateCommand(interp, "videoThreshold", (Tcl_CmdProc *) videothresholdCmd,
                      (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
    Tcl_CreateCommand(interp, "videoMask", (Tcl_CmdProc *) videomaskCmd,
                      (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);

    Tcl_Eval(interp, 
	     "proc videoAsset {filename} {\n"
	     "  return [video [assetFind $filename]]\n"
	     "}\n");
    
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
