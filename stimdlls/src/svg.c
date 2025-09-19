/*
 * svg.c
 *  SVG display module using NanoSVG for parsing and rasterization
 *  Follows the same pattern as image.c and polygon.c modules
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

#define NANOSVG_IMPLEMENTATION
#include "nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvgrast.h"

#include <tcl.h>

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <stim2.h>
#include <prmutil.h>

typedef struct _svg_obj {
    int width;
    int height;
    float aspect_ratio;
    
    // Display state
    int visible;
    
    // OpenGL resources
    GLuint texture;
    GLuint vertex_buffer;
    GLuint vao;
    
    // SVG-specific resources
    NSVGimage* svg_image;
    NSVGrasterizer* rasterizer;
    unsigned char* raster_data;
    int raster_width;
    int raster_height;
    
    // Rendering parameters
    float scale;
    float opacity;
    float color[4];         // Tint color (RGBA)
    int color_override;     // 0=preserve original colors, 1=apply tint
    float rotation;         // Rotation angle in degrees
    
    // Background parameters
    int background_enabled; // 0=transparent, 1=solid background
    float background_color[4]; // Background color (RGBA)
    
} SVG_OBJ;

static int SvgID = -1;  /* unique SVG object id */
static GLuint SvgShaderProgram = 0;  /* shared shader program */
static GLint SvgUniformTexture = -1;
static GLint SvgUniformModelview = -1;
static GLint SvgUniformProjection = -1;
static GLint SvgUniformOpacity = -1;
static GLint SvgUniformColorTint = -1;
static GLint SvgUniformColorOverride = -1;
static GLint SvgUniformAspectRatio = -1;

#ifdef STIM2_USE_GLES
static const char* svg_vertex_shader_source = 
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

static const char* svg_fragment_shader_source = 
"#version 300 es\n"
"precision mediump float;\n"
"out vec4 FragColor;\n"
"in vec2 TexCoord;\n"
"uniform sampler2D ourTexture;\n"
"uniform float opacity;\n"
"uniform vec4 colorTint;\n"
"uniform int colorOverride;\n"
"uniform float aspectRatio;\n"
"\n"
"void main() {\n"
"    vec4 color = texture(ourTexture, TexCoord);\n"
"    \n"
"    // Apply color override or tinting\n"
"    if (colorOverride == 1) {\n"
"        // Use tint color but preserve alpha\n"
"        color.rgb = colorTint.rgb;\n"
"        color.a *= colorTint.a;\n"
"    } else if (colorOverride == 2) {\n"
"        // Multiply with tint color\n"
"        color *= colorTint;\n"
"    }\n"
"    \n"
"    // Apply global opacity\n"
"    color.a *= opacity;\n"
"    \n"
"    FragColor = color;\n"
"}\n";

#else
static const char* svg_vertex_shader_source = 
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

static const char* svg_fragment_shader_source = 
"#version 330 core\n"
"out vec4 FragColor;\n"
"in vec2 TexCoord;\n"
"uniform sampler2D ourTexture;\n"
"uniform float opacity;\n"
"uniform vec4 colorTint;\n"
"uniform int colorOverride;\n"
"uniform float aspectRatio;\n"
"\n"
"void main() {\n"
"    vec4 color = texture(ourTexture, TexCoord);\n"
"    \n"
"    if (colorOverride == 1) {\n"
"        color.rgb = colorTint.rgb;\n"
"        color.a *= colorTint.a;\n"
"    } else if (colorOverride == 2) {\n"
"        color *= colorTint;\n"
"    }\n"
"    \n"
"    color.a *= opacity;\n"
"    FragColor = color;\n"
"}\n";
#endif

// Generate aspect-ratio corrected quad vertices
static void generate_svg_vertices(float *vertices, float aspect_ratio) {
    float half_width, half_height;
    
    if (aspect_ratio >= 1.0f) {
        half_width = 0.5f;
        half_height = 0.5f / aspect_ratio;
    } else {
        half_width = 0.5f * aspect_ratio;
        half_height = 0.5f;
    }
    
    float temp_vertices[] = {
        -half_width,  half_height, 0.0f,  0.0f, 0.0f,  // top-left
        -half_width, -half_height, 0.0f,  0.0f, 1.0f,  // bottom-left
         half_width, -half_height, 0.0f,  1.0f, 1.0f,  // bottom-right
        -half_width,  half_height, 0.0f,  0.0f, 0.0f,  // top-left
         half_width, -half_height, 0.0f,  1.0f, 1.0f,  // bottom-right
         half_width,  half_height, 0.0f,  1.0f, 0.0f   // top-right
    };
    
    memcpy(vertices, temp_vertices, sizeof(temp_vertices));
}

// Helper function to compile shader
static GLuint compile_svg_shader(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    
    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetShaderInfoLog(shader, 512, NULL, infoLog);
        fprintf(stderr, "SVG shader compilation error: %s\n", infoLog);
        return 0;
    }
    return shader;
}

// Create shader program once at module initialization
static int create_svg_shader_program() {
    GLuint vertex_shader = compile_svg_shader(GL_VERTEX_SHADER, svg_vertex_shader_source);
    GLuint fragment_shader = compile_svg_shader(GL_FRAGMENT_SHADER, svg_fragment_shader_source);
    
    if (!vertex_shader || !fragment_shader) return -1;
    
    SvgShaderProgram = glCreateProgram();
    glAttachShader(SvgShaderProgram, vertex_shader);
    glAttachShader(SvgShaderProgram, fragment_shader);
    glLinkProgram(SvgShaderProgram);
    
    GLint success;
    glGetProgramiv(SvgShaderProgram, GL_LINK_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetProgramInfoLog(SvgShaderProgram, 512, NULL, infoLog);
        fprintf(stderr, "SVG shader program linking error: %s\n", infoLog);
        return -1;
    }
    
    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);
    
    // Get uniform locations
    SvgUniformTexture = glGetUniformLocation(SvgShaderProgram, "ourTexture");
    SvgUniformModelview = glGetUniformLocation(SvgShaderProgram, "modelviewMat");
    SvgUniformProjection = glGetUniformLocation(SvgShaderProgram, "projMat");
    SvgUniformOpacity = glGetUniformLocation(SvgShaderProgram, "opacity");
    SvgUniformColorTint = glGetUniformLocation(SvgShaderProgram, "colorTint");
    SvgUniformColorOverride = glGetUniformLocation(SvgShaderProgram, "colorOverride");
    SvgUniformAspectRatio = glGetUniformLocation(SvgShaderProgram, "aspectRatio");
    
    return 0;
}

// Initialize OpenGL resources
static int init_svg_gl_resources(SVG_OBJ *svg) {
    // Create VAO and VBO
    glGenVertexArrays(1, &svg->vao);
    glBindVertexArray(svg->vao);
    
    glGenBuffers(1, &svg->vertex_buffer);
    glBindBuffer(GL_ARRAY_BUFFER, svg->vertex_buffer);
    glBufferData(GL_ARRAY_BUFFER, 6 * 5 * sizeof(float), NULL, GL_DYNAMIC_DRAW);
    
    // Position attribute (location = 0)
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
    
    // Texture coord attribute (location = 1)
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
    
    glBindVertexArray(0);
    
    // Create texture
    glGenTextures(1, &svg->texture);
    
    return 0;
}

// Rasterize SVG at specified dimensions
static int rasterize_svg(SVG_OBJ *svg, int width, int height) {
    if (!svg->svg_image || !svg->rasterizer) return -1;
    
    // Free existing raster data
    if (svg->raster_data) {
        free(svg->raster_data);
        svg->raster_data = NULL;
    }
    
    // Allocate new raster data (RGBA format)
    svg->raster_width = width;
    svg->raster_height = height;
    svg->raster_data = (unsigned char*)malloc(width * height * 4);
    if (!svg->raster_data) return -1;
    
    // Clear with transparent background or solid background color
    if (svg->background_enabled) {
        unsigned char bg_r = (unsigned char)(svg->background_color[0] * 255);
        unsigned char bg_g = (unsigned char)(svg->background_color[1] * 255);
        unsigned char bg_b = (unsigned char)(svg->background_color[2] * 255);
        unsigned char bg_a = (unsigned char)(svg->background_color[3] * 255);
        
        for (int i = 0; i < width * height; i++) {
            svg->raster_data[i*4 + 0] = bg_r;
            svg->raster_data[i*4 + 1] = bg_g;
            svg->raster_data[i*4 + 2] = bg_b;
            svg->raster_data[i*4 + 3] = bg_a;
        }
    } else {
        memset(svg->raster_data, 0, width * height * 4);
    }
    
    // Calculate scale to fit SVG in raster dimensions
    float scale_x = (float)width / svg->svg_image->width;
    float scale_y = (float)height / svg->svg_image->height;
    float scale = fminf(scale_x, scale_y) * svg->scale;
    
    // Rasterize the SVG
    nsvgRasterize(svg->rasterizer, svg->svg_image, 0, 0, scale, 
                  svg->raster_data, width, height, width * 4);
    
    // Update OpenGL texture
    glBindTexture(GL_TEXTURE_2D, svg->texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, 
                 GL_RGBA, GL_UNSIGNED_BYTE, svg->raster_data);
    glBindTexture(GL_TEXTURE_2D, 0);
    
    return 0;
}

// Load SVG from string
static int load_svg_from_string(SVG_OBJ *svg, const char *svg_data) {
    // Parse SVG from memory - make a copy since nsvgParse modifies the string
    char *svg_copy = strdup(svg_data);
    if (!svg_copy) {
        fprintf(getConsoleFP(), "Failed to allocate memory for SVG data\n");
        return -1;
    }
    
    svg->svg_image = nsvgParse(svg_copy, "px", 96.0f);
    free(svg_copy);
    
    if (!svg->svg_image) {
        fprintf(getConsoleFP(), "Failed to parse SVG from string\n");
        return -1;
    }
    
    // Create rasterizer
    svg->rasterizer = nsvgCreateRasterizer();
    if (!svg->rasterizer) {
        fprintf(getConsoleFP(), "Failed to create SVG rasterizer\n");
        nsvgDelete(svg->svg_image);
        svg->svg_image = NULL;
        return -1;
    }
    
    // Set dimensions from SVG
    svg->width = (int)svg->svg_image->width;
    svg->height = (int)svg->svg_image->height;
    svg->aspect_ratio = svg->svg_image->width / svg->svg_image->height;
    
    // Generate aspect-corrected vertices and update VBO
    float vertices[30];
    generate_svg_vertices(vertices, svg->aspect_ratio);
    
    glBindBuffer(GL_ARRAY_BUFFER, svg->vertex_buffer);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    
    // Initial rasterization at native size
    int raster_size = (int)fmaxf(svg->svg_image->width, svg->svg_image->height);
    raster_size = fmaxf(256, fminf(2048, raster_size)); // Clamp to reasonable range
    
    return rasterize_svg(svg, raster_size, raster_size);
}

// Load SVG from file
static int load_svg_from_file(SVG_OBJ *svg, const char *filename) {
    // Parse SVG file
    svg->svg_image = nsvgParseFromFile(filename, "px", 96.0f);
    if (!svg->svg_image) {
        fprintf(getConsoleFP(), "Failed to parse SVG file: %s\n", filename);
        return -1;
    }
    
    // Create rasterizer
    svg->rasterizer = nsvgCreateRasterizer();
    if (!svg->rasterizer) {
        fprintf(getConsoleFP(), "Failed to create SVG rasterizer\n");
        nsvgDelete(svg->svg_image);
        svg->svg_image = NULL;
        return -1;
    }
    
    // Set dimensions from SVG
    svg->width = (int)svg->svg_image->width;
    svg->height = (int)svg->svg_image->height;
    svg->aspect_ratio = svg->svg_image->width / svg->svg_image->height;
    
    // Generate aspect-corrected vertices and update VBO
    float vertices[30];
    generate_svg_vertices(vertices, svg->aspect_ratio);
    
    glBindBuffer(GL_ARRAY_BUFFER, svg->vertex_buffer);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    
    // Initial rasterization at native size
    int raster_size = (int)fmaxf(svg->svg_image->width, svg->svg_image->height);
    raster_size = fmaxf(256, fminf(2048, raster_size)); // Clamp to reasonable range
    
    return rasterize_svg(svg, raster_size, raster_size);
}

void svgShow(GR_OBJ *gobj) {
    SVG_OBJ *svg = (SVG_OBJ *) GR_CLIENTDATA(gobj);
    
    if (!svg->visible || !svg->texture) return;
    
    // Get matrices
    float modelview[16], projection[16];
    stimGetMatrix(STIM_MODELVIEW_MATRIX, modelview);
    stimGetMatrix(STIM_PROJECTION_MATRIX, projection);
    
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    
    glUseProgram(SvgShaderProgram);
    glUniformMatrix4fv(SvgUniformModelview, 1, GL_FALSE, modelview);
    glUniformMatrix4fv(SvgUniformProjection, 1, GL_FALSE, projection);
    glUniform1f(SvgUniformOpacity, svg->opacity);
    glUniform4f(SvgUniformColorTint, svg->color[0], svg->color[1], 
                svg->color[2], svg->color[3]);
    glUniform1i(SvgUniformColorOverride, svg->color_override);
    glUniform1f(SvgUniformAspectRatio, svg->aspect_ratio);
 
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, svg->texture);
    glUniform1i(SvgUniformTexture, 0);
    
    glBindVertexArray(svg->vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    
    // Clean up
    glBindVertexArray(0);
    glUseProgram(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_BLEND);
}

void svgDelete(GR_OBJ *gobj) {
    SVG_OBJ *svg = (SVG_OBJ *) GR_CLIENTDATA(gobj);

    // Clean up SVG resources
    if (svg->raster_data) free(svg->raster_data);
    if (svg->rasterizer) nsvgDeleteRasterizer(svg->rasterizer);
    if (svg->svg_image) nsvgDelete(svg->svg_image);
    
    // Clean up OpenGL resources
    if (svg->texture) glDeleteTextures(1, &svg->texture);
    if (svg->vertex_buffer) glDeleteBuffers(1, &svg->vertex_buffer);
    if (svg->vao) glDeleteVertexArrays(1, &svg->vao);
    
    free((void *) svg);
}

void svgReset(GR_OBJ *gobj) {
    SVG_OBJ *svg = (SVG_OBJ *) GR_CLIENTDATA(gobj);
    // Reset any state as needed
}

int svgCreate(OBJ_LIST *objlist, char *filename) {
    const char *name = "SVG";
    GR_OBJ *obj;
    SVG_OBJ *svg;

    obj = gobjCreateObj();
    if (!obj) return -1;

    strcpy(GR_NAME(obj), name);
    GR_OBJTYPE(obj) = SvgID;

    GR_DELETEFUNCP(obj) = svgDelete;
    GR_RESETFUNCP(obj) = svgReset;
    GR_ACTIONFUNCP(obj) = svgShow;

    svg = (SVG_OBJ *) calloc(1, sizeof(SVG_OBJ));
    GR_CLIENTDATA(obj) = svg;

    // Initialize state
    svg->visible = 1;
    svg->scale = 1.0f;
    svg->opacity = 1.0f;
    svg->color[0] = 1.0f;  // Default white tint
    svg->color[1] = 1.0f;
    svg->color[2] = 1.0f;
    svg->color[3] = 1.0f;
    svg->color_override = 0;  // Preserve original colors
    svg->rotation = 0.0f;
    svg->background_enabled = 0;
    svg->background_color[0] = 1.0f;
    svg->background_color[1] = 1.0f;
    svg->background_color[2] = 1.0f;
    svg->background_color[3] = 1.0f;
    
    // Initialize OpenGL resources
    if (init_svg_gl_resources(svg) < 0) {
        fprintf(getConsoleFP(), "error initializing SVG OpenGL resources\n");
        svgDelete(obj);
        return -1;
    }

    // Load SVG
    if (load_svg_from_file(svg, filename) < 0) {
        fprintf(getConsoleFP(), "error loading SVG: %s\n", filename);
        svgDelete(obj);
        return -1;
    }
    
    return gobjAddObj(objlist, obj);
}

int svgCreateFromString(OBJ_LIST *objlist, char *svg_data) {
    const char *name = "SVG";
    GR_OBJ *obj;
    SVG_OBJ *svg;

    obj = gobjCreateObj();
    if (!obj) return -1;

    strcpy(GR_NAME(obj), name);
    GR_OBJTYPE(obj) = SvgID;

    GR_DELETEFUNCP(obj) = svgDelete;
    GR_RESETFUNCP(obj) = svgReset;
    GR_ACTIONFUNCP(obj) = svgShow;

    svg = (SVG_OBJ *) calloc(1, sizeof(SVG_OBJ));
    GR_CLIENTDATA(obj) = svg;

    // Initialize state
    svg->visible = 1;
    svg->scale = 1.0f;
    svg->opacity = 1.0f;
    svg->color[0] = 1.0f;  // Default white tint
    svg->color[1] = 1.0f;
    svg->color[2] = 1.0f;
    svg->color[3] = 1.0f;
    svg->color_override = 0;  // Preserve original colors
    svg->rotation = 0.0f;
    svg->background_enabled = 0;
    svg->background_color[0] = 1.0f;
    svg->background_color[1] = 1.0f;
    svg->background_color[2] = 1.0f;
    svg->background_color[3] = 1.0f;
    
    // Initialize OpenGL resources
    if (init_svg_gl_resources(svg) < 0) {
        fprintf(getConsoleFP(), "error initializing SVG OpenGL resources\n");
        svgDelete(obj);
        return -1;
    }

    // Load SVG from string
    if (load_svg_from_string(svg, svg_data) < 0) {
        fprintf(getConsoleFP(), "error parsing SVG from string\n");
        svgDelete(obj);
        return -1;
    }
    
    return gobjAddObj(objlist, obj);
}

// Tcl command implementations

static int svgCmd(ClientData clientData, Tcl_Interp *interp,
                  int argc, char *argv[]) {
    OBJ_LIST *olist = (OBJ_LIST *) clientData;
    int id;
    int is_svg_data = 0;

    if (argc < 2) {
        Tcl_AppendResult(interp, "usage: ", argv[0], " svgfile_or_data", NULL);
        return TCL_ERROR;
    }

    // See if argument contains <svg> tag and then treat as svg_data directly
    const char *input = argv[1];
    if (strncmp(input, "<svg", 4) == 0 || strstr(input, "<svg") != NULL) {
      is_svg_data = 1;
    }
    
    if (is_svg_data) {
        if ((id = svgCreateFromString(olist, argv[1])) < 0) {
            Tcl_SetResult(interp, "error parsing SVG data", TCL_STATIC);
            return TCL_ERROR;
        }
    } else {
        if ((id = svgCreate(olist, argv[1])) < 0) {
            Tcl_SetResult(interp, "error loading SVG file", TCL_STATIC);
            return TCL_ERROR;
        }
    }

    Tcl_SetObjResult(interp, Tcl_NewIntObj(id));
    return TCL_OK;
}

static int svginfoCmd(ClientData clientData, Tcl_Interp *interp,
                      int argc, char *argv[]) {
    OBJ_LIST *olist = (OBJ_LIST *) clientData;
    SVG_OBJ *svg;
    int id;
    Tcl_Obj *dictObj;

    if (argc < 2) {
        Tcl_AppendResult(interp, "usage: ", argv[0], " id", NULL);
        return TCL_ERROR;
    }

    if (Tcl_GetInt(interp, argv[1], &id) != TCL_OK) return TCL_ERROR;
    if (id >= OL_NOBJS(olist) || GR_OBJTYPE(OL_OBJ(olist, id)) != SvgID) {
        Tcl_AppendResult(interp, argv[0], ": invalid SVG object", NULL);
        return TCL_ERROR;
    }

    svg = GR_CLIENTDATA(OL_OBJ(olist, id));

    dictObj = Tcl_NewDictObj();
    Tcl_DictObjPut(interp, dictObj, Tcl_NewStringObj("width", -1), 
                   Tcl_NewIntObj(svg->width));
    Tcl_DictObjPut(interp, dictObj, Tcl_NewStringObj("height", -1), 
                   Tcl_NewIntObj(svg->height));
    Tcl_DictObjPut(interp, dictObj, Tcl_NewStringObj("aspect_ratio", -1), 
                   Tcl_NewDoubleObj(svg->aspect_ratio));
    Tcl_DictObjPut(interp, dictObj, Tcl_NewStringObj("scale", -1), 
                   Tcl_NewDoubleObj(svg->scale));

    Tcl_SetObjResult(interp, dictObj);
    return TCL_OK;
}

static int svgscaleCmd(ClientData clientData, Tcl_Interp *interp,
                       int argc, char *argv[]) {
    OBJ_LIST *olist = (OBJ_LIST *) clientData;
    SVG_OBJ *svg;
    int id;
    double scale;

    if (argc < 2) {
        Tcl_AppendResult(interp, "usage: ", argv[0], " id [scale]", NULL);
        return TCL_ERROR;
    }

    if (Tcl_GetInt(interp, argv[1], &id) != TCL_OK) return TCL_ERROR;
    if (id >= OL_NOBJS(olist) || GR_OBJTYPE(OL_OBJ(olist, id)) != SvgID) {
        Tcl_AppendResult(interp, argv[0], ": invalid SVG object", NULL);
        return TCL_ERROR;
    }

    svg = GR_CLIENTDATA(OL_OBJ(olist, id));
    
    if (argc == 2) {
        Tcl_SetObjResult(interp, Tcl_NewDoubleObj(svg->scale));
        return TCL_OK;
    }
    
    if (Tcl_GetDouble(interp, argv[2], &scale) != TCL_OK) return TCL_ERROR;
    
    svg->scale = (float)fmax(0.1, fmin(10.0, scale));
    
    // Re-rasterize at new scale
    int raster_size = (int)(fmaxf(svg->svg_image->width, svg->svg_image->height) * svg->scale);
    raster_size = fmaxf(64, fminf(4096, raster_size));
    rasterize_svg(svg, raster_size, raster_size);
    
    return TCL_OK;
}

static int svgopacityCmd(ClientData clientData, Tcl_Interp *interp,
                         int argc, char *argv[]) {
    OBJ_LIST *olist = (OBJ_LIST *) clientData;
    SVG_OBJ *svg;
    int id;
    double opacity;

    if (argc < 2) {
        Tcl_AppendResult(interp, "usage: ", argv[0], " id [opacity]", NULL);
        return TCL_ERROR;
    }

    if (Tcl_GetInt(interp, argv[1], &id) != TCL_OK) return TCL_ERROR;
    if (id >= OL_NOBJS(olist) || GR_OBJTYPE(OL_OBJ(olist, id)) != SvgID) {
        Tcl_AppendResult(interp, argv[0], ": invalid SVG object", NULL);
        return TCL_ERROR;
    }

    svg = GR_CLIENTDATA(OL_OBJ(olist, id));
    
    if (argc == 2) {
        Tcl_SetObjResult(interp, Tcl_NewDoubleObj(svg->opacity));
        return TCL_OK;
    }
    
    if (Tcl_GetDouble(interp, argv[2], &opacity) != TCL_OK) return TCL_ERROR;
    svg->opacity = (float)fmax(0.0, fmin(1.0, opacity));
    
    return TCL_OK;
}

static int svgcolorCmd(ClientData clientData, Tcl_Interp *interp,
                       int argc, char *argv[]) {
    OBJ_LIST *olist = (OBJ_LIST *) clientData;
    SVG_OBJ *svg;
    int id, override_mode;
    double r, g, b, a;

    if (argc < 2) {
        Tcl_AppendResult(interp, "usage: ", argv[0], " id [override_mode r g b a]", NULL);
        return TCL_ERROR;
    }

    if (Tcl_GetInt(interp, argv[1], &id) != TCL_OK) return TCL_ERROR;
    if (id >= OL_NOBJS(olist) || GR_OBJTYPE(OL_OBJ(olist, id)) != SvgID) {
        Tcl_AppendResult(interp, argv[0], ": invalid SVG object", NULL);
        return TCL_ERROR;
    }

    svg = GR_CLIENTDATA(OL_OBJ(olist, id));
    
    if (argc == 2) {
        Tcl_Obj *listObj = Tcl_NewListObj(0, NULL);
        Tcl_ListObjAppendElement(interp, listObj, Tcl_NewIntObj(svg->color_override));
        Tcl_ListObjAppendElement(interp, listObj, Tcl_NewDoubleObj(svg->color[0]));
        Tcl_ListObjAppendElement(interp, listObj, Tcl_NewDoubleObj(svg->color[1]));
        Tcl_ListObjAppendElement(interp, listObj, Tcl_NewDoubleObj(svg->color[2]));
        Tcl_ListObjAppendElement(interp, listObj, Tcl_NewDoubleObj(svg->color[3]));
        Tcl_SetObjResult(interp, listObj);
        return TCL_OK;
    }
    
    if (argc < 7) {
        Tcl_AppendResult(interp, "usage: ", argv[0], " id override_mode r g b a", NULL);
        return TCL_ERROR;
    }
    
    if (Tcl_GetInt(interp, argv[2], &override_mode) != TCL_OK) return TCL_ERROR;
    if (Tcl_GetDouble(interp, argv[3], &r) != TCL_OK) return TCL_ERROR;
    if (Tcl_GetDouble(interp, argv[4], &g) != TCL_OK) return TCL_ERROR;
    if (Tcl_GetDouble(interp, argv[5], &b) != TCL_OK) return TCL_ERROR;
    if (Tcl_GetDouble(interp, argv[6], &a) != TCL_OK) return TCL_ERROR;
    
    svg->color_override = fmax(0, fmin(2, override_mode));
    svg->color[0] = (float)fmax(0.0, fmin(1.0, r));
    svg->color[1] = (float)fmax(0.0, fmin(1.0, g));
    svg->color[2] = (float)fmax(0.0, fmin(1.0, b));
    svg->color[3] = (float)fmax(0.0, fmin(1.0, a));
    
    return TCL_OK;
}

static int svgbackgroundCmd(ClientData clientData, Tcl_Interp *interp,
                           int argc, char *argv[]) {
    OBJ_LIST *olist = (OBJ_LIST *) clientData;
    SVG_OBJ *svg;
    int id, enabled;
    double r, g, b, a;

    if (argc < 2) {
        Tcl_AppendResult(interp, "usage: ", argv[0], " id [enabled r g b a]", NULL);
        return TCL_ERROR;
    }

    if (Tcl_GetInt(interp, argv[1], &id) != TCL_OK) return TCL_ERROR;
    if (id >= OL_NOBJS(olist) || GR_OBJTYPE(OL_OBJ(olist, id)) != SvgID) {
        Tcl_AppendResult(interp, argv[0], ": invalid SVG object", NULL);
        return TCL_ERROR;
    }

    svg = GR_CLIENTDATA(OL_OBJ(olist, id));
    
    if (argc == 2) {
        Tcl_Obj *listObj = Tcl_NewListObj(0, NULL);
        Tcl_ListObjAppendElement(interp, listObj, Tcl_NewIntObj(svg->background_enabled));
        Tcl_ListObjAppendElement(interp, listObj, Tcl_NewDoubleObj(svg->background_color[0]));
        Tcl_ListObjAppendElement(interp, listObj, Tcl_NewDoubleObj(svg->background_color[1]));
        Tcl_ListObjAppendElement(interp, listObj, Tcl_NewDoubleObj(svg->background_color[2]));
        Tcl_ListObjAppendElement(interp, listObj, Tcl_NewDoubleObj(svg->background_color[3]));
        Tcl_SetObjResult(interp, listObj);
        return TCL_OK;
    }
    
    if (argc < 7) {
        Tcl_AppendResult(interp, "usage: ", argv[0], " id enabled r g b a", NULL);
        return TCL_ERROR;
    }
    
    if (Tcl_GetInt(interp, argv[2], &enabled) != TCL_OK) return TCL_ERROR;
    if (Tcl_GetDouble(interp, argv[3], &r) != TCL_OK) return TCL_ERROR;
    if (Tcl_GetDouble(interp, argv[4], &g) != TCL_OK) return TCL_ERROR;
    if (Tcl_GetDouble(interp, argv[5], &b) != TCL_OK) return TCL_ERROR;
    if (Tcl_GetDouble(interp, argv[6], &a) != TCL_OK) return TCL_ERROR;
    
    svg->background_enabled = enabled ? 1 : 0;
    svg->background_color[0] = (float)fmax(0.0, fmin(1.0, r));
    svg->background_color[1] = (float)fmax(0.0, fmin(1.0, g));
    svg->background_color[2] = (float)fmax(0.0, fmin(1.0, b));
    svg->background_color[3] = (float)fmax(0.0, fmin(1.0, a));
    
    // Re-rasterize with new background
    if (svg->svg_image && svg->raster_width > 0 && svg->raster_height > 0) {
        rasterize_svg(svg, svg->raster_width, svg->raster_height);
    }
    
    return TCL_OK;
}

#ifdef _WIN32
EXPORT(int, Svg_Init) (Tcl_Interp *interp)
#else
int Svg_Init(Tcl_Interp *interp)
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

    if (SvgID < 0) {
        SvgID = gobjRegisterType();
        
        // Load OpenGL functions
        gladLoadGL();
        
        // Create shader program once for all SVG instances
        if (create_svg_shader_program() < 0) {
            Tcl_SetResult(interp, "error creating SVG shader program", TCL_STATIC);
            return TCL_ERROR;
        }
    }

    Tcl_CreateCommand(interp, "svg", (Tcl_CmdProc *) svgCmd,
                      (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
    
    Tcl_CreateCommand(interp, "svgInfo", (Tcl_CmdProc *) svginfoCmd,
                      (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
    Tcl_CreateCommand(interp, "svgScale", (Tcl_CmdProc *) svgscaleCmd,
                      (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
    Tcl_CreateCommand(interp, "svgOpacity", (Tcl_CmdProc *) svgopacityCmd,
                      (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
    Tcl_CreateCommand(interp, "svgColor", (Tcl_CmdProc *) svgcolorCmd,
                      (ClientData) OBJList, (Tcl_CmdDeleteProc *) NULL);
    Tcl_CreateCommand(interp, "svgBackground", (Tcl_CmdProc *) svgbackgroundCmd,
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
