/*
 * image.c
 *  Simplified image display module using OpenGL
 *  Based on video.c but specialized for still images
 *  Uses stb_image for loading various image formats
 *
 *  Supports:
 *   - Loading images from files (PNG, JPEG, TGA, BMP, etc.)
 *   - Loading images from raw byte arrays
 *   - Loading images from DYN_LIST data structures
 *   - Shared texture pool for efficient multi-instance rendering
 *   - Real-time image processing via shader uniforms
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

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <tcl.h>

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <stim2.h>
#include <prmutil.h>
#include "objname.h"
#include <df.h>
#include <tcl_dl.h>

/****************************************************************/
/*                   Texture Pool Structures                    */
/****************************************************************/

#define MAX_IMAGE_TEXTURES 4096

typedef struct _image_texture {
  int in_use;                   // Is this slot active?
  int width;
  int height;
  int channels;
  float aspect_ratio;
  GLuint texid;                 // OpenGL texture ID
  int filter;                   // GL_LINEAR or GL_NEAREST
} IMAGE_TEXTURE;

typedef struct _image_texture_pool {
  int count;
  IMAGE_TEXTURE textures[MAX_IMAGE_TEXTURES];
} IMAGE_TEXTURE_POOL;

static IMAGE_TEXTURE_POOL TexturePool = {0};

/****************************************************************/
/*                  Graphics Object Structure                   */
/****************************************************************/

typedef struct _image_obj {
  int texture_id;               // Index into TexturePool
  float aspect_ratio;           // Copied from texture for vertex generation
  
  // Display state
  int visible;
  
  // OpenGL resources (per-object, for geometry)
  GLuint vertex_buffer;
  GLuint vao;
  
  // Image processing parameters (per-object shader uniforms)
  int grayscale_mode;           // 0=color, 1=grayscale
  float brightness;             // -1.0 to 1.0 (additive)
  float contrast;               // 0.0 to 3.0 (1.0 = normal)
  float gamma;                  // 0.1 to 3.0 (1.0 = normal)
  float opacity;                // 0.0 to 1.0 (for alpha blending)
  
  // Color channel manipulation
  float red_gain;               // 0.0 to 2.0 (1.0 = normal)
  float green_gain;             // 0.0 to 2.0 (1.0 = normal) 
  float blue_gain;              // 0.0 to 2.0 (1.0 = normal)
  
  // Special effects
  int invert_mode;              // 0=normal, 1=invert colors
  int threshold_mode;           // 0=off, 1=binary threshold
  float threshold_value;        // 0.0 to 1.0 for threshold
  
  // Gaze-contingent masking (normalized coordinates 0.0-1.0)
  int mask_mode;                // 0=off, 1=circular window, 2=rectangular window, 3=inverse
  float mask_center_x;          // Center X (0.0 = left, 1.0 = right)
  float mask_center_y;          // Center Y (0.0 = top, 1.0 = bottom) 
  float mask_radius;            // Radius for circular mask (0.0-1.0)
  float mask_width;             // Width for rectangular mask (0.0-1.0)
  float mask_height;            // Height for rectangular mask (0.0-1.0)
  float mask_feather;           // Soft edge width (0.0 = hard, 0.1 = 10% feather)
  
} IMAGE_OBJ;

static int ImageID = -1;  /* unique image object id */
static GLuint ImageShaderProgram = 0;  /* shared shader program */
static GLint ImageUniformTexture = -1;
static GLint ImageUniformModelview = -1;
static GLint ImageUniformProjection = -1;
static GLint ImageUniformGrayscale = -1;

static GLint ImageUniformBrightness = -1;
static GLint ImageUniformContrast = -1;
static GLint ImageUniformGamma = -1;
static GLint ImageUniformOpacity = -1;
static GLint ImageUniformColorGains = -1;
static GLint ImageUniformInvertMode = -1;
static GLint ImageUniformThresholdMode = -1;
static GLint ImageUniformThresholdValue = -1;

static GLint ImageUniformMaskMode = -1;
static GLint ImageUniformMaskCenter = -1;
static GLint ImageUniformMaskRadius = -1;
static GLint ImageUniformMaskSize = -1;
static GLint ImageUniformMaskFeather = -1;

static GLint ImageUniformAspectRatio = -1;

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
"// Image processing controls\n"
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
"uniform float aspectRatio;\n"
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
"            correctedCoord.x *= aspectRatio;\n"
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
"        } else if (maskMode == 2) { // Rectangular window\n"
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

/****************************************************************/
/*                   Texture Pool Functions                     */
/****************************************************************/

// Find a free slot in the texture pool
static int texture_pool_find_free_slot(void) {
    for (int i = 0; i < MAX_IMAGE_TEXTURES; i++) {
        if (!TexturePool.textures[i].in_use) {
            return i;
        }
    }
    return -1;
}

// Upload pixel data to a texture in the pool
static int texture_pool_upload(int slot, int width, int height, int channels,
                               unsigned char *pixels, int filter) {
    IMAGE_TEXTURE *tex = &TexturePool.textures[slot];
    
    tex->width = width;
    tex->height = height;
    tex->channels = channels;
    tex->aspect_ratio = (float)width / (float)height;
    tex->filter = filter;
    tex->in_use = 1;
    
    glGenTextures(1, &tex->texid);
    glBindTexture(GL_TEXTURE_2D, tex->texid);
    
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    
    GLenum format = GL_RGB;
    if (channels == 4) format = GL_RGBA;
    else if (channels == 1) format = GL_RED;

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);    
    glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, 
                 format, GL_UNSIGNED_BYTE, pixels);

    // For grayscale images, replicate red channel to G and B for correct display
    // Texture swizzle supported in GL 3.3+ and GLES 3.0+
    if (channels == 1) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_G, GL_RED);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_B, GL_RED);
    }
    
    glBindTexture(GL_TEXTURE_2D, 0);
    
    if (slot >= TexturePool.count) {
        TexturePool.count = slot + 1;
    }
    
    return slot;
}

// Free a single texture from the pool
static void texture_pool_free(int slot) {
    if (slot < 0 || slot >= MAX_IMAGE_TEXTURES) return;
    
    IMAGE_TEXTURE *tex = &TexturePool.textures[slot];
    if (tex->in_use && tex->texid) {
        glDeleteTextures(1, &tex->texid);
    }
    memset(tex, 0, sizeof(IMAGE_TEXTURE));
}

// Free all textures in the pool
static void texture_pool_reset(void) {
    for (int i = 0; i < TexturePool.count; i++) {
        texture_pool_free(i);
    }
    TexturePool.count = 0;
}

/****************************************************************/
/*              Texture Loading Functions                       */
/****************************************************************/

// Load texture from file using stb_image
static int texture_load_from_file(const char *filename, int filter) {
    int width, height, channels;
    unsigned char *data = stbi_load(filename, &width, &height, &channels, 0);
    if (!data) {
        fprintf(getConsoleFP(), "Failed to load image from file: %s\n", stbi_failure_reason());
        return -1;
    }
    
    int slot = texture_pool_find_free_slot();
    if (slot < 0) {
        fprintf(getConsoleFP(), "Texture pool full\n");
        stbi_image_free(data);
        return -1;
    }
    
    texture_pool_upload(slot, width, height, channels, data, filter);
    stbi_image_free(data);
    
    return slot;
}

// Load texture from encoded image data (PNG, JPEG, etc.) in memory
static int texture_load_from_memory(const unsigned char *buffer, int len, int filter) {
    int width, height, channels;
    unsigned char *data = stbi_load_from_memory(buffer, len, &width, &height, &channels, 0);
    if (!data) {
        fprintf(getConsoleFP(), "Failed to load image from memory: %s\n", stbi_failure_reason());
        return -1;
    }
    
    int slot = texture_pool_find_free_slot();
    if (slot < 0) {
        fprintf(getConsoleFP(), "Texture pool full\n");
        stbi_image_free(data);
        return -1;
    }
    
    texture_pool_upload(slot, width, height, channels, data, filter);
    stbi_image_free(data);
    
    return slot;
}

// Load texture from raw pixel data (no decoding)
static int texture_load_from_raw(const unsigned char *pixels, int width, int height, 
                                  int channels, int filter) {
    int slot = texture_pool_find_free_slot();
    if (slot < 0) {
        fprintf(getConsoleFP(), "Texture pool full\n");
        return -1;
    }
    
    texture_pool_upload(slot, width, height, channels, (unsigned char *)pixels, filter);
    return slot;
}

// Load texture from DYN_LIST
static int texture_load_from_dynlist(DYN_LIST *dl, int width, int height, int filter) {
    int slot = texture_pool_find_free_slot();
    if (slot < 0) {
        fprintf(getConsoleFP(), "Texture pool full\n");
        return -1;
    }
    
    int n = DYN_LIST_N(dl);
    int size = width * height;
    int channels;
    
    // Determine channels from data size
    if (n == size) channels = 1;
    else if (n == size * 3) channels = 3;
    else if (n == size * 4) channels = 4;
    else {
        fprintf(getConsoleFP(), "Invalid DYN_LIST size for %dx%d image\n", width, height);
        return -1;
    }
    
    unsigned char *pixels = NULL;
    int datatype = DYN_LIST_DATATYPE(dl);
    
    if (datatype == DF_CHAR) {
        // Direct copy for byte data
        pixels = (unsigned char *)DYN_LIST_VALS(dl);
        texture_pool_upload(slot, width, height, channels, pixels, filter);
    }
    else if (datatype == DF_FLOAT) {
        // Convert float (0.0-1.0) to unsigned byte (0-255)
        float *fvals = (float *)DYN_LIST_VALS(dl);
        pixels = (unsigned char *)malloc(n);
        if (!pixels) return -1;
        
        for (int i = 0; i < n; i++) {
            float v = fvals[i];
            if (v < 0.0f) v = 0.0f;
            if (v > 1.0f) v = 1.0f;
            pixels[i] = (unsigned char)(v * 255.0f);
        }
        
        texture_pool_upload(slot, width, height, channels, pixels, filter);
        free(pixels);
    }
    else if (datatype == DF_LONG) {
        // Clamp long to 0-255
        long *lvals = (long *)DYN_LIST_VALS(dl);
        pixels = (unsigned char *)malloc(n);
        if (!pixels) return -1;
        
        for (int i = 0; i < n; i++) {
            long v = lvals[i];
            if (v < 0) v = 0;
            if (v > 255) v = 255;
            pixels[i] = (unsigned char)v;
        }
        
        texture_pool_upload(slot, width, height, channels, pixels, filter);
        free(pixels);
    }
    else {
        fprintf(getConsoleFP(), "Unsupported DYN_LIST datatype: %d\n", datatype);
        return -1;
    }
    
    return slot;
}

// Helper: detect if data looks like encoded image
static int is_image_data(const unsigned char *data, int len) {
    int width, height, channels;
    return stbi_info_from_memory(data, len, &width, &height, &channels);
}

// Helper: detect if string is valid image file
static int is_image_file(const char *filename) {
    int width, height, channels;
    return stbi_info(filename, &width, &height, &channels);
}

/****************************************************************/
/*                    Shader Functions                          */
/****************************************************************/

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

static int create_image_shader_program(void) {
    GLuint vertex_shader = compile_shader(GL_VERTEX_SHADER, vertex_shader_source);
    GLuint fragment_shader = compile_shader(GL_FRAGMENT_SHADER, fragment_shader_source);
    
    if (!vertex_shader || !fragment_shader) return -1;
    
    ImageShaderProgram = glCreateProgram();
    glAttachShader(ImageShaderProgram, vertex_shader);
    glAttachShader(ImageShaderProgram, fragment_shader);
    glLinkProgram(ImageShaderProgram);
    
    GLint success;
    glGetProgramiv(ImageShaderProgram, GL_LINK_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetProgramInfoLog(ImageShaderProgram, 512, NULL, infoLog);
        fprintf(stderr, "Image shader program linking error: %s\n", infoLog);
        return -1;
    }
    
    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);
    
    // Get uniform locations
    ImageUniformTexture = glGetUniformLocation(ImageShaderProgram, "ourTexture");
    ImageUniformModelview = glGetUniformLocation(ImageShaderProgram, "modelviewMat");
    ImageUniformProjection = glGetUniformLocation(ImageShaderProgram, "projMat");
    ImageUniformGrayscale = glGetUniformLocation(ImageShaderProgram, "grayscale");
    ImageUniformBrightness = glGetUniformLocation(ImageShaderProgram, "brightness");
    ImageUniformContrast = glGetUniformLocation(ImageShaderProgram, "contrast");
    ImageUniformGamma = glGetUniformLocation(ImageShaderProgram, "gamma");
    ImageUniformOpacity = glGetUniformLocation(ImageShaderProgram, "opacity");
    ImageUniformColorGains = glGetUniformLocation(ImageShaderProgram, "colorGains");
    ImageUniformInvertMode = glGetUniformLocation(ImageShaderProgram, "invertMode");
    ImageUniformThresholdMode = glGetUniformLocation(ImageShaderProgram, "thresholdMode");
    ImageUniformThresholdValue = glGetUniformLocation(ImageShaderProgram, "thresholdValue");
    ImageUniformMaskMode = glGetUniformLocation(ImageShaderProgram, "maskMode");
    ImageUniformMaskCenter = glGetUniformLocation(ImageShaderProgram, "maskCenter");
    ImageUniformMaskRadius = glGetUniformLocation(ImageShaderProgram, "maskRadius");
    ImageUniformMaskSize = glGetUniformLocation(ImageShaderProgram, "maskSize");
    ImageUniformMaskFeather = glGetUniformLocation(ImageShaderProgram, "maskFeather");    
    ImageUniformAspectRatio = glGetUniformLocation(ImageShaderProgram, "aspectRatio");
    
    return 0;
}

/****************************************************************/
/*                Graphics Object Functions                     */
/****************************************************************/

// Generate aspect-ratio corrected quad vertices
static void generate_aspect_corrected_vertices(float *vertices, float aspect_ratio) {
    float half_width, half_height;
    
    if (aspect_ratio >= 1.0f) {
        half_width = 0.5f;
        half_height = 0.5f / aspect_ratio;
    } else {
        half_width = 0.5f * aspect_ratio;
        half_height = 0.5f;
    }
    
    float temp_vertices[] = {
        -half_width,  half_height, 0.0f,  0.0f, 0.0f,
        -half_width, -half_height, 0.0f,  0.0f, 1.0f,
         half_width, -half_height, 0.0f,  1.0f, 1.0f,
        -half_width,  half_height, 0.0f,  0.0f, 0.0f,
         half_width, -half_height, 0.0f,  1.0f, 1.0f,
         half_width,  half_height, 0.0f,  1.0f, 0.0f
    };
    
    memcpy(vertices, temp_vertices, sizeof(temp_vertices));
}

// Initialize per-object OpenGL resources
static int init_obj_gl_resources(IMAGE_OBJ *img, float aspect_ratio) {
    glGenVertexArrays(1, &img->vao);
    glBindVertexArray(img->vao);
    
    glGenBuffers(1, &img->vertex_buffer);
    glBindBuffer(GL_ARRAY_BUFFER, img->vertex_buffer);
    
    float vertices[30];
    generate_aspect_corrected_vertices(vertices, aspect_ratio);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
    
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
    
    glBindVertexArray(0);
    
    return 0;
}

void imageShow(GR_OBJ *gobj) {
    IMAGE_OBJ *img = (IMAGE_OBJ *) GR_CLIENTDATA(gobj);
    
    if (!img->visible) return;
    if (img->texture_id < 0 || img->texture_id >= TexturePool.count) return;
    
    IMAGE_TEXTURE *tex = &TexturePool.textures[img->texture_id];
    if (!tex->in_use) return;
    
    float modelview[16], projection[16];
    stimGetMatrix(STIM_MODELVIEW_MATRIX, modelview);
    stimGetMatrix(STIM_PROJECTION_MATRIX, projection);
    
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    
    glUseProgram(ImageShaderProgram);
    glUniformMatrix4fv(ImageUniformModelview, 1, GL_FALSE, modelview);
    glUniformMatrix4fv(ImageUniformProjection, 1, GL_FALSE, projection);
    glUniform1i(ImageUniformGrayscale, img->grayscale_mode); 
    glUniform1f(ImageUniformBrightness, img->brightness);
    glUniform1f(ImageUniformContrast, img->contrast);
    glUniform1f(ImageUniformGamma, img->gamma);
    glUniform1f(ImageUniformOpacity, img->opacity);
    glUniform3f(ImageUniformColorGains, img->red_gain, img->green_gain, img->blue_gain);
    glUniform1i(ImageUniformInvertMode, img->invert_mode);
    glUniform1i(ImageUniformThresholdMode, img->threshold_mode);
    glUniform1f(ImageUniformThresholdValue, img->threshold_value);
    glUniform1i(ImageUniformMaskMode, img->mask_mode);
    glUniform2f(ImageUniformMaskCenter, img->mask_center_x, img->mask_center_y);
    glUniform1f(ImageUniformMaskRadius, img->mask_radius);
    glUniform2f(ImageUniformMaskSize, img->mask_width, img->mask_height);
    glUniform1f(ImageUniformMaskFeather, img->mask_feather);
    glUniform1f(ImageUniformAspectRatio, img->aspect_ratio);
 
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex->texid);
    glUniform1i(ImageUniformTexture, 0);
    
    glBindVertexArray(img->vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    
    glBindVertexArray(0);
    glUseProgram(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_BLEND);
}

void imageDelete(GR_OBJ *gobj) {
    IMAGE_OBJ *img = (IMAGE_OBJ *) GR_CLIENTDATA(gobj);

    // Clean up per-object OpenGL resources (NOT the texture - that's in the pool)
    if (img->vertex_buffer) glDeleteBuffers(1, &img->vertex_buffer);
    if (img->vao) glDeleteVertexArrays(1, &img->vao);
    
    free((void *) img);
}

void imageReset(GR_OBJ *gobj) {
    IMAGE_OBJ *img = (IMAGE_OBJ *) GR_CLIENTDATA(gobj);
    // Reset any state as needed
}

// Create graphics object from texture ID
static int imageCreateFromTexture(OBJ_LIST *objlist, int texture_id) {
    if (texture_id < 0 || texture_id >= MAX_IMAGE_TEXTURES) return -1;
    
    IMAGE_TEXTURE *tex = &TexturePool.textures[texture_id];
    if (!tex->in_use) return -1;
    
    const char *name = "Image";
    GR_OBJ *obj;
    IMAGE_OBJ *img;

    obj = gobjCreateObj();
    if (!obj) return -1;

    strcpy(GR_NAME(obj), name);
    GR_OBJTYPE(obj) = ImageID;

    GR_DELETEFUNCP(obj) = imageDelete;
    GR_RESETFUNCP(obj) = imageReset;
    GR_ACTIONFUNCP(obj) = imageShow;

    img = (IMAGE_OBJ *) calloc(1, sizeof(IMAGE_OBJ));
    GR_CLIENTDATA(obj) = img;

    // Link to texture
    img->texture_id = texture_id;
    img->aspect_ratio = tex->aspect_ratio;
    
    // Initialize state
    img->visible = 1;
    img->grayscale_mode = 0;
    img->brightness = 0.0f;
    img->contrast = 1.0f;
    img->gamma = 1.0f;
    img->opacity = 1.0f;
    img->red_gain = 1.0f;
    img->green_gain = 1.0f;
    img->blue_gain = 1.0f;
    img->invert_mode = 0;
    img->threshold_mode = 0;
    img->threshold_value = 0.5f;
    img->mask_mode = 0;
    img->mask_center_x = 0.5f;
    img->mask_center_y = 0.5f;
    img->mask_radius = 0.2f;
    img->mask_width = 0.4f;
    img->mask_height = 0.3f;
    img->mask_feather = 0.05f;
    
    // Initialize per-object GL resources
    if (init_obj_gl_resources(img, tex->aspect_ratio) < 0) {
        fprintf(getConsoleFP(), "error initializing OpenGL resources\n");
        imageDelete(obj);
        return -1;
    }
    
    return gobjAddObj(objlist, obj);
}

/****************************************************************/
/*                   Tcl Command: imageTextureLoad              */
/****************************************************************/

static int imagetextureloadCmd(ClientData clientData, Tcl_Interp *interp,
                               int argc, char *argv[]) {
    int filter = GL_LINEAR;
    
    if (argc < 2) {
        Tcl_AppendResult(interp, "usage: ", argv[0], " filename [filter]", NULL);
        return TCL_ERROR;
    }
    
    if (argc > 2) {
        if (!strcmp(argv[2], "NEAREST") || !strcmp(argv[2], "nearest"))
            filter = GL_NEAREST;
        else if (!strcmp(argv[2], "LINEAR") || !strcmp(argv[2], "linear"))
            filter = GL_LINEAR;
    }
    
    int id = texture_load_from_file(argv[1], filter);
    if (id < 0) {
        Tcl_AppendResult(interp, argv[0], ": unable to load image \"", argv[1], "\"", NULL);
        return TCL_ERROR;
    }
    
    Tcl_SetObjResult(interp, Tcl_NewIntObj(id));
    return TCL_OK;
}

/****************************************************************/
/*                   Tcl Command: imageTextureRaw               */
/****************************************************************/

static int imagetexturerawCmd(ClientData clientData, Tcl_Interp *interp,
                              int objc, Tcl_Obj *const objv[]) {
    int width, height, channels;
    int filter = GL_LINEAR;
    unsigned char *data;
    Tcl_Size length;
    
    if (objc < 5) {
        Tcl_WrongNumArgs(interp, 1, objv, "data width height channels [filter]");
        return TCL_ERROR;
    }
    
    data = Tcl_GetByteArrayFromObj(objv[1], &length);
    if (!data) {
        Tcl_SetResult(interp, "invalid byte array data", TCL_STATIC);
        return TCL_ERROR;
    }
    
    if (Tcl_GetIntFromObj(interp, objv[2], &width) != TCL_OK) return TCL_ERROR;
    if (Tcl_GetIntFromObj(interp, objv[3], &height) != TCL_OK) return TCL_ERROR;
    if (Tcl_GetIntFromObj(interp, objv[4], &channels) != TCL_OK) return TCL_ERROR;
    
    if (channels < 1 || channels > 4) {
        Tcl_SetResult(interp, "channels must be 1, 3, or 4", TCL_STATIC);
        return TCL_ERROR;
    }
    
    if (length < width * height * channels) {
        Tcl_SetResult(interp, "byte array too small for specified dimensions", TCL_STATIC);
        return TCL_ERROR;
    }
    
    if (objc > 5) {
        const char *filtername = Tcl_GetString(objv[5]);
        if (!strcmp(filtername, "NEAREST") || !strcmp(filtername, "nearest"))
            filter = GL_NEAREST;
        else if (!strcmp(filtername, "LINEAR") || !strcmp(filtername, "linear"))
            filter = GL_LINEAR;
    }
    
    int id = texture_load_from_raw(data, width, height, channels, filter);
    if (id < 0) {
        Tcl_SetResult(interp, "unable to create texture from raw data", TCL_STATIC);
        return TCL_ERROR;
    }
    
    Tcl_SetObjResult(interp, Tcl_NewIntObj(id));
    return TCL_OK;
}

/****************************************************************/
/*                   Tcl Command: imageTextureFromList          */
/****************************************************************/

static int imagetexturefromlistCmd(ClientData clientData, Tcl_Interp *interp,
                                   int argc, char *argv[]) {
    DYN_LIST *dl;
    int width, height;
    int filter = GL_LINEAR;
    
    if (argc < 4) {
        Tcl_AppendResult(interp, "usage: ", argv[0], " dynlist width height [filter]", NULL);
        return TCL_ERROR;
    }
    
    if (tclFindDynList(interp, argv[1], &dl) != TCL_OK) return TCL_ERROR;
    if (Tcl_GetInt(interp, argv[2], &width) != TCL_OK) return TCL_ERROR;
    if (Tcl_GetInt(interp, argv[3], &height) != TCL_OK) return TCL_ERROR;
    
    if (argc > 4) {
        if (!strcmp(argv[4], "NEAREST") || !strcmp(argv[4], "nearest"))
            filter = GL_NEAREST;
        else if (!strcmp(argv[4], "LINEAR") || !strcmp(argv[4], "linear"))
            filter = GL_LINEAR;
    }
    
    int id = texture_load_from_dynlist(dl, width, height, filter);
    if (id < 0) {
        Tcl_AppendResult(interp, argv[0], ": unable to create texture from dynlist \"", 
                         argv[1], "\"", NULL);
        return TCL_ERROR;
    }
    
    Tcl_SetObjResult(interp, Tcl_NewIntObj(id));
    return TCL_OK;
}

/****************************************************************/
/*                   Tcl Command: imageTextureReset             */
/****************************************************************/

static int imagetextureresetCmd(ClientData clientData, Tcl_Interp *interp,
                                int argc, char *argv[]) {
    texture_pool_reset();
    return TCL_OK;
}

/****************************************************************/
/*                   Tcl Command: imageTextureDelete            */
/****************************************************************/

static int imagetexturedeleteCmd(ClientData clientData, Tcl_Interp *interp,
                                 int argc, char *argv[]) {
    int id;
    
    if (argc < 2) {
        Tcl_AppendResult(interp, "usage: ", argv[0], " texture_id", NULL);
        return TCL_ERROR;
    }
    
    if (Tcl_GetInt(interp, argv[1], &id) != TCL_OK) return TCL_ERROR;
    
    if (id < 0 || id >= MAX_IMAGE_TEXTURES || !TexturePool.textures[id].in_use) {
        Tcl_AppendResult(interp, argv[0], ": invalid texture id", NULL);
        return TCL_ERROR;
    }
    
    texture_pool_free(id);
    return TCL_OK;
}

/****************************************************************/
/*                   Tcl Command: imageTextureInfo              */
/****************************************************************/

static int imagetextureinfoCmd(ClientData clientData, Tcl_Interp *interp,
                               int argc, char *argv[]) {
    int id;
    
    if (argc < 2) {
        Tcl_AppendResult(interp, "usage: ", argv[0], " texture_id", NULL);
        return TCL_ERROR;
    }
    
    if (Tcl_GetInt(interp, argv[1], &id) != TCL_OK) return TCL_ERROR;
    
    if (id < 0 || id >= MAX_IMAGE_TEXTURES || !TexturePool.textures[id].in_use) {
        Tcl_AppendResult(interp, argv[0], ": invalid texture id", NULL);
        return TCL_ERROR;
    }
    
    IMAGE_TEXTURE *tex = &TexturePool.textures[id];
    
    Tcl_Obj *dictObj = Tcl_NewDictObj();
    Tcl_DictObjPut(interp, dictObj, Tcl_NewStringObj("width", -1), 
                   Tcl_NewIntObj(tex->width));
    Tcl_DictObjPut(interp, dictObj, Tcl_NewStringObj("height", -1), 
                   Tcl_NewIntObj(tex->height));
    Tcl_DictObjPut(interp, dictObj, Tcl_NewStringObj("channels", -1), 
                   Tcl_NewIntObj(tex->channels));
    Tcl_DictObjPut(interp, dictObj, Tcl_NewStringObj("aspect_ratio", -1), 
                   Tcl_NewDoubleObj(tex->aspect_ratio));
    Tcl_DictObjPut(interp, dictObj, Tcl_NewStringObj("texid", -1), 
                   Tcl_NewIntObj(tex->texid));
    
    Tcl_SetObjResult(interp, dictObj);
    return TCL_OK;
}

/****************************************************************/
/*                   Tcl Command: image                         */
/****************************************************************/

// Main image command: accepts texture_id OR filename
static int imageCmd(ClientData clientData, Tcl_Interp *interp,
                    int objc, Tcl_Obj *const objv[]) {
    OBJ_LIST *olist = (OBJ_LIST *) clientData;
    int id;
    int texture_id = -1;
    
    if (objc < 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "texture_id_or_filename");
        return TCL_ERROR;
    }
    
    // First, try to interpret as integer (texture_id)
    if (Tcl_GetIntFromObj(NULL, objv[1], &texture_id) == TCL_OK) {
        // It's an integer - check if it's a valid texture ID
        if (texture_id >= 0 && texture_id < MAX_IMAGE_TEXTURES && 
            TexturePool.textures[texture_id].in_use) {
            // Valid texture ID - create object from it
            id = imageCreateFromTexture(olist, texture_id);
            if (id < 0) {
                Tcl_SetResult(interp, "error creating image object from texture", TCL_STATIC);
                return TCL_ERROR;
            }
            Tcl_SetObjResult(interp, Tcl_NewIntObj(id));
            return TCL_OK;
        }
    }
    
    // Not a valid texture ID - try as filename or image data
    char *str_input = Tcl_GetString(objv[1]);
    int str_len = strlen(str_input);
    
    unsigned char *byte_input;
    Tcl_Size byte_len;
    byte_input = Tcl_GetByteArrayFromObj(objv[1], &byte_len);
    
    int is_file = 0;
    int is_data = 0;
    
    // Check if it could be a filename
    if (str_len > 0 && str_len < 512) {
        if (is_image_file(str_input)) {
            is_file = 1;
        }
    }
    
    // Check if it could be encoded image data
    if (byte_input && byte_len > 0) {
        if (is_image_data(byte_input, byte_len)) {
            is_data = 1;
        }
    }
    
    // Load texture and create object
    if (is_file && !is_data) {
        texture_id = texture_load_from_file(str_input, GL_LINEAR);
    } else if (is_data && !is_file) {
        texture_id = texture_load_from_memory(byte_input, byte_len, GL_LINEAR);
    } else if (is_file && is_data) {
        if (str_len < 100) {
            texture_id = texture_load_from_file(str_input, GL_LINEAR);
        } else {
            texture_id = texture_load_from_memory(byte_input, byte_len, GL_LINEAR);
        }
    } else {
        Tcl_SetResult(interp, "input is neither a valid texture id, image file, nor image data", TCL_STATIC);
        return TCL_ERROR;
    }
    
    if (texture_id < 0) {
        Tcl_SetResult(interp, "error loading image", TCL_STATIC);
        return TCL_ERROR;
    }
    
    id = imageCreateFromTexture(olist, texture_id);
    if (id < 0) {
        Tcl_SetResult(interp, "error creating image object", TCL_STATIC);
        return TCL_ERROR;
    }
    
    Tcl_SetObjResult(interp, Tcl_NewIntObj(id));
    return TCL_OK;
}

/****************************************************************/
/*                   Per-object control commands                */
/****************************************************************/

static int imageinfoCmd(ClientData clientData, Tcl_Interp *interp,
                       int argc, char *argv[]) {
    OBJ_LIST *olist = (OBJ_LIST *) clientData;
    IMAGE_OBJ *img;
    int id;
    Tcl_Obj *dictObj;

    if (argc < 2) {
        Tcl_AppendResult(interp, "usage: ", argv[0], " id", NULL);
        return TCL_ERROR;
    }

    if ((id = resolveObjId(interp, OL_NAMEINFO(olist), argv[1], ImageID, "image")) < 0)
        return TCL_ERROR;

    img = GR_CLIENTDATA(OL_OBJ(olist, id));
    
    IMAGE_TEXTURE *tex = &TexturePool.textures[img->texture_id];

    dictObj = Tcl_NewDictObj();
    Tcl_DictObjPut(interp, dictObj, Tcl_NewStringObj("texture_id", -1), 
                   Tcl_NewIntObj(img->texture_id));
    Tcl_DictObjPut(interp, dictObj, Tcl_NewStringObj("width", -1), 
                   Tcl_NewIntObj(tex->width));
    Tcl_DictObjPut(interp, dictObj, Tcl_NewStringObj("height", -1), 
                   Tcl_NewIntObj(tex->height));
    Tcl_DictObjPut(interp, dictObj, Tcl_NewStringObj("channels", -1), 
                   Tcl_NewIntObj(tex->channels));
    Tcl_DictObjPut(interp, dictObj, Tcl_NewStringObj("aspect_ratio", -1), 
                   Tcl_NewDoubleObj(img->aspect_ratio));

    Tcl_SetObjResult(interp, dictObj);
    return TCL_OK;
}

// Grayscale control
static int imagegrayscaleCmd(ClientData clientData, Tcl_Interp *interp,
                            int argc, char *argv[]) {
    OBJ_LIST *olist = (OBJ_LIST *) clientData;
    IMAGE_OBJ *img;
    int id, grayscale;

    if (argc < 2) {
        Tcl_AppendResult(interp, "usage: ", argv[0], " id [grayscale(0/1)]", NULL);
        return TCL_ERROR;
    }

    if ((id = resolveObjId(interp, OL_NAMEINFO(olist), argv[1], ImageID, "image")) < 0)
        return TCL_ERROR;

    img = GR_CLIENTDATA(OL_OBJ(olist, id));
    
    if (argc == 2) {
        Tcl_SetObjResult(interp, Tcl_NewIntObj(img->grayscale_mode));
        return TCL_OK;
    }
    
    if (Tcl_GetInt(interp, argv[2], &grayscale) != TCL_OK) return TCL_ERROR;
    img->grayscale_mode = grayscale ? 1 : 0;
    
    return TCL_OK;
}

// Brightness control
static int imagebrightnessCmd(ClientData clientData, Tcl_Interp *interp,
                             int argc, char *argv[]) {
    OBJ_LIST *olist = (OBJ_LIST *) clientData;
    IMAGE_OBJ *img;
    int id;
    double brightness;

    if (argc < 2) {
        Tcl_AppendResult(interp, "usage: ", argv[0], " id [brightness]", NULL);
        return TCL_ERROR;
    }

    if ((id = resolveObjId(interp, OL_NAMEINFO(olist), argv[1], ImageID, "image")) < 0)
        return TCL_ERROR;

    img = GR_CLIENTDATA(OL_OBJ(olist, id));
    
    if (argc == 2) {
        Tcl_SetObjResult(interp, Tcl_NewDoubleObj(img->brightness));
        return TCL_OK;
    }
    
    if (Tcl_GetDouble(interp, argv[2], &brightness) != TCL_OK) return TCL_ERROR;
    img->brightness = (float)fmax(-1.0, fmin(1.0, brightness));
    
    return TCL_OK;
}

// Contrast control
static int imagecontrastCmd(ClientData clientData, Tcl_Interp *interp,
                           int argc, char *argv[]) {
    OBJ_LIST *olist = (OBJ_LIST *) clientData;
    IMAGE_OBJ *img;
    int id;
    double contrast;

    if (argc < 2) {
        Tcl_AppendResult(interp, "usage: ", argv[0], " id [contrast]", NULL);
        return TCL_ERROR;
    }

    if ((id = resolveObjId(interp, OL_NAMEINFO(olist), argv[1], ImageID, "image")) < 0)
        return TCL_ERROR;

    img = GR_CLIENTDATA(OL_OBJ(olist, id));
    
    if (argc == 2) {
        Tcl_SetObjResult(interp, Tcl_NewDoubleObj(img->contrast));
        return TCL_OK;
    }
    
    if (Tcl_GetDouble(interp, argv[2], &contrast) != TCL_OK) return TCL_ERROR;
    img->contrast = (float)fmax(0.0, fmin(3.0, contrast));
    
    return TCL_OK;
}

// Gamma control
static int imagegammaCmd(ClientData clientData, Tcl_Interp *interp,
                        int argc, char *argv[]) {
    OBJ_LIST *olist = (OBJ_LIST *) clientData;
    IMAGE_OBJ *img;
    int id;
    double gamma;

    if (argc < 2) {
        Tcl_AppendResult(interp, "usage: ", argv[0], " id [gamma]", NULL);
        return TCL_ERROR;
    }

    if ((id = resolveObjId(interp, OL_NAMEINFO(olist), argv[1], ImageID, "image")) < 0)
        return TCL_ERROR;

    img = GR_CLIENTDATA(OL_OBJ(olist, id));
    
    if (argc == 2) {
        Tcl_SetObjResult(interp, Tcl_NewDoubleObj(img->gamma));
        return TCL_OK;
    }
    
    if (Tcl_GetDouble(interp, argv[2], &gamma) != TCL_OK) return TCL_ERROR;
    img->gamma = (float)fmax(0.1, fmin(3.0, gamma));
    
    return TCL_OK;
}

// Opacity control
static int imageopacityCmd(ClientData clientData, Tcl_Interp *interp,
                          int argc, char *argv[]) {
    OBJ_LIST *olist = (OBJ_LIST *) clientData;
    IMAGE_OBJ *img;
    int id;
    double opacity;

    if (argc < 2) {
        Tcl_AppendResult(interp, "usage: ", argv[0], " id [opacity]", NULL);
        return TCL_ERROR;
    }

    if ((id = resolveObjId(interp, OL_NAMEINFO(olist), argv[1], ImageID, "image")) < 0)
        return TCL_ERROR;

    img = GR_CLIENTDATA(OL_OBJ(olist, id));
    
    if (argc == 2) {
        Tcl_SetObjResult(interp, Tcl_NewDoubleObj(img->opacity));
        return TCL_OK;
    }
    
    if (Tcl_GetDouble(interp, argv[2], &opacity) != TCL_OK) return TCL_ERROR;
    img->opacity = (float)fmax(0.0, fmin(1.0, opacity));
    
    return TCL_OK;
}

// Color gains control
static int imagecolorgainsCmd(ClientData clientData, Tcl_Interp *interp,
                             int argc, char *argv[]) {
    OBJ_LIST *olist = (OBJ_LIST *) clientData;
    IMAGE_OBJ *img;
    int id;
    double red, green, blue;

    if (argc < 2) {
        Tcl_AppendResult(interp, "usage: ", argv[0], " id [red green blue]", NULL);
        return TCL_ERROR;
    }

    if ((id = resolveObjId(interp, OL_NAMEINFO(olist), argv[1], ImageID, "image")) < 0)
        return TCL_ERROR;

    img = GR_CLIENTDATA(OL_OBJ(olist, id));
    
    if (argc == 2) {
        Tcl_Obj *listObj = Tcl_NewListObj(0, NULL);
        Tcl_ListObjAppendElement(interp, listObj, Tcl_NewDoubleObj(img->red_gain));
        Tcl_ListObjAppendElement(interp, listObj, Tcl_NewDoubleObj(img->green_gain));
        Tcl_ListObjAppendElement(interp, listObj, Tcl_NewDoubleObj(img->blue_gain));
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
    
    img->red_gain = (float)fmax(0.0, fmin(2.0, red));
    img->green_gain = (float)fmax(0.0, fmin(2.0, green));
    img->blue_gain = (float)fmax(0.0, fmin(2.0, blue));
    
    return TCL_OK;
}

// Invert control
static int imageinvertCmd(ClientData clientData, Tcl_Interp *interp,
                         int argc, char *argv[]) {
    OBJ_LIST *olist = (OBJ_LIST *) clientData;
    IMAGE_OBJ *img;
    int id, invert;

    if (argc < 2) {
        Tcl_AppendResult(interp, "usage: ", argv[0], " id [invert(0/1)]", NULL);
        return TCL_ERROR;
    }

    if ((id = resolveObjId(interp, OL_NAMEINFO(olist), argv[1], ImageID, "image")) < 0)
        return TCL_ERROR;

    img = GR_CLIENTDATA(OL_OBJ(olist, id));
    
    if (argc == 2) {
        Tcl_SetObjResult(interp, Tcl_NewIntObj(img->invert_mode));
        return TCL_OK;
    }
    
    if (Tcl_GetInt(interp, argv[2], &invert) != TCL_OK) return TCL_ERROR;
    img->invert_mode = invert ? 1 : 0;
    
    return TCL_OK;
}

// Threshold control
static int imagethresholdCmd(ClientData clientData, Tcl_Interp *interp,
                            int argc, char *argv[]) {
    OBJ_LIST *olist = (OBJ_LIST *) clientData;
    IMAGE_OBJ *img;
    int id, enable;
    double threshold;

    if (argc < 2) {
        Tcl_AppendResult(interp, "usage: ", argv[0], " id [enable(0/1) threshold]", NULL);
        return TCL_ERROR;
    }

    if ((id = resolveObjId(interp, OL_NAMEINFO(olist), argv[1], ImageID, "image")) < 0)
        return TCL_ERROR;

    img = GR_CLIENTDATA(OL_OBJ(olist, id));
    
    if (argc == 2) {
        Tcl_Obj *listObj = Tcl_NewListObj(0, NULL);
        Tcl_ListObjAppendElement(interp, listObj, Tcl_NewIntObj(img->threshold_mode));
        Tcl_ListObjAppendElement(interp, listObj, Tcl_NewDoubleObj(img->threshold_value));
        Tcl_SetObjResult(interp, listObj);
        return TCL_OK;
    }
    
    if (argc < 4) {
        Tcl_AppendResult(interp, "usage: ", argv[0], " id enable threshold", NULL);
        return TCL_ERROR;
    }
    
    if (Tcl_GetInt(interp, argv[2], &enable) != TCL_OK) return TCL_ERROR;
    if (Tcl_GetDouble(interp, argv[3], &threshold) != TCL_OK) return TCL_ERROR;
    
    img->threshold_mode = enable ? 1 : 0;
    img->threshold_value = (float)fmax(0.0, fmin(1.0, threshold));
    
    return TCL_OK;
}

// Gaze-contingent mask control
// Circular (mode 1,3): imageMask id mode centerX centerY radius feather
// Rectangular (mode 2): imageMask id mode centerX centerY width height feather
static int imagemaskCmd(ClientData clientData, Tcl_Interp *interp,
                       int argc, char *argv[]) {
    OBJ_LIST *olist = (OBJ_LIST *) clientData;
    IMAGE_OBJ *img;
    int id, mode;
    double centerX, centerY, radius, width, height, feather;

    if (argc < 2) {
        Tcl_AppendResult(interp, "usage: ", argv[0], " id [mode centerX centerY radius feather] (circular)\n",
                         "   or: ", argv[0], " id mode centerX centerY width height feather (rectangular)", NULL);
        return TCL_ERROR;
    }

    if ((id = resolveObjId(interp, OL_NAMEINFO(olist), argv[1], ImageID, "image")) < 0)
        return TCL_ERROR;

    img = GR_CLIENTDATA(OL_OBJ(olist, id));
    
    if (argc == 2) {
        Tcl_Obj *listObj = Tcl_NewListObj(0, NULL);
        Tcl_ListObjAppendElement(interp, listObj, Tcl_NewIntObj(img->mask_mode));
        Tcl_ListObjAppendElement(interp, listObj, Tcl_NewDoubleObj(img->mask_center_x));
        Tcl_ListObjAppendElement(interp, listObj, Tcl_NewDoubleObj(img->mask_center_y));
        Tcl_ListObjAppendElement(interp, listObj, Tcl_NewDoubleObj(img->mask_radius));
        Tcl_ListObjAppendElement(interp, listObj, Tcl_NewDoubleObj(img->mask_width));
        Tcl_ListObjAppendElement(interp, listObj, Tcl_NewDoubleObj(img->mask_height));
        Tcl_ListObjAppendElement(interp, listObj, Tcl_NewDoubleObj(img->mask_feather));
        Tcl_SetObjResult(interp, listObj);
        return TCL_OK;
    }
    
    if (argc < 3) {
        Tcl_AppendResult(interp, "usage: ", argv[0], " id mode ...", NULL);
        return TCL_ERROR;
    }
    
    if (Tcl_GetInt(interp, argv[2], &mode) != TCL_OK) return TCL_ERROR;
    mode = (int)fmax(0, fmin(3, mode));
    
    if (mode == 0) {
        img->mask_mode = 0;
        return TCL_OK;
    }
    
    // Circular modes (1=window, 3=inverse)
    if (mode == 1 || mode == 3) {
        if (argc < 7) {
            Tcl_AppendResult(interp, "circular mask usage: ", argv[0], 
                             " id mode centerX centerY radius feather", NULL);
            return TCL_ERROR;
        }
        if (Tcl_GetDouble(interp, argv[3], &centerX) != TCL_OK) return TCL_ERROR;
        if (Tcl_GetDouble(interp, argv[4], &centerY) != TCL_OK) return TCL_ERROR;
        if (Tcl_GetDouble(interp, argv[5], &radius) != TCL_OK) return TCL_ERROR;
        if (Tcl_GetDouble(interp, argv[6], &feather) != TCL_OK) return TCL_ERROR;
        
        img->mask_mode = mode;
        img->mask_center_x = (float)fmax(0.0, fmin(1.0, centerX));
        img->mask_center_y = (float)fmax(0.0, fmin(1.0, centerY));
        img->mask_radius = (float)fmax(0.0, fmin(1.0, radius));
        img->mask_feather = (float)fmax(0.0, fmin(0.5, feather));
    }
    // Rectangular mode (2)
    else if (mode == 2) {
        if (argc < 8) {
            Tcl_AppendResult(interp, "rectangular mask usage: ", argv[0], 
                             " id mode centerX centerY width height feather", NULL);
            return TCL_ERROR;
        }
        if (Tcl_GetDouble(interp, argv[3], &centerX) != TCL_OK) return TCL_ERROR;
        if (Tcl_GetDouble(interp, argv[4], &centerY) != TCL_OK) return TCL_ERROR;
        if (Tcl_GetDouble(interp, argv[5], &width) != TCL_OK) return TCL_ERROR;
        if (Tcl_GetDouble(interp, argv[6], &height) != TCL_OK) return TCL_ERROR;
        if (Tcl_GetDouble(interp, argv[7], &feather) != TCL_OK) return TCL_ERROR;
        
        img->mask_mode = mode;
        img->mask_center_x = (float)fmax(0.0, fmin(1.0, centerX));
        img->mask_center_y = (float)fmax(0.0, fmin(1.0, centerY));
        img->mask_width = (float)fmax(0.0, fmin(1.0, width));
        img->mask_height = (float)fmax(0.0, fmin(1.0, height));
        img->mask_feather = (float)fmax(0.0, fmin(0.5, feather));
    }
    
    return TCL_OK;
}

/****************************************************************/
/*                       Module Init                            */
/****************************************************************/

#ifdef _WIN32
EXPORT(int, Image_Init) (Tcl_Interp *interp)
#else
int Image_Init(Tcl_Interp *interp)
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

    if (ImageID < 0) {
        ImageID = gobjRegisterType();
        
        // Ensure OpenGL function pointers are loaded
        gladLoadGL();
        
        // Create shader program once for all image instances
        if (create_image_shader_program() < 0) {
            Tcl_SetResult(interp, "error creating image shader program", TCL_STATIC);
            return TCL_ERROR;
        }
    }

    // Texture pool management commands
    Tcl_CreateCommand(interp, "imageTextureLoad", (Tcl_CmdProc *) imagetextureloadCmd,
                      (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
    Tcl_CreateObjCommand(interp, "imageTextureRaw", imagetexturerawCmd,
                         (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
    Tcl_CreateCommand(interp, "imageTextureFromList", (Tcl_CmdProc *) imagetexturefromlistCmd,
                      (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
    Tcl_CreateCommand(interp, "imageTextureReset", (Tcl_CmdProc *) imagetextureresetCmd,
                      (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
    Tcl_CreateCommand(interp, "imageTextureDelete", (Tcl_CmdProc *) imagetexturedeleteCmd,
                      (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
    Tcl_CreateCommand(interp, "imageTextureInfo", (Tcl_CmdProc *) imagetextureinfoCmd,
                      (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
    
    // Main image object command
    Tcl_CreateObjCommand(interp, "image", imageCmd, 
                         (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
    
    // Per-object control commands
    Tcl_CreateCommand(interp, "imageInfo", (Tcl_CmdProc *) imageinfoCmd,
                      (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
    Tcl_CreateCommand(interp, "imageGrayscale", (Tcl_CmdProc *) imagegrayscaleCmd,
                      (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
    Tcl_CreateCommand(interp, "imageBrightness", (Tcl_CmdProc *) imagebrightnessCmd,
                      (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
    Tcl_CreateCommand(interp, "imageContrast", (Tcl_CmdProc *) imagecontrastCmd,
                      (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
    Tcl_CreateCommand(interp, "imageGamma", (Tcl_CmdProc *) imagegammaCmd,
                      (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
    Tcl_CreateCommand(interp, "imageOpacity", (Tcl_CmdProc *) imageopacityCmd,
                      (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
    Tcl_CreateCommand(interp, "imageColorGains", (Tcl_CmdProc *) imagecolorgainsCmd,
                      (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
    Tcl_CreateCommand(interp, "imageInvert", (Tcl_CmdProc *) imageinvertCmd,
                      (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
    Tcl_CreateCommand(interp, "imageThreshold", (Tcl_CmdProc *) imagethresholdCmd,
                      (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
    Tcl_CreateCommand(interp, "imageMask", (Tcl_CmdProc *) imagemaskCmd,
                      (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);


   /* Asset helper to help find images */
    Tcl_Eval(interp, 
     "proc imageAsset {filename} {\n"
     "  return [image [assetFind $filename]]\n"
     "}\n"
     "proc textureAsset {filename} {\n"
     "  return [imageTextureLoad [assetFind $filename]]\n"
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
