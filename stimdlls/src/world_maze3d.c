/*
 * world_maze3d.c
 *
 * First-person 3D maze rendering from 2D tilemap data.
 * Converts a top-down tile grid into textured wall/floor/ceiling geometry
 * for VR-style navigation experiments.
 *
 * Design:
 *   - Reuses existing World struct, atlases, and tilemap loading
 *   - The collision layer from worldLoadTMX defines wall cells
 *   - Only wall faces adjacent to empty cells are generated
 *   - First-person camera with position (x,z) on the ground plane, yaw/pitch
 *   - Separate shader with fog + basic lighting for depth cues
 *   - Box2D dynamic body for camera collision (reuses existing wall bodies)
 *   - Fallback grid-based collision when physics disabled
 *
 * Coordinate mapping (2D tilemap -> 3D maze):
 *   tilemap grid X  ->  3D X
 *   tilemap grid Y  ->  3D Z  (depth)
 *   wall height     ->  3D Y  (up)
 *
 *   Box2D XY plane maps to maze XZ ground plane:
 *     Box2D X = maze X,  Box2D Y = maze Z
 *
 * Author: DLS 2025
 */

#include "world_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/*========================================================================
 * Constants
 *========================================================================*/

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define MAZE3D_FLOATS_PER_VERT 8   /* pos(3) + uv(2) + normal(3) */
#define MAZE3D_VERTS_PER_FACE  6   /* 2 triangles */
#define MAZE3D_FACE_STRIDE     (MAZE3D_VERTS_PER_FACE * MAZE3D_FLOATS_PER_VERT)
#define MAZE3D_MAX_ITEMS       256
#define MAZE3D_ITEM_VERTS_PER  6   /* 2 triangles per billboard */

/*========================================================================
 * Maze Item (3D billboard sprite in maze space)
 *========================================================================*/

typedef struct {
    char  name[64];
    float x, z;                /* maze coordinates (XZ plane) */
    float y_offset;            /* height above floor (0 = floor level) */
    float size;                /* world-space billboard width */
    int   sprite_sheet_id;     /* index into w->sprite_sheets[] */
    int   current_frame;
    int   visible;
    int   active;              /* 0 = slot unused */

    /* Animation (same model as Sprite) */
    int   anim_frames[32];
    int   anim_frame_count;
    int   anim_current_frame;
    float anim_fps;
    float anim_time;
    int   anim_loop;
    int   anim_playing;

    /* Pickup */
    float pickup_radius;       /* distance for collection trigger */

    /* Visual effects */
    float bob_phase;           /* random phase offset for hover bob */
    float bob_amplitude;       /* bob height (world units), 0 = no bob */
    float bob_speed;           /* bob frequency (Hz) */
    float spin_speed;          /* Y-axis spin (rad/s), 0 = face camera */
    float spin_angle;          /* current Y rotation */
} MazeItem;

/*========================================================================
 * Maze3D State
 *========================================================================*/

struct Maze3D {
    /* Camera */
    float cam_x, cam_y, cam_z;      /* eye position in 3D space */
    float cam_yaw, cam_pitch;        /* radians */
    float eye_height;                /* above floor */
    float move_speed;                /* m/s */
    float turn_speed;                /* rad/s */
    float fov_degrees;               /* vertical FOV */

    /* Physics camera body (circle on XZ ground plane via Box2D XY) */
    b2BodyId cam_body;
    int      has_cam_body;
    float    cam_radius;             /* collision circle radius */
    float    cam_damping;            /* linear damping for stopping */
    int      use_physics;            /* 1=Box2D, 0=grid collision */

    /* Maze geometry */
    float wall_height;
    int   draw_floor;
    int   draw_ceiling;

    /* Fog */
    float fog_start, fog_end;
    float fog_color[4];
    float ambient_light;

    /* Grid data - binary wall map extracted from tilemap */
    int   grid_w, grid_h;
    int  *grid;                      /* 1=wall, 0=empty */
    float cell_size;                 /* world units per cell */

    /* GL resources */
    GLuint shader;
    GLuint vao, vbo;
    GLint  u_proj, u_view, u_texture;
    GLint  u_fog_start, u_fog_end, u_fog_color;
    GLint  u_ambient;
    int    face_count;
    int    total_verts;
    int    dirty;

    /* Texture tile UVs (from atlas) */
    int    wall_atlas_id, floor_atlas_id, ceiling_atlas_id;
    float  wall_u0, wall_v0, wall_u1, wall_v1;
    float  floor_u0, floor_v0, floor_u1, floor_v1;
    float  ceil_u0, ceil_v0, ceil_u1, ceil_v1;

    int    enabled;

    /* 2D map marker */
    GLuint marker_tex;   /* 1x1 white texture for solid-color marker */

    /* Items (3D billboard sprites) */
    MazeItem items[MAZE3D_MAX_ITEMS];
    int      item_count;             /* next slot to try; items can be sparse */
    GLuint   item_vao, item_vbo;     /* separate VBO for billboard quads */
    float    item_bob_time;          /* global time accumulator for bob */
    char     item_callback[256];     /* Tcl proc called on pickup */
};

/*========================================================================
 * Shader Sources
 *========================================================================*/

#ifdef STIM2_USE_GLES
#define GLSL_VER "#version 300 es\nprecision mediump float;\n"
#else
#define GLSL_VER "#version 330 core\n"
#endif

static const char *maze3d_vs_src =
    GLSL_VER
    "layout(location=0) in vec3 aPos;\n"
    "layout(location=1) in vec2 aUV;\n"
    "layout(location=2) in vec3 aNormal;\n"
    "out vec2 vUV;\n"
    "out float vFogFactor;\n"
    "out float vLight;\n"
    "uniform mat4 projMat, viewMat;\n"
    "uniform float fogStart, fogEnd, ambient;\n"
    "void main() {\n"
    "  vec4 viewPos = viewMat * vec4(aPos, 1.0);\n"
    "  gl_Position = projMat * viewPos;\n"
    "  vUV = aUV;\n"
    "  float dist = length(viewPos.xyz);\n"
    "  vFogFactor = clamp((fogEnd - dist) / (fogEnd - fogStart), 0.0, 1.0);\n"
    "  vec3 lightDir = normalize(vec3(0.2, 1.0, 0.3));\n"
    "  vLight = max(dot(aNormal, lightDir), 0.0) * (1.0 - ambient) + ambient;\n"
    "}\n";

static const char *maze3d_fs_src =
    GLSL_VER
    "in vec2 vUV;\n"
    "in float vFogFactor;\n"
    "in float vLight;\n"
    "out vec4 fragColor;\n"
    "uniform sampler2D atlas;\n"
    "uniform vec4 fogColor;\n"
    "void main() {\n"
    "  vec4 tex = texture(atlas, vUV);\n"
    "  if (tex.a < 0.1) discard;\n"
    "  vec3 lit = tex.rgb * vLight;\n"
    "  fragColor = vec4(mix(fogColor.rgb, lit, vFogFactor), tex.a);\n"
    "}\n";

/*========================================================================
 * Shader Compilation
 *========================================================================*/

static GLuint maze3d_compile(GLenum type, const char *src)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(s, 512, NULL, log);
        fprintf(stderr, "maze3d shader: %s\n", log);
        return 0;
    }
    return s;
}

static int maze3d_init_gl(Maze3D *m)
{
    GLuint vs = maze3d_compile(GL_VERTEX_SHADER, maze3d_vs_src);
    GLuint fs = maze3d_compile(GL_FRAGMENT_SHADER, maze3d_fs_src);
    if (!vs || !fs) return -1;

    m->shader = glCreateProgram();
    glAttachShader(m->shader, vs);
    glAttachShader(m->shader, fs);
    glLinkProgram(m->shader);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint ok;
    glGetProgramiv(m->shader, GL_LINK_STATUS, &ok);
    if (!ok) return -1;

    m->u_proj      = glGetUniformLocation(m->shader, "projMat");
    m->u_view      = glGetUniformLocation(m->shader, "viewMat");
    m->u_texture   = glGetUniformLocation(m->shader, "atlas");
    m->u_fog_start = glGetUniformLocation(m->shader, "fogStart");
    m->u_fog_end   = glGetUniformLocation(m->shader, "fogEnd");
    m->u_fog_color = glGetUniformLocation(m->shader, "fogColor");
    m->u_ambient   = glGetUniformLocation(m->shader, "ambient");

    glGenVertexArrays(1, &m->vao);
    glGenBuffers(1, &m->vbo);
    glBindVertexArray(m->vao);
    glBindBuffer(GL_ARRAY_BUFFER, m->vbo);

    int stride = MAZE3D_FLOATS_PER_VERT * sizeof(float);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, (void*)(3*sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, stride, (void*)(5*sizeof(float)));
    glBindVertexArray(0);

    /* 1x1 white texture for 2D marker */
    unsigned char white[4] = {255, 255, 255, 255};
    glGenTextures(1, &m->marker_tex);
    glBindTexture(GL_TEXTURE_2D, m->marker_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
    glBindTexture(GL_TEXTURE_2D, 0);

    /* Item billboard VBO — same vertex format as walls (pos3 + uv2 + normal3) */
    glGenVertexArrays(1, &m->item_vao);
    glGenBuffers(1, &m->item_vbo);
    glBindVertexArray(m->item_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m->item_vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 MAZE3D_MAX_ITEMS * MAZE3D_ITEM_VERTS_PER * MAZE3D_FLOATS_PER_VERT * sizeof(float),
                 NULL, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, (void*)(3*sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, stride, (void*)(5*sizeof(float)));
    glBindVertexArray(0);

    return 0;
}

/*========================================================================
 * Matrix Utilities
 *========================================================================*/

static void mat4_perspective(float *m, float fov_rad, float aspect,
                             float znear, float zfar)
{
    memset(m, 0, 16 * sizeof(float));
    float f = 1.0f / tanf(fov_rad * 0.5f);
    m[0]  = f / aspect;
    m[5]  = f;
    m[10] = (zfar + znear) / (znear - zfar);
    m[11] = -1.0f;
    m[14] = (2.0f * zfar * znear) / (znear - zfar);
}

static void mat4_fps_view(float *m, float cx, float cy, float cz,
                          float yaw, float pitch)
{
    float cp = cosf(pitch), sp = sinf(pitch);
    float cyw = cosf(yaw),  sy = sinf(yaw);

    /* Forward (where camera looks) */
    float fx = -sy * cp, fy = sp, fz = -cyw * cp;
    /* Right */
    float rx = cyw, ry = 0.0f, rz = -sy;
    /* Up = right x forward */
    float ux = ry*fz - rz*fy;
    float uy = rz*fx - rx*fz;
    float uz = rx*fy - ry*fx;

    memset(m, 0, 16 * sizeof(float));
    m[0] = rx;  m[4] = ry;  m[8]  = rz;
    m[1] = ux;  m[5] = uy;  m[9]  = uz;
    m[2] = -fx; m[6] = -fy; m[10] = -fz;
    m[12] = -(rx*cx + ry*cy + rz*cz);
    m[13] = -(ux*cx + uy*cy + uz*cz);
    m[14] = -((-fx)*cx + (-fy)*cy + (-fz)*cz);
    m[15] = 1.0f;
}

/*========================================================================
 * Grid Extraction from Tilemap
 *========================================================================*/

/*
 * Build binary wall grid from loaded tilemap data.
 * Uses is_collision flag (set on ALL collision-layer tiles) rather than
 * has_body (which is only set on the first tile of a merged run).
 * Called after worldLoadTMX.
 */
static void maze3d_extract_grid(World *w, Maze3D *m)
{
    if (m->grid) { free(m->grid); m->grid = NULL; }

    m->grid_w = w->map_width;
    m->grid_h = w->map_height;
    m->cell_size = w->tile_size;

    int n = m->grid_w * m->grid_h;
    m->grid = calloc(n, sizeof(int));

    int wall_count = 0;
    for (int i = 0; i < w->tile_count; i++) {
        TileInstance *t = &w->tiles[i];
        if (!t->is_collision) continue;

        /*
         * Reverse tile center position to grid coords.
         * Tile positions are in world coords: Y-flipped, with offset.
         *   world X = (tx + 0.5) * tile_size + offset_x
         *   world Y = (map_height - ty - 0.5) * tile_size + offset_y
         *
         * Solve back to grid (tx, ty):
         *   tx = (world_X - offset_x) / tile_size - 0.5
         *   ty = map_height - 0.5 - (world_Y - offset_y) / tile_size
         */
        float px = t->x - w->offset_x;
        float py = t->y - w->offset_y;

        int gx = (int)(px / w->tile_size);
        int gy = w->map_height - 1 - (int)(py / w->tile_size);

        if (gx >= 0 && gx < m->grid_w && gy >= 0 && gy < m->grid_h) {
            m->grid[gy * m->grid_w + gx] = 1;
            wall_count++;
        }
    }

    fprintf(stderr, "maze3d: grid %dx%d, cell_size=%.3f, %d wall cells from %d tiles\n",
            m->grid_w, m->grid_h, m->cell_size, wall_count, w->tile_count);

    m->dirty = 1;
}

/*========================================================================
 * Coordinate Conversion: Maze <-> Box2D World
 *========================================================================*/

/*
 * The 3D maze uses a coordinate system where:
 *   maze X  = grid column * cell_size         (left to right)
 *   maze Z  = grid row * cell_size            (top to bottom in TMX row order)
 *
 * Box2D wall bodies from worldLoadTMX use world coordinates:
 *   b2 X   = (tx + 0.5) * tile_size + offset_x
 *   b2 Y   = (map_height - ty - 0.5) * tile_size + offset_y    (Y-up)
 *
 * Maze grid cell (gx, gy) corresponds to TMX grid (tx=gx, ty=gy), so:
 *   maze X = (gx + 0.5) * cell_size  for cell center
 *   maze Z = (gy + 0.5) * cell_size  for cell center
 *
 * Conversion:
 *   b2 X = maze_x + offset_x                              (X is same direction)
 *   b2 Y = (map_height * cell_size) - maze_z + offset_y   (Y is flipped)
 */
static float maze_x_to_b2x(World *w, Maze3D *m, float mx)
{
    (void)m;
    return mx + w->offset_x;
}

static float maze_z_to_b2y(World *w, Maze3D *m, float mz)
{
    return (w->map_height * m->cell_size) - mz + w->offset_y;
}

static float b2x_to_maze_x(World *w, Maze3D *m, float b2x)
{
    (void)m;
    return b2x - w->offset_x;
}

static float b2y_to_maze_z(World *w, Maze3D *m, float b2y)
{
    return (w->map_height * m->cell_size) - (b2y - w->offset_y);
}

/*========================================================================
 * Camera Body (Box2D)
 *========================================================================*/

/*
 * Create a dynamic circle body in the existing Box2D world.
 * The body is positioned in Box2D world coords (matching TMX wall bodies).
 * Maze-space cam_x/cam_z are converted on creation and back each frame.
 */
static void maze3d_create_cam_body(World *w, Maze3D *m)
{
    if (!w->has_world) return;
    if (m->has_cam_body && b2Body_IsValid(m->cam_body))
        b2DestroyBody(m->cam_body);

    b2BodyDef bd = b2DefaultBodyDef();
    bd.type = b2_dynamicBody;
    bd.position = (b2Vec2){
        maze_x_to_b2x(w, m, m->cam_x),
        maze_z_to_b2y(w, m, m->cam_z)
    };
    bd.gravityScale = 0.0f;          /* no gravity in maze XZ plane */
    bd.linearDamping = m->cam_damping;
    bd.motionLocks.angularZ = true;  /* fixedRotation - yaw is manual */
    bd.isBullet = true;              /* CCD to avoid tunneling thin walls */

    m->cam_body = b2CreateBody(w->world_id, &bd);

    b2ShapeDef sd = b2DefaultShapeDef();
    sd.density = 1.0f;
    sd.userData = (void *)"player";
    sd.enableContactEvents = true;
    sd.enableSensorEvents = true;

    b2Circle circle = { .center = {0, 0}, .radius = m->cam_radius };
    b2ShapeId shape = b2CreateCircleShape(m->cam_body, &sd, &circle);
    b2Shape_SetFriction(shape, 0.0f);
    b2Shape_SetRestitution(shape, 0.0f);

    m->has_cam_body = 1;
}

static void maze3d_destroy_cam_body(Maze3D *m)
{
    if (m->has_cam_body && b2Body_IsValid(m->cam_body)) {
        b2DestroyBody(m->cam_body);
    }
    m->has_cam_body = 0;
}

/*
 * Sync camera position from Box2D body after b2World_Step.
 * Converts Box2D world coords back to maze coords.
 * Also keeps the 2D camera centered on the player so toggling
 * to the map view shows the correct location.
 */
void maze3d_sync_camera(World *w, Maze3D *m)
{
    if (!m) return;

    /* Physics mode: read position back from Box2D body */
    if (m->has_cam_body && b2Body_IsValid(m->cam_body)) {
        b2Vec2 pos = b2Body_GetPosition(m->cam_body);
        m->cam_x = b2x_to_maze_x(w, m, pos.x);
        m->cam_z = b2y_to_maze_z(w, m, pos.y);
    }
    m->cam_y = m->eye_height;

    /* Keep 2D camera in sync (convert maze coords to world coords) */
    float wx = maze_x_to_b2x(w, m, m->cam_x);
    float wy = maze_z_to_b2y(w, m, m->cam_z);
    w->camera.x = wx;
    w->camera.y = wy;
    w->camera.target_x = wx;
    w->camera.target_y = wy;
}

/*========================================================================
 * Movement
 *========================================================================*/

/*
 * Grid-based collision fallback (when use_physics=0).
 * Checks all four corners of the camera bounding box against the wall grid.
 */
static int maze3d_grid_blocked(Maze3D *m, float x, float z)
{
    float r = m->cam_radius;
    float corners[4][2] = {
        { x - r, z - r }, { x + r, z - r },
        { x - r, z + r }, { x + r, z + r }
    };
    for (int i = 0; i < 4; i++) {
        int gx = (int)(corners[i][0] / m->cell_size);
        int gy = (int)(corners[i][1] / m->cell_size);
        if (gx < 0 || gx >= m->grid_w || gy < 0 || gy >= m->grid_h) return 1;
        if (m->grid[gy * m->grid_w + gx]) return 1;
    }
    return 0;
}

static void maze3d_move_grid(Maze3D *m, float forward, float strafe, float dt)
{
    float dx = -sinf(m->cam_yaw) * forward + cosf(m->cam_yaw) * strafe;
    float dz = -cosf(m->cam_yaw) * forward - sinf(m->cam_yaw) * strafe;
    float speed = m->move_speed * dt;
    float nx = m->cam_x + dx * speed;
    float nz = m->cam_z + dz * speed;

    /* Axis-separated collision for wall sliding */
    if (!maze3d_grid_blocked(m, nx, m->cam_z))
        m->cam_x = nx;
    if (!maze3d_grid_blocked(m, m->cam_x, nz))
        m->cam_z = nz;
}

void maze3d_rotate(Maze3D *m, float dyaw, float dpitch)
{
    m->cam_yaw += dyaw;
    m->cam_pitch += dpitch;
    if (m->cam_pitch >  1.4f) m->cam_pitch =  1.4f;
    if (m->cam_pitch < -1.4f) m->cam_pitch = -1.4f;
}

/*========================================================================
 * Geometry Generation
 *========================================================================*/

static int emit_quad(float *buf, int i,
                     float x0, float y0, float z0,
                     float x1, float y1, float z1,
                     float x2, float y2, float z2,
                     float x3, float y3, float z3,
                     float u0, float v0, float u1, float v1,
                     float nx, float ny, float nz)
{
    /* tri 1: v0 v1 v2 */
    buf[i++]=x0; buf[i++]=y0; buf[i++]=z0; buf[i++]=u0; buf[i++]=v1; buf[i++]=nx; buf[i++]=ny; buf[i++]=nz;
    buf[i++]=x1; buf[i++]=y1; buf[i++]=z1; buf[i++]=u1; buf[i++]=v1; buf[i++]=nx; buf[i++]=ny; buf[i++]=nz;
    buf[i++]=x2; buf[i++]=y2; buf[i++]=z2; buf[i++]=u1; buf[i++]=v0; buf[i++]=nx; buf[i++]=ny; buf[i++]=nz;
    /* tri 2: v0 v2 v3 */
    buf[i++]=x0; buf[i++]=y0; buf[i++]=z0; buf[i++]=u0; buf[i++]=v1; buf[i++]=nx; buf[i++]=ny; buf[i++]=nz;
    buf[i++]=x2; buf[i++]=y2; buf[i++]=z2; buf[i++]=u1; buf[i++]=v0; buf[i++]=nx; buf[i++]=ny; buf[i++]=nz;
    buf[i++]=x3; buf[i++]=y3; buf[i++]=z3; buf[i++]=u0; buf[i++]=v0; buf[i++]=nx; buf[i++]=ny; buf[i++]=nz;
    return i;
}

static int cell_is_wall(Maze3D *m, int gx, int gy)
{
    if (gx < 0 || gx >= m->grid_w || gy < 0 || gy >= m->grid_h) return 0;
    return m->grid[gy * m->grid_w + gx];
}

static void maze3d_rebuild(Maze3D *m)
{
    float cs = m->cell_size;
    float wh = m->wall_height;

    int max_quads = m->grid_w * m->grid_h * 8;
    float *buf = malloc(max_quads * MAZE3D_FACE_STRIDE * sizeof(float));
    int vi = 0, faces = 0;

    float wu0 = m->wall_u0, wv0 = m->wall_v0, wu1 = m->wall_u1, wv1 = m->wall_v1;
    float fu0 = m->floor_u0, fv0 = m->floor_v0, fu1 = m->floor_u1, fv1 = m->floor_v1;
    float cu0 = m->ceil_u0, cv0 = m->ceil_v0, cu1 = m->ceil_u1, cv1 = m->ceil_v1;

    for (int gy = 0; gy < m->grid_h; gy++) {
        for (int gx = 0; gx < m->grid_w; gx++) {
            float x0 = gx * cs, z0 = gy * cs;
            float x1 = x0 + cs, z1 = z0 + cs;

            if (cell_is_wall(m, gx, gy)) {
                /* North face (-Z): viewer at z < z0 looking toward +Z */
                if (!cell_is_wall(m, gx, gy-1)) {
                    vi = emit_quad(buf, vi,
                        x1,0,z0, x0,0,z0, x0,wh,z0, x1,wh,z0,
                        wu0,wv0,wu1,wv1, 0,0,-1); faces++;
                }
                /* South face (+Z): viewer at z > z1 looking toward -Z */
                if (!cell_is_wall(m, gx, gy+1)) {
                    vi = emit_quad(buf, vi,
                        x0,0,z1, x1,0,z1, x1,wh,z1, x0,wh,z1,
                        wu0,wv0,wu1,wv1, 0,0,1); faces++;
                }
                /* West face (-X): viewer at x < x0 looking toward +X */
                if (!cell_is_wall(m, gx-1, gy)) {
                    vi = emit_quad(buf, vi,
                        x0,0,z0, x0,0,z1, x0,wh,z1, x0,wh,z0,
                        wu0,wv0,wu1,wv1, -1,0,0); faces++;
                }
                /* East face (+X): viewer at x > x1 looking toward -X */
                if (!cell_is_wall(m, gx+1, gy)) {
                    vi = emit_quad(buf, vi,
                        x1,0,z1, x1,0,z0, x1,wh,z0, x1,wh,z1,
                        wu0,wv0,wu1,wv1, 1,0,0); faces++;
                }
            } else {
                /* Floor */
                if (m->draw_floor) {
                    vi = emit_quad(buf, vi,
                        x0,0,z1, x1,0,z1, x1,0,z0, x0,0,z0,
                        fu0,fv0,fu1,fv1, 0,1,0); faces++;
                }
                /* Ceiling */
                if (m->draw_ceiling) {
                    vi = emit_quad(buf, vi,
                        x0,wh,z0, x1,wh,z0, x1,wh,z1, x0,wh,z1,
                        cu0,cv0,cu1,cv1, 0,-1,0); faces++;
                }
            }
        }
    }

    m->face_count = faces;
    m->total_verts = faces * 6;

    glBindBuffer(GL_ARRAY_BUFFER, m->vbo);
    glBufferData(GL_ARRAY_BUFFER, vi * sizeof(float), buf, GL_STATIC_DRAW);
    free(buf);
    m->dirty = 0;
}

/*========================================================================
 * Render
 *========================================================================*/

void maze3d_render(World *w, Maze3D *m)
{
    if (!m->enabled || m->total_verts == 0) return;
    if (m->dirty) maze3d_rebuild(m);

    glDepthMask(GL_TRUE);
    glClear(GL_DEPTH_BUFFER_BIT);

    float proj[16], view[16];
    int vp[4];
    glGetIntegerv(GL_VIEWPORT, vp);
    float aspect = (vp[2] > 0 && vp[3] > 0) ? (float)vp[2] / vp[3] : 1.0f;

    mat4_perspective(proj, m->fov_degrees * (float)M_PI / 180.0f,
                     aspect, 0.05f, 100.0f);
    mat4_fps_view(view, m->cam_x, m->cam_y, m->cam_z,
                  m->cam_yaw, m->cam_pitch);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);

    glUseProgram(m->shader);
    glUniformMatrix4fv(m->u_proj, 1, GL_FALSE, proj);
    glUniformMatrix4fv(m->u_view, 1, GL_FALSE, view);
    glUniform1f(m->u_fog_start, m->fog_start);
    glUniform1f(m->u_fog_end, m->fog_end);
    glUniform4fv(m->u_fog_color, 1, m->fog_color);
    glUniform1f(m->u_ambient, m->ambient_light);

    if (m->wall_atlas_id >= 0 && m->wall_atlas_id < w->atlas_count) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, w->atlases[m->wall_atlas_id].texture);
        glUniform1i(m->u_texture, 0);
    }

    glBindVertexArray(m->vao);
    glDrawArrays(GL_TRIANGLES, 0, m->total_verts);
    glBindVertexArray(0);

    /* ---- Render item billboards ---- */
    glDisable(GL_CULL_FACE);   /* billboards are double-sided */

    /*
     * Extract camera right and up vectors from the view matrix.
     * For a standard view matrix: right = (view[0], view[4], view[8]),
     * up = (view[1], view[5], view[9]).
     */
    float cam_right[3] = { view[0], view[4], view[8] };
    float cam_up[3]    = { view[1], view[5], view[9] };

    /* Build item billboard vertices into a temp buffer */
    float *item_verts = NULL;
    int item_vert_count = 0;
    int active_items = 0;

    for (int i = 0; i < MAZE3D_MAX_ITEMS; i++) {
        if (m->items[i].active && m->items[i].visible) active_items++;
    }

    if (active_items > 0) {
        item_verts = malloc(active_items * MAZE3D_ITEM_VERTS_PER * MAZE3D_FLOATS_PER_VERT * sizeof(float));
        int vi = 0;

        /* Group items by atlas for batched rendering */
        /* For now, draw all items — each is a camera-facing quad */
        for (int i = 0; i < MAZE3D_MAX_ITEMS; i++) {
            MazeItem *it = &m->items[i];
            if (!it->active || !it->visible) continue;
            if (it->sprite_sheet_id < 0 || it->sprite_sheet_id >= w->sprite_sheet_count) continue;

            SpriteSheet *ss = &w->sprite_sheets[it->sprite_sheet_id];
            if (it->current_frame < 0 || it->current_frame >= ss->frame_count) continue;

            SpriteFrame *sf = &ss->frames[it->current_frame];
            float u0 = sf->u0, v0 = sf->v0, u1 = sf->u1, v1 = sf->v1;

            /* Item center in 3D maze space */
            float bob = it->bob_amplitude * sinf(m->item_bob_time * it->bob_speed * 6.2832f + it->bob_phase);
            float cx = it->x;
            float cy = it->y_offset + it->size * 0.5f + bob;
            float cz = it->z;

            /* Billboard half-extents */
            /* Maintain aspect ratio from sprite frame */
            float aspect_ratio = (sf->h > 0) ? (float)sf->w / sf->h : 1.0f;
            float hw = it->size * 0.5f;
            float hh = hw / aspect_ratio;

            float rx = cam_right[0] * hw;
            float ry = cam_right[1] * hw;
            float rz = cam_right[2] * hw;
            float ux = cam_up[0] * hh;
            float uy = cam_up[1] * hh;
            float uz = cam_up[2] * hh;

            /* Normal pointing toward camera */
            float nx = -view[2], ny = -view[6], nz = -view[10];

            /* Four corners: BL, BR, TR, TL */
            float bl[3] = { cx - rx - ux, cy - ry - uy, cz - rz - uz };
            float br[3] = { cx + rx - ux, cy + ry - uy, cz + rz - uz };
            float tr[3] = { cx + rx + ux, cy + ry + uy, cz + rz + uz };
            float tl[3] = { cx - rx + ux, cy - ry + uy, cz - rz + uz };

            /* Two triangles: BL-BR-TR, BL-TR-TL */
#define ITEM_V(p, su, sv) \
    item_verts[vi++] = p[0]; item_verts[vi++] = p[1]; item_verts[vi++] = p[2]; \
    item_verts[vi++] = su;   item_verts[vi++] = sv; \
    item_verts[vi++] = nx;   item_verts[vi++] = ny;   item_verts[vi++] = nz;

            ITEM_V(bl, u0, v1);
            ITEM_V(br, u1, v1);
            ITEM_V(tr, u1, v0);
            ITEM_V(bl, u0, v1);
            ITEM_V(tr, u1, v0);
            ITEM_V(tl, u0, v0);
#undef ITEM_V
            item_vert_count += MAZE3D_ITEM_VERTS_PER;
        }

        if (item_vert_count > 0) {
            /* Items may use a different atlas than walls — bind per-batch.
             * For simplicity, bind the first active item's atlas.
             * TODO: sort by atlas and draw in batches if multiple atlases used.
             */
            for (int i = 0; i < MAZE3D_MAX_ITEMS; i++) {
                MazeItem *it = &m->items[i];
                if (!it->active || !it->visible) continue;
                SpriteSheet *ss = &w->sprite_sheets[it->sprite_sheet_id];
                if (ss->atlas_id >= 0 && ss->atlas_id < w->atlas_count) {
                    glBindTexture(GL_TEXTURE_2D, w->atlases[ss->atlas_id].texture);
                }
                break;
            }

            glBindVertexArray(m->item_vao);
            glBindBuffer(GL_ARRAY_BUFFER, m->item_vbo);
            glBufferSubData(GL_ARRAY_BUFFER, 0,
                            item_vert_count * MAZE3D_FLOATS_PER_VERT * sizeof(float),
                            item_verts);
            glDrawArrays(GL_TRIANGLES, 0, item_vert_count);
            glBindVertexArray(0);
        }
        free(item_verts);
    }

    glUseProgram(0);
    glDisable(GL_DEPTH_TEST);
}

/*========================================================================
 * Item Animation & Pickup Detection
 *========================================================================*/

/*
 * Update item animations and check for pickups.
 * Called from world_update_callback via maze3d_update_items.
 */
void maze3d_update_items(World *w, Maze3D *m, float dt)
{
    if (!m) return;

    m->item_bob_time += dt;

    for (int i = 0; i < MAZE3D_MAX_ITEMS; i++) {
        MazeItem *it = &m->items[i];
        if (!it->active) continue;

        /* Animation update — same logic as world_sprite_update_animation */
        if (it->anim_playing && it->anim_frame_count > 0) {
            it->anim_time += dt;
            float frame_dur = 1.0f / it->anim_fps;
            if (it->anim_time >= frame_dur) {
                it->anim_time -= frame_dur;
                it->anim_current_frame++;
                if (it->anim_current_frame >= it->anim_frame_count) {
                    if (it->anim_loop) {
                        it->anim_current_frame = 0;
                    } else {
                        it->anim_current_frame = it->anim_frame_count - 1;
                        it->anim_playing = 0;
                    }
                }
                it->current_frame = it->anim_frames[it->anim_current_frame];
            }
        }

        /* Spin */
        if (it->spin_speed != 0.0f) {
            it->spin_angle += it->spin_speed * dt;
        }

        /* Pickup detection — distance in XZ plane */
        if (it->visible && it->pickup_radius > 0.0f) {
            float dx = m->cam_x - it->x;
            float dz = m->cam_z - it->z;
            float dist_sq = dx * dx + dz * dz;
            float r = it->pickup_radius;
            if (dist_sq < r * r) {
                it->visible = 0;
                /* Fire Tcl callback */
                if (m->item_callback[0] != '\0' && w->interp) {
                    char cmd[512];
                    snprintf(cmd, sizeof(cmd), "%s %d {%s}",
                             m->item_callback, i, it->name);
                    Tcl_Eval(w->interp, cmd);
                }
            }
        }
    }
}

/*========================================================================
 * 2D Map Player Marker
 *========================================================================*/

/*
 * Draw a directional arrow marker on the 2D map view.
 * Uses the world's own shader and sprite VBO — same pipeline as sprites,
 * so projection, vertex format, and GL state are guaranteed to match.
 *
 * The world shader expects (x, y, u, v) per vertex with a texture sampler.
 * We pick a single texel from the wall texture as a "solid color" fill.
 */
void maze3d_render_2d_marker(World *w, Maze3D *m)
{
    if (!m || !m->grid || !w->shader_program || w->atlas_count == 0) return;

    /* Player position in world coords */
    float wx = maze_x_to_b2x(w, m, m->cam_x);
    float wy = maze_z_to_b2y(w, m, m->cam_z);

    /*
     * Yaw direction in world coords.
     * Maze forward at yaw=0 is (dx=0, dz=-1) in maze space.
     * World Y = flipped maze Z, so forward maps to (0, +1) in world XY.
     */
    float fwd_x = -sinf(m->cam_yaw);
    float fwd_y =  cosf(m->cam_yaw);

    /* Arrow size relative to tile */
    float sz = m->cell_size * 0.4f;

    /* Arrow: tip + two base corners */
    float tip_x  = wx + fwd_x * sz;
    float tip_y  = wy + fwd_y * sz;
    float perp_x = -fwd_y;
    float perp_y =  fwd_x;
    float base_sz = sz * 0.5f;
    float bl_x = wx - fwd_x * sz * 0.3f + perp_x * base_sz;
    float bl_y = wy - fwd_y * sz * 0.3f + perp_y * base_sz;
    float br_x = wx - fwd_x * sz * 0.3f - perp_x * base_sz;
    float br_y = wy - fwd_y * sz * 0.3f - perp_y * base_sz;

    /* Vertex format matches world shader: (x, y, u, v) * 6 verts (2 tris) */
    float verts[24] = {
        tip_x, tip_y, 0.5f, 0.5f,
        bl_x,  bl_y,  0.5f, 0.5f,
        br_x,  br_y,  0.5f, 0.5f,
        tip_x, tip_y, 0.5f, 0.5f,
        br_x,  br_y,  0.5f, 0.5f,
        bl_x,  bl_y,  0.5f, 0.5f,
    };

    /* Set up world shader with stim2 matrices, same as world_render */
    float model[16], proj[16];
    stimGetMatrix(STIM_MODELVIEW_MATRIX, model);
    stimGetMatrix(STIM_PROJECTION_MATRIX, proj);
    model[12] -= w->camera.x;
    model[13] -= w->camera.y;

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(w->shader_program);
    glUniformMatrix4fv(w->u_modelview, 1, GL_FALSE, model);
    glUniformMatrix4fv(w->u_projection, 1, GL_FALSE, proj);

    /* Bind 1x1 white texture — marker renders as solid white */
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m->marker_tex);
    glUniform1i(w->u_texture, 0);

    glBindVertexArray(w->sprite_vao);
    glBindBuffer(GL_ARRAY_BUFFER, w->sprite_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    /* ---- Draw 2D item icons on the map ---- */
    for (int i = 0; i < MAZE3D_MAX_ITEMS; i++) {
        MazeItem *it = &m->items[i];
        if (!it->active || !it->visible) continue;
        if (it->sprite_sheet_id < 0 || it->sprite_sheet_id >= w->sprite_sheet_count) continue;

        SpriteSheet *ss = &w->sprite_sheets[it->sprite_sheet_id];
        if (it->current_frame < 0 || it->current_frame >= ss->frame_count) continue;

        /* Item position in world coords */
        float ix = maze_x_to_b2x(w, m, it->x);
        float iy = maze_z_to_b2y(w, m, it->z);
        float hs = m->cell_size * 0.25f; /* small icon, quarter-cell */

        SpriteFrame *sf = &ss->frames[it->current_frame];

        float iv[24] = {
            ix - hs, iy - hs, sf->u0, sf->v1,
            ix + hs, iy - hs, sf->u1, sf->v1,
            ix + hs, iy + hs, sf->u1, sf->v0,
            ix - hs, iy - hs, sf->u0, sf->v1,
            ix + hs, iy + hs, sf->u1, sf->v0,
            ix - hs, iy + hs, sf->u0, sf->v0,
        };

        /* Bind item's atlas texture */
        if (ss->atlas_id >= 0 && ss->atlas_id < w->atlas_count) {
            glBindTexture(GL_TEXTURE_2D, w->atlases[ss->atlas_id].texture);
        }

        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(iv), iv);
        glDrawArrays(GL_TRIANGLES, 0, 6);
    }

    glBindVertexArray(0);
    glUseProgram(0);
}

/*========================================================================
 * Create / Destroy
 *========================================================================*/

Maze3D* maze3d_create(void)
{
    Maze3D *m = calloc(1, sizeof(Maze3D));
    m->wall_height    = 1.0f;
    m->eye_height     = 0.5f;
    m->cam_y          = 0.5f;
    m->move_speed     = 3.0f;
    m->turn_speed     = 2.0f;
    m->fov_degrees    = 60.0f;
    m->cam_radius     = 0.15f;
    m->cam_damping    = 10.0f;
    m->use_physics    = 1;
    m->draw_floor     = 1;
    m->draw_ceiling   = 1;
    m->fog_start      = 3.0f;
    m->fog_end        = 12.0f;
    m->fog_color[0]   = 0.1f;
    m->fog_color[1]   = 0.1f;
    m->fog_color[2]   = 0.15f;
    m->fog_color[3]   = 1.0f;
    m->ambient_light  = 0.3f;
    m->wall_atlas_id  = 0;
    m->floor_atlas_id = 0;
    m->ceiling_atlas_id = 0;
    m->wall_u0  = 0; m->wall_v0  = 0; m->wall_u1  = 1; m->wall_v1  = 1;
    m->floor_u0 = 0; m->floor_v0 = 0; m->floor_u1 = 1; m->floor_v1 = 1;
    m->ceil_u0  = 0; m->ceil_v0  = 0; m->ceil_u1  = 1; m->ceil_v1  = 1;
    m->dirty    = 1;
    return m;
}

void maze3d_destroy(Maze3D *m)
{
    if (!m) return;
    maze3d_destroy_cam_body(m);
    if (m->vao) glDeleteVertexArrays(1, &m->vao);
    if (m->vbo) glDeleteBuffers(1, &m->vbo);
    if (m->shader) glDeleteProgram(m->shader);
    if (m->marker_tex) glDeleteTextures(1, &m->marker_tex);
    if (m->item_vao) glDeleteVertexArrays(1, &m->item_vao);
    if (m->item_vbo) glDeleteBuffers(1, &m->item_vbo);
    if (m->grid) free(m->grid);
    free(m);
}

int maze3d_is_enabled(Maze3D *m) { return m && m->enabled; }

/*========================================================================
 * Helpers
 *========================================================================*/

static void maze3d_find_spawn(Maze3D *m, float *out_x, float *out_z)
{
    for (int gy = 0; gy < m->grid_h; gy++) {
        for (int gx = 0; gx < m->grid_w; gx++) {
            if (!m->grid[gy * m->grid_w + gx]) {
                *out_x = (gx + 0.5f) * m->cell_size;
                *out_z = (gy + 0.5f) * m->cell_size;
                return;
            }
        }
    }
    *out_x = m->cell_size * 0.5f;
    *out_z = m->cell_size * 0.5f;
}

static void maze3d_set_cam_position(World *w, Maze3D *m, float x, float z)
{
    m->cam_x = x;
    m->cam_z = z;
    m->cam_y = m->eye_height;
    if (m->has_cam_body && b2Body_IsValid(m->cam_body)) {
        b2Body_SetTransform(m->cam_body,
                            (b2Vec2){ maze_x_to_b2x(w, m, x),
                                      maze_z_to_b2y(w, m, z) },
                            b2Body_GetRotation(m->cam_body));
        b2Body_SetLinearVelocity(m->cam_body, (b2Vec2){0, 0});
    }
}

/*========================================================================
 * Tcl Commands
 *========================================================================*/

/*
 * worldMaze3DEnable <world> <0|1>
 */
static int worldMaze3DEnableCmd(ClientData cd, Tcl_Interp *interp,
                                int argc, char *argv[])
{
    OBJ_LIST *olist = (OBJ_LIST *)cd;
    if (argc < 3) {
        Tcl_AppendResult(interp, "usage: ", argv[0], " world 0|1", NULL);
        return TCL_ERROR;
    }

    int id;
    if ((id = resolveObjId(interp, (ObjNameInfo *)OL_NAMEINFO(olist),
                           argv[1], WorldID, "world")) < 0)
        return TCL_ERROR;

    World *w = (World *)GR_CLIENTDATA(OL_OBJ(olist, id));
    int enable;
    if (Tcl_GetInt(interp, argv[2], &enable) != TCL_OK) return TCL_ERROR;

    /* Lazy init */
    if (!w->maze3d) {
        w->maze3d = maze3d_create();
        if (maze3d_init_gl(w->maze3d) != 0) {
            Tcl_SetResult(interp, "maze3d: GL init failed", TCL_STATIC);
            return TCL_ERROR;
        }
    }

    Maze3D *m = w->maze3d;

    if (enable && !m->grid) {
        /* First enable: extract grid, build geometry, create body */
        maze3d_extract_grid(w, m);
        maze3d_rebuild(m);

        float sx, sz;
        maze3d_find_spawn(m, &sx, &sz);
        m->cam_x = sx; m->cam_z = sz; m->cam_y = m->eye_height;

        if (m->use_physics)
            maze3d_create_cam_body(w, m);
    }

    if (enable && !m->has_cam_body && m->use_physics)
        maze3d_create_cam_body(w, m);

    /* Don't destroy cam body on disable - movement continues in 2D view */

    m->enabled = enable;
    return TCL_OK;
}

/*
 * worldMaze3DCamera <world> <x> <z> ?yaw? ?pitch?
 */
static int worldMaze3DCameraCmd(ClientData cd, Tcl_Interp *interp,
                                int argc, char *argv[])
{
    OBJ_LIST *olist = (OBJ_LIST *)cd;
    if (argc < 4) {
        Tcl_AppendResult(interp, "usage: ", argv[0],
                         " world x z ?yaw? ?pitch?", NULL);
        return TCL_ERROR;
    }

    int id;
    if ((id = resolveObjId(interp, (ObjNameInfo *)OL_NAMEINFO(olist),
                           argv[1], WorldID, "world")) < 0)
        return TCL_ERROR;

    World *w = (World *)GR_CLIENTDATA(OL_OBJ(olist, id));
    Maze3D *m = w->maze3d;
    if (!m) { Tcl_SetResult(interp, "maze3d not init", TCL_STATIC); return TCL_ERROR; }

    double x, z;
    if (Tcl_GetDouble(interp, argv[2], &x) != TCL_OK) return TCL_ERROR;
    if (Tcl_GetDouble(interp, argv[3], &z) != TCL_OK) return TCL_ERROR;
    maze3d_set_cam_position(w, m, (float)x, (float)z);

    if (argc > 4) { double v; Tcl_GetDouble(interp, argv[4], &v); m->cam_yaw = (float)v; }
    if (argc > 5) { double v; Tcl_GetDouble(interp, argv[5], &v); m->cam_pitch = (float)v; }
    return TCL_OK;
}

/*
 * worldMaze3DMove <world> <forward> <strafe>
 *   forward/strafe: -1..1 input axes
 *   Returns: {x z yaw}
 */
static int worldMaze3DMoveCmd(ClientData cd, Tcl_Interp *interp,
                              int argc, char *argv[])
{
    OBJ_LIST *olist = (OBJ_LIST *)cd;
    if (argc < 4) {
        Tcl_AppendResult(interp, "usage: ", argv[0],
                         " world forward strafe", NULL);
        return TCL_ERROR;
    }

    int id;
    if ((id = resolveObjId(interp, (ObjNameInfo *)OL_NAMEINFO(olist),
                           argv[1], WorldID, "world")) < 0)
        return TCL_ERROR;

    World *w = (World *)GR_CLIENTDATA(OL_OBJ(olist, id));
    Maze3D *m = w->maze3d;
    if (!m) return TCL_OK;

    double fwd, str;
    if (Tcl_GetDouble(interp, argv[2], &fwd) != TCL_OK) return TCL_ERROR;
    if (Tcl_GetDouble(interp, argv[3], &str) != TCL_OK) return TCL_ERROR;

    float dt = getFrameDuration() / 1000.0f;
    if (dt > 0.1f) dt = 0.016f;

    /* Always use grid collision for movement (reliable in both views) */
    maze3d_move_grid(m, (float)fwd, (float)str, dt);

    /* Keep physics body in sync if it exists (for 3D mode collision) */
    if (m->has_cam_body && b2Body_IsValid(m->cam_body)) {
        b2Vec2 pos = {
            maze_x_to_b2x(w, m, m->cam_x),
            maze_z_to_b2y(w, m, m->cam_z)
        };
        b2Body_SetTransform(m->cam_body, pos, b2Body_GetRotation(m->cam_body));
        b2Body_SetLinearVelocity(m->cam_body, (b2Vec2){0, 0});
    }

    Tcl_Obj *result = Tcl_NewListObj(0, NULL);
    Tcl_ListObjAppendElement(interp, result, Tcl_NewDoubleObj(m->cam_x));
    Tcl_ListObjAppendElement(interp, result, Tcl_NewDoubleObj(m->cam_z));
    Tcl_ListObjAppendElement(interp, result, Tcl_NewDoubleObj(m->cam_yaw));
    Tcl_SetObjResult(interp, result);
    return TCL_OK;
}

/*
 * worldMaze3DRotate <world> <dyaw> ?dpitch?
 */
static int worldMaze3DRotateCmd(ClientData cd, Tcl_Interp *interp,
                                int argc, char *argv[])
{
    OBJ_LIST *olist = (OBJ_LIST *)cd;
    if (argc < 3) {
        Tcl_AppendResult(interp, "usage: ", argv[0],
                         " world dyaw ?dpitch?", NULL);
        return TCL_ERROR;
    }

    int id;
    if ((id = resolveObjId(interp, (ObjNameInfo *)OL_NAMEINFO(olist),
                           argv[1], WorldID, "world")) < 0)
        return TCL_ERROR;

    World *w = (World *)GR_CLIENTDATA(OL_OBJ(olist, id));
    Maze3D *m = w->maze3d;
    if (!m) return TCL_OK;

    double dyaw, dpitch = 0;
    if (Tcl_GetDouble(interp, argv[2], &dyaw) != TCL_OK) return TCL_ERROR;
    if (argc > 3) Tcl_GetDouble(interp, argv[3], &dpitch);
    maze3d_rotate(m, (float)dyaw, (float)dpitch);
    return TCL_OK;
}

/*
 * worldMaze3DConfigure <world> ?-option value ...?
 */
static int worldMaze3DConfigureCmd(ClientData cd, Tcl_Interp *interp,
                                   int argc, char *argv[])
{
    OBJ_LIST *olist = (OBJ_LIST *)cd;
    if (argc < 2) {
        Tcl_AppendResult(interp, "usage: ", argv[0],
                         " world ?-opt val ...?", NULL);
        return TCL_ERROR;
    }

    int id;
    if ((id = resolveObjId(interp, (ObjNameInfo *)OL_NAMEINFO(olist),
                           argv[1], WorldID, "world")) < 0)
        return TCL_ERROR;

    World *w = (World *)GR_CLIENTDATA(OL_OBJ(olist, id));
    if (!w->maze3d) {
        w->maze3d = maze3d_create();
        maze3d_init_gl(w->maze3d);
    }
    Maze3D *m = w->maze3d;

    for (int i = 2; i < argc - 1; i += 2) {
        double val;
        const char *opt = argv[i];

        if (strcmp(opt, "-wall_height") == 0) {
            Tcl_GetDouble(interp, argv[i+1], &val);
            m->wall_height = (float)val; m->dirty = 1;
        } else if (strcmp(opt, "-eye_height") == 0) {
            Tcl_GetDouble(interp, argv[i+1], &val);
            m->eye_height = (float)val; m->cam_y = m->eye_height;
        } else if (strcmp(opt, "-move_speed") == 0) {
            Tcl_GetDouble(interp, argv[i+1], &val);
            m->move_speed = (float)val;
        } else if (strcmp(opt, "-turn_speed") == 0) {
            Tcl_GetDouble(interp, argv[i+1], &val);
            m->turn_speed = (float)val;
        } else if (strcmp(opt, "-fov") == 0) {
            Tcl_GetDouble(interp, argv[i+1], &val);
            m->fov_degrees = (float)val;
        } else if (strcmp(opt, "-fog_start") == 0) {
            Tcl_GetDouble(interp, argv[i+1], &val);
            m->fog_start = (float)val;
        } else if (strcmp(opt, "-fog_end") == 0) {
            Tcl_GetDouble(interp, argv[i+1], &val);
            m->fog_end = (float)val;
        } else if (strcmp(opt, "-ambient") == 0) {
            Tcl_GetDouble(interp, argv[i+1], &val);
            m->ambient_light = (float)val;
        } else if (strcmp(opt, "-draw_floor") == 0) {
            int v; Tcl_GetInt(interp, argv[i+1], &v);
            m->draw_floor = v; m->dirty = 1;
        } else if (strcmp(opt, "-draw_ceiling") == 0) {
            int v; Tcl_GetInt(interp, argv[i+1], &v);
            m->draw_ceiling = v; m->dirty = 1;
        } else if (strcmp(opt, "-physics") == 0) {
            int v; Tcl_GetInt(interp, argv[i+1], &v);
            m->use_physics = v;
        } else if (strcmp(opt, "-cam_radius") == 0) {
            Tcl_GetDouble(interp, argv[i+1], &val);
            m->cam_radius = (float)val;
        } else if (strcmp(opt, "-cam_damping") == 0) {
            Tcl_GetDouble(interp, argv[i+1], &val);
            m->cam_damping = (float)val;
            if (m->has_cam_body && b2Body_IsValid(m->cam_body))
                b2Body_SetLinearDamping(m->cam_body, m->cam_damping);
        } else if (strcmp(opt, "-fog_color") == 0) {
            Tcl_Size lc; const char **lv;
            if (Tcl_SplitList(interp, argv[i+1], &lc, &lv) == TCL_OK && lc >= 3) {
                double r, g, b, a = 1.0;
                Tcl_GetDouble(interp, lv[0], &r);
                Tcl_GetDouble(interp, lv[1], &g);
                Tcl_GetDouble(interp, lv[2], &b);
                if (lc > 3) Tcl_GetDouble(interp, lv[3], &a);
                m->fog_color[0]=(float)r; m->fog_color[1]=(float)g;
                m->fog_color[2]=(float)b; m->fog_color[3]=(float)a;
                Tcl_Free((char *)lv);
            }
        } else if (strcmp(opt, "-wall_gid") == 0) {
            int gid; Tcl_GetInt(interp, argv[i+1], &gid);
            Atlas *a = world_find_atlas_for_gid(w, gid);
            if (a) {
                world_get_tile_uvs(a, gid, &m->wall_u0, &m->wall_v0,
                                   &m->wall_u1, &m->wall_v1);
                m->wall_atlas_id = (int)(a - w->atlases);
                m->dirty = 1;
            }
        } else if (strcmp(opt, "-floor_gid") == 0) {
            int gid; Tcl_GetInt(interp, argv[i+1], &gid);
            Atlas *a = world_find_atlas_for_gid(w, gid);
            if (a) {
                world_get_tile_uvs(a, gid, &m->floor_u0, &m->floor_v0,
                                   &m->floor_u1, &m->floor_v1);
                m->floor_atlas_id = (int)(a - w->atlases);
                m->dirty = 1;
            }
        } else if (strcmp(opt, "-ceiling_gid") == 0) {
            int gid; Tcl_GetInt(interp, argv[i+1], &gid);
            Atlas *a = world_find_atlas_for_gid(w, gid);
            if (a) {
                world_get_tile_uvs(a, gid, &m->ceil_u0, &m->ceil_v0,
                                   &m->ceil_u1, &m->ceil_v1);
                m->ceiling_atlas_id = (int)(a - w->atlases);
                m->dirty = 1;
            }
        }
    }

    return TCL_OK;
}

/*
 * worldMaze3DInfo <world>
 */
static int worldMaze3DInfoCmd(ClientData cd, Tcl_Interp *interp,
                              int argc, char *argv[])
{
    OBJ_LIST *olist = (OBJ_LIST *)cd;
    if (argc < 2) {
        Tcl_AppendResult(interp, "usage: ", argv[0], " world", NULL);
        return TCL_ERROR;
    }

    int id;
    if ((id = resolveObjId(interp, (ObjNameInfo *)OL_NAMEINFO(olist),
                           argv[1], WorldID, "world")) < 0)
        return TCL_ERROR;

    World *w = (World *)GR_CLIENTDATA(OL_OBJ(olist, id));
    if (!w->maze3d) { Tcl_SetResult(interp, "", TCL_STATIC); return TCL_OK; }
    Maze3D *m = w->maze3d;

    Tcl_Obj *r = Tcl_NewDictObj();
    Tcl_DictObjPut(interp, r, Tcl_NewStringObj("enabled",-1), Tcl_NewIntObj(m->enabled));
    Tcl_DictObjPut(interp, r, Tcl_NewStringObj("cam_x",-1), Tcl_NewDoubleObj(m->cam_x));
    Tcl_DictObjPut(interp, r, Tcl_NewStringObj("cam_z",-1), Tcl_NewDoubleObj(m->cam_z));
    Tcl_DictObjPut(interp, r, Tcl_NewStringObj("cam_y",-1), Tcl_NewDoubleObj(m->cam_y));
    Tcl_DictObjPut(interp, r, Tcl_NewStringObj("cam_yaw",-1), Tcl_NewDoubleObj(m->cam_yaw));
    Tcl_DictObjPut(interp, r, Tcl_NewStringObj("cam_pitch",-1), Tcl_NewDoubleObj(m->cam_pitch));
    Tcl_DictObjPut(interp, r, Tcl_NewStringObj("grid_w",-1), Tcl_NewIntObj(m->grid_w));
    Tcl_DictObjPut(interp, r, Tcl_NewStringObj("grid_h",-1), Tcl_NewIntObj(m->grid_h));
    Tcl_DictObjPut(interp, r, Tcl_NewStringObj("cell_size",-1), Tcl_NewDoubleObj(m->cell_size));
    Tcl_DictObjPut(interp, r, Tcl_NewStringObj("face_count",-1), Tcl_NewIntObj(m->face_count));
    Tcl_DictObjPut(interp, r, Tcl_NewStringObj("total_verts",-1), Tcl_NewIntObj(m->total_verts));
    Tcl_DictObjPut(interp, r, Tcl_NewStringObj("use_physics",-1), Tcl_NewIntObj(m->use_physics));

    /* Item counts */
    int items_active = 0, items_visible = 0;
    for (int i = 0; i < MAZE3D_MAX_ITEMS; i++) {
        if (m->items[i].active) { items_active++; if (m->items[i].visible) items_visible++; }
    }
    Tcl_DictObjPut(interp, r, Tcl_NewStringObj("items_active",-1), Tcl_NewIntObj(items_active));
    Tcl_DictObjPut(interp, r, Tcl_NewStringObj("items_visible",-1), Tcl_NewIntObj(items_visible));

    if (m->has_cam_body && b2Body_IsValid(m->cam_body)) {
        b2Vec2 vel = b2Body_GetLinearVelocity(m->cam_body);
        Tcl_DictObjPut(interp, r, Tcl_NewStringObj("vx",-1), Tcl_NewDoubleObj(vel.x));
        Tcl_DictObjPut(interp, r, Tcl_NewStringObj("vz",-1), Tcl_NewDoubleObj(vel.y));
    }

    Tcl_SetObjResult(interp, r);
    return TCL_OK;
}

/*
 * worldMaze3DPlaceAt <world> <object_name> ?yaw?
 */
static int worldMaze3DPlaceAtCmd(ClientData cd, Tcl_Interp *interp,
                                 int argc, char *argv[])
{
    OBJ_LIST *olist = (OBJ_LIST *)cd;
    if (argc < 3) {
        Tcl_AppendResult(interp, "usage: ", argv[0],
                         " world object_name ?yaw?", NULL);
        return TCL_ERROR;
    }

    int id;
    if ((id = resolveObjId(interp, (ObjNameInfo *)OL_NAMEINFO(olist),
                           argv[1], WorldID, "world")) < 0)
        return TCL_ERROR;

    World *w = (World *)GR_CLIENTDATA(OL_OBJ(olist, id));
    Maze3D *m = w->maze3d;
    if (!m) { Tcl_SetResult(interp, "maze3d not init", TCL_STATIC); return TCL_ERROR; }

    for (int i = 0; i < w->object_count; i++) {
        if (strcmp(w->objects[i].name, argv[2]) == 0) {
            /*
             * TMX objects are in world coords (after ppm conversion + Y flip).
             * For the 3D maze we need to map back to grid-space:
             *   maze X = obj.x - offset_x
             *   maze Z = (map_height * cell_size) - (obj.y - offset_y)
             */
            float ox = w->objects[i].x - w->offset_x;
            float oy = w->objects[i].y - w->offset_y;
            float mz = (w->map_height * m->cell_size) - oy;

            maze3d_set_cam_position(w, m, ox, mz);

            if (argc > 3) {
                double yaw;
                Tcl_GetDouble(interp, argv[3], &yaw);
                m->cam_yaw = (float)yaw;
            }

            Tcl_Obj *r = Tcl_NewListObj(0, NULL);
            Tcl_ListObjAppendElement(interp, r, Tcl_NewDoubleObj(m->cam_x));
            Tcl_ListObjAppendElement(interp, r, Tcl_NewDoubleObj(m->cam_z));
            Tcl_SetObjResult(interp, r);
            return TCL_OK;
        }
    }

    Tcl_AppendResult(interp, "object not found: ", argv[2], NULL);
    return TCL_ERROR;
}

/*
 * worldMaze3DRebuild <world>
 */
static int worldMaze3DRebuildCmd(ClientData cd, Tcl_Interp *interp,
                                 int argc, char *argv[])
{
    OBJ_LIST *olist = (OBJ_LIST *)cd;
    if (argc < 2) {
        Tcl_AppendResult(interp, "usage: ", argv[0], " world", NULL);
        return TCL_ERROR;
    }

    int id;
    if ((id = resolveObjId(interp, (ObjNameInfo *)OL_NAMEINFO(olist),
                           argv[1], WorldID, "world")) < 0)
        return TCL_ERROR;

    World *w = (World *)GR_CLIENTDATA(OL_OBJ(olist, id));
    Maze3D *m = w->maze3d;
    if (!m) return TCL_OK;

    maze3d_extract_grid(w, m);
    maze3d_rebuild(m);

    Tcl_Obj *r = Tcl_NewDictObj();
    Tcl_DictObjPut(interp, r, Tcl_NewStringObj("faces",-1), Tcl_NewIntObj(m->face_count));
    Tcl_DictObjPut(interp, r, Tcl_NewStringObj("verts",-1), Tcl_NewIntObj(m->total_verts));
    Tcl_SetObjResult(interp, r);
    return TCL_OK;
}

/*
 * worldMaze3DQueryCell <world> <gx> <gy>
 */
static int worldMaze3DQueryCellCmd(ClientData cd, Tcl_Interp *interp,
                                   int argc, char *argv[])
{
    OBJ_LIST *olist = (OBJ_LIST *)cd;
    if (argc < 4) {
        Tcl_AppendResult(interp, "usage: ", argv[0], " world gx gy", NULL);
        return TCL_ERROR;
    }

    int id;
    if ((id = resolveObjId(interp, (ObjNameInfo *)OL_NAMEINFO(olist),
                           argv[1], WorldID, "world")) < 0)
        return TCL_ERROR;

    World *w = (World *)GR_CLIENTDATA(OL_OBJ(olist, id));
    Maze3D *m = w->maze3d;
    if (!m || !m->grid) { Tcl_SetObjResult(interp, Tcl_NewIntObj(-1)); return TCL_OK; }

    int gx, gy;
    if (Tcl_GetInt(interp, argv[2], &gx) != TCL_OK) return TCL_ERROR;
    if (Tcl_GetInt(interp, argv[3], &gy) != TCL_OK) return TCL_ERROR;

    Tcl_SetObjResult(interp, Tcl_NewIntObj(cell_is_wall(m, gx, gy)));
    return TCL_OK;
}

/*========================================================================
 * Item Tcl Commands
 *========================================================================*/

/*
 * worldMaze3DItemAdd <world> <sheetName> <x> <z> ?-name n? ?-frame f? ?-size s? ?-radius r?
 * Returns: item ID
 */
static int worldMaze3DItemAddCmd(ClientData cd, Tcl_Interp *interp,
                                  int argc, char *argv[])
{
    OBJ_LIST *olist = (OBJ_LIST *)cd;
    if (argc < 5) {
        Tcl_AppendResult(interp, "usage: ", argv[0],
                         " world sheetName x z ?-name n? ?-frame f? ?-size s? ?-radius r?", NULL);
        return TCL_ERROR;
    }

    int id;
    if ((id = resolveObjId(interp, (ObjNameInfo *)OL_NAMEINFO(olist),
                           argv[1], WorldID, "world")) < 0)
        return TCL_ERROR;

    World *w = (World *)GR_CLIENTDATA(OL_OBJ(olist, id));
    Maze3D *m = w->maze3d;
    if (!m) { Tcl_SetResult(interp, "no maze3d", TCL_STATIC); return TCL_ERROR; }

    /* Find sprite sheet */
    int sheet_id = -1;
    for (int i = 0; i < w->sprite_sheet_count; i++) {
        if (strcmp(w->sprite_sheets[i].name, argv[2]) == 0) { sheet_id = i; break; }
    }
    if (sheet_id < 0) {
        Tcl_AppendResult(interp, "sprite sheet not found: ", argv[2], NULL);
        return TCL_ERROR;
    }

    double x, z;
    if (Tcl_GetDouble(interp, argv[3], &x) != TCL_OK) return TCL_ERROR;
    if (Tcl_GetDouble(interp, argv[4], &z) != TCL_OK) return TCL_ERROR;

    /* Find free slot */
    int slot = -1;
    for (int i = 0; i < MAZE3D_MAX_ITEMS; i++) {
        if (!m->items[i].active) { slot = i; break; }
    }
    if (slot < 0) { Tcl_SetResult(interp, "max items reached", TCL_STATIC); return TCL_ERROR; }

    MazeItem *it = &m->items[slot];
    memset(it, 0, sizeof(MazeItem));
    it->active = 1;
    it->visible = 1;
    it->sprite_sheet_id = sheet_id;
    it->current_frame = 0;
    it->x = (float)x;
    it->z = (float)z;
    it->y_offset = 0.3f;         /* default hover above floor */
    it->size = 0.5f;             /* default billboard size */
    it->pickup_radius = 0.5f;    /* default pickup distance */
    it->bob_phase = (float)(slot * 1.7f); /* different phase per slot */
    it->bob_amplitude = 0.05f;   /* default bob height */
    it->bob_speed = 1.0f;        /* default bob frequency (Hz) */
    it->anim_loop = 1;
    strncpy(it->name, argv[2], sizeof(it->name) - 1);

    /* Parse optional flags */
    for (int i = 5; i < argc - 1; i += 2) {
        if (strcmp(argv[i], "-name") == 0) {
            strncpy(it->name, argv[i+1], sizeof(it->name) - 1);
        } else if (strcmp(argv[i], "-frame") == 0) {
            int f; if (Tcl_GetInt(interp, argv[i+1], &f) == TCL_OK) it->current_frame = f;
        } else if (strcmp(argv[i], "-size") == 0) {
            double s; if (Tcl_GetDouble(interp, argv[i+1], &s) == TCL_OK) it->size = (float)s;
        } else if (strcmp(argv[i], "-radius") == 0) {
            double r; if (Tcl_GetDouble(interp, argv[i+1], &r) == TCL_OK) it->pickup_radius = (float)r;
        } else if (strcmp(argv[i], "-height") == 0) {
            double h; if (Tcl_GetDouble(interp, argv[i+1], &h) == TCL_OK) it->y_offset = (float)h;
        } else if (strcmp(argv[i], "-spin") == 0) {
            double sp; if (Tcl_GetDouble(interp, argv[i+1], &sp) == TCL_OK) it->spin_speed = (float)sp;
        } else if (strcmp(argv[i], "-bob_amplitude") == 0) {
            double a; if (Tcl_GetDouble(interp, argv[i+1], &a) == TCL_OK) it->bob_amplitude = (float)a;
        } else if (strcmp(argv[i], "-bob_speed") == 0) {
            double s; if (Tcl_GetDouble(interp, argv[i+1], &s) == TCL_OK) it->bob_speed = (float)s;
        }
    }

    if (slot >= m->item_count) m->item_count = slot + 1;

    Tcl_SetObjResult(interp, Tcl_NewIntObj(slot));
    return TCL_OK;
}

/*
 * worldMaze3DItemShow <world> <itemId> <0|1>
 */
static int worldMaze3DItemShowCmd(ClientData cd, Tcl_Interp *interp,
                                   int argc, char *argv[])
{
    OBJ_LIST *olist = (OBJ_LIST *)cd;
    if (argc < 4) {
        Tcl_AppendResult(interp, "usage: ", argv[0], " world itemId visible", NULL);
        return TCL_ERROR;
    }

    int id;
    if ((id = resolveObjId(interp, (ObjNameInfo *)OL_NAMEINFO(olist),
                           argv[1], WorldID, "world")) < 0)
        return TCL_ERROR;

    World *w = (World *)GR_CLIENTDATA(OL_OBJ(olist, id));
    Maze3D *m = w->maze3d;
    if (!m) return TCL_OK;

    int item_id, vis;
    if (Tcl_GetInt(interp, argv[2], &item_id) != TCL_OK) return TCL_ERROR;
    if (Tcl_GetInt(interp, argv[3], &vis) != TCL_OK) return TCL_ERROR;
    if (item_id < 0 || item_id >= MAZE3D_MAX_ITEMS || !m->items[item_id].active)
        return TCL_OK;

    m->items[item_id].visible = vis;
    return TCL_OK;
}

/*
 * worldMaze3DItemRemove <world> <itemId>
 */
static int worldMaze3DItemRemoveCmd(ClientData cd, Tcl_Interp *interp,
                                     int argc, char *argv[])
{
    OBJ_LIST *olist = (OBJ_LIST *)cd;
    if (argc < 3) {
        Tcl_AppendResult(interp, "usage: ", argv[0], " world itemId", NULL);
        return TCL_ERROR;
    }

    int id;
    if ((id = resolveObjId(interp, (ObjNameInfo *)OL_NAMEINFO(olist),
                           argv[1], WorldID, "world")) < 0)
        return TCL_ERROR;

    World *w = (World *)GR_CLIENTDATA(OL_OBJ(olist, id));
    Maze3D *m = w->maze3d;
    if (!m) return TCL_OK;

    int item_id;
    if (Tcl_GetInt(interp, argv[2], &item_id) != TCL_OK) return TCL_ERROR;
    if (item_id < 0 || item_id >= MAZE3D_MAX_ITEMS) return TCL_OK;

    m->items[item_id].active = 0;
    m->items[item_id].visible = 0;
    return TCL_OK;
}

/*
 * worldMaze3DItemPosition <world> <itemId> ?x z?
 * Get or set item position.
 */
static int worldMaze3DItemPositionCmd(ClientData cd, Tcl_Interp *interp,
                                       int argc, char *argv[])
{
    OBJ_LIST *olist = (OBJ_LIST *)cd;
    if (argc < 3) {
        Tcl_AppendResult(interp, "usage: ", argv[0], " world itemId ?x z?", NULL);
        return TCL_ERROR;
    }

    int id;
    if ((id = resolveObjId(interp, (ObjNameInfo *)OL_NAMEINFO(olist),
                           argv[1], WorldID, "world")) < 0)
        return TCL_ERROR;

    World *w = (World *)GR_CLIENTDATA(OL_OBJ(olist, id));
    Maze3D *m = w->maze3d;
    if (!m) return TCL_OK;

    int item_id;
    if (Tcl_GetInt(interp, argv[2], &item_id) != TCL_OK) return TCL_ERROR;
    if (item_id < 0 || item_id >= MAZE3D_MAX_ITEMS || !m->items[item_id].active) {
        Tcl_SetResult(interp, "invalid item", TCL_STATIC);
        return TCL_ERROR;
    }

    MazeItem *it = &m->items[item_id];

    if (argc >= 5) {
        double x, z;
        if (Tcl_GetDouble(interp, argv[3], &x) != TCL_OK) return TCL_ERROR;
        if (Tcl_GetDouble(interp, argv[4], &z) != TCL_OK) return TCL_ERROR;
        it->x = (float)x;
        it->z = (float)z;
    }

    Tcl_Obj *result = Tcl_NewListObj(0, NULL);
    Tcl_ListObjAppendElement(interp, result, Tcl_NewDoubleObj(it->x));
    Tcl_ListObjAppendElement(interp, result, Tcl_NewDoubleObj(it->z));
    Tcl_SetObjResult(interp, result);
    return TCL_OK;
}

/*
 * worldMaze3DItemConfigure <world> <itemId> ?-flag value ...?
 * Modify properties of an existing item.
 * Supports: -size, -radius, -height, -frame, -name,
 *           -bob_amplitude, -bob_speed, -spin, -visible
 */
static int worldMaze3DItemConfigureCmd(ClientData cd, Tcl_Interp *interp,
                                        int argc, char *argv[])
{
    OBJ_LIST *olist = (OBJ_LIST *)cd;
    if (argc < 3) {
        Tcl_AppendResult(interp, "usage: ", argv[0],
                         " world itemId ?-flag value ...?", NULL);
        return TCL_ERROR;
    }

    int id;
    if ((id = resolveObjId(interp, (ObjNameInfo *)OL_NAMEINFO(olist),
                           argv[1], WorldID, "world")) < 0)
        return TCL_ERROR;

    World *w = (World *)GR_CLIENTDATA(OL_OBJ(olist, id));
    Maze3D *m = w->maze3d;
    if (!m) { Tcl_SetResult(interp, "no maze3d", TCL_STATIC); return TCL_ERROR; }

    int item_id;
    if (Tcl_GetInt(interp, argv[2], &item_id) != TCL_OK) return TCL_ERROR;
    if (item_id < 0 || item_id >= MAZE3D_MAX_ITEMS || !m->items[item_id].active) {
        Tcl_SetResult(interp, "invalid item", TCL_STATIC);
        return TCL_ERROR;
    }

    MazeItem *it = &m->items[item_id];

    for (int i = 3; i < argc - 1; i += 2) {
        if (strcmp(argv[i], "-size") == 0) {
            double v; if (Tcl_GetDouble(interp, argv[i+1], &v) == TCL_OK) it->size = (float)v;
        } else if (strcmp(argv[i], "-radius") == 0) {
            double v; if (Tcl_GetDouble(interp, argv[i+1], &v) == TCL_OK) it->pickup_radius = (float)v;
        } else if (strcmp(argv[i], "-height") == 0) {
            double v; if (Tcl_GetDouble(interp, argv[i+1], &v) == TCL_OK) it->y_offset = (float)v;
        } else if (strcmp(argv[i], "-frame") == 0) {
            int f; if (Tcl_GetInt(interp, argv[i+1], &f) == TCL_OK) it->current_frame = f;
        } else if (strcmp(argv[i], "-name") == 0) {
            strncpy(it->name, argv[i+1], sizeof(it->name) - 1);
        } else if (strcmp(argv[i], "-bob_amplitude") == 0) {
            double v; if (Tcl_GetDouble(interp, argv[i+1], &v) == TCL_OK) it->bob_amplitude = (float)v;
        } else if (strcmp(argv[i], "-bob_speed") == 0) {
            double v; if (Tcl_GetDouble(interp, argv[i+1], &v) == TCL_OK) it->bob_speed = (float)v;
        } else if (strcmp(argv[i], "-spin") == 0) {
            double v; if (Tcl_GetDouble(interp, argv[i+1], &v) == TCL_OK) it->spin_speed = (float)v;
        } else if (strcmp(argv[i], "-visible") == 0) {
            int v; if (Tcl_GetInt(interp, argv[i+1], &v) == TCL_OK) it->visible = v;
        }
    }

    return TCL_OK;
}

/*
 * worldMaze3DItemAnimate <world> <itemId> <animName> ?fps? ?loop?
 * Set animation from sprite sheet's Aseprite animation data.
 */
static int worldMaze3DItemAnimateCmd(ClientData cd, Tcl_Interp *interp,
                                      int argc, char *argv[])
{
    OBJ_LIST *olist = (OBJ_LIST *)cd;
    if (argc < 4) {
        Tcl_AppendResult(interp, "usage: ", argv[0], " world itemId animName ?fps? ?loop?", NULL);
        return TCL_ERROR;
    }

    int id;
    if ((id = resolveObjId(interp, (ObjNameInfo *)OL_NAMEINFO(olist),
                           argv[1], WorldID, "world")) < 0)
        return TCL_ERROR;

    World *w = (World *)GR_CLIENTDATA(OL_OBJ(olist, id));
    Maze3D *m = w->maze3d;
    if (!m) return TCL_OK;

    int item_id;
    if (Tcl_GetInt(interp, argv[2], &item_id) != TCL_OK) return TCL_ERROR;
    if (item_id < 0 || item_id >= MAZE3D_MAX_ITEMS || !m->items[item_id].active) {
        Tcl_SetResult(interp, "invalid item", TCL_STATIC);
        return TCL_ERROR;
    }

    MazeItem *it = &m->items[item_id];
    SpriteSheet *ss = &w->sprite_sheets[it->sprite_sheet_id];

    if (!ss->has_aseprite) {
        Tcl_SetResult(interp, "sprite sheet has no animation data", TCL_STATIC);
        return TCL_ERROR;
    }

    AsepriteAnimation *anim = aseprite_find_animation(&ss->aseprite, argv[3]);
    if (!anim) {
        Tcl_AppendResult(interp, "animation not found: ", argv[3], NULL);
        return TCL_ERROR;
    }

    it->anim_frame_count = anim->frame_count > 32 ? 32 : anim->frame_count;
    for (int i = 0; i < it->anim_frame_count; i++) {
        it->anim_frames[i] = anim->frames[i];
    }

    it->anim_fps = anim->default_fps;
    if (argc > 4) { double f; if (Tcl_GetDouble(interp, argv[4], &f) == TCL_OK) it->anim_fps = (float)f; }
    it->anim_loop = 1;
    if (argc > 5) { int l; if (Tcl_GetInt(interp, argv[5], &l) == TCL_OK) it->anim_loop = l; }

    it->anim_current_frame = 0;
    it->anim_time = 0;
    it->anim_playing = 1;
    it->current_frame = it->anim_frames[0];

    return TCL_OK;
}

/*
 * worldMaze3DItemCallback <world> <procName>
 * Set Tcl callback for item pickup: procName world_id item_id item_name
 */
static int worldMaze3DItemCallbackCmd(ClientData cd, Tcl_Interp *interp,
                                       int argc, char *argv[])
{
    OBJ_LIST *olist = (OBJ_LIST *)cd;
    if (argc < 3) {
        Tcl_AppendResult(interp, "usage: ", argv[0], " world procName", NULL);
        return TCL_ERROR;
    }

    int id;
    if ((id = resolveObjId(interp, (ObjNameInfo *)OL_NAMEINFO(olist),
                           argv[1], WorldID, "world")) < 0)
        return TCL_ERROR;

    World *w = (World *)GR_CLIENTDATA(OL_OBJ(olist, id));
    Maze3D *m = w->maze3d;
    if (!m) return TCL_OK;

    strncpy(m->item_callback, argv[2], sizeof(m->item_callback) - 1);
    return TCL_OK;
}

/*
 * worldMaze3DItemList <world>
 * Returns list of dicts: {id name x z visible active}
 */
static int worldMaze3DItemListCmd(ClientData cd, Tcl_Interp *interp,
                                   int argc, char *argv[])
{
    OBJ_LIST *olist = (OBJ_LIST *)cd;
    if (argc < 2) {
        Tcl_AppendResult(interp, "usage: ", argv[0], " world", NULL);
        return TCL_ERROR;
    }

    int id;
    if ((id = resolveObjId(interp, (ObjNameInfo *)OL_NAMEINFO(olist),
                           argv[1], WorldID, "world")) < 0)
        return TCL_ERROR;

    World *w = (World *)GR_CLIENTDATA(OL_OBJ(olist, id));
    Maze3D *m = w->maze3d;
    if (!m) { Tcl_SetObjResult(interp, Tcl_NewListObj(0, NULL)); return TCL_OK; }

    Tcl_Obj *list = Tcl_NewListObj(0, NULL);
    for (int i = 0; i < MAZE3D_MAX_ITEMS; i++) {
        MazeItem *it = &m->items[i];
        if (!it->active) continue;

        Tcl_Obj *dict = Tcl_NewDictObj();
        Tcl_DictObjPut(interp, dict, Tcl_NewStringObj("id",-1), Tcl_NewIntObj(i));
        Tcl_DictObjPut(interp, dict, Tcl_NewStringObj("name",-1), Tcl_NewStringObj(it->name,-1));
        Tcl_DictObjPut(interp, dict, Tcl_NewStringObj("x",-1), Tcl_NewDoubleObj(it->x));
        Tcl_DictObjPut(interp, dict, Tcl_NewStringObj("z",-1), Tcl_NewDoubleObj(it->z));
        Tcl_DictObjPut(interp, dict, Tcl_NewStringObj("visible",-1), Tcl_NewIntObj(it->visible));
        Tcl_ListObjAppendElement(interp, list, dict);
    }

    Tcl_SetObjResult(interp, list);
    return TCL_OK;
}

/*
 * worldMaze3DItemInfo <world> <itemId>
 * Returns dict with full item state.
 */
static int worldMaze3DItemInfoCmd(ClientData cd, Tcl_Interp *interp,
                                   int argc, char *argv[])
{
    OBJ_LIST *olist = (OBJ_LIST *)cd;
    if (argc < 3) {
        Tcl_AppendResult(interp, "usage: ", argv[0], " world itemId", NULL);
        return TCL_ERROR;
    }

    int id;
    if ((id = resolveObjId(interp, (ObjNameInfo *)OL_NAMEINFO(olist),
                           argv[1], WorldID, "world")) < 0)
        return TCL_ERROR;

    World *w = (World *)GR_CLIENTDATA(OL_OBJ(olist, id));
    Maze3D *m = w->maze3d;
    if (!m) { Tcl_SetResult(interp, "no maze3d", TCL_STATIC); return TCL_ERROR; }

    int item_id;
    if (Tcl_GetInt(interp, argv[2], &item_id) != TCL_OK) return TCL_ERROR;
    if (item_id < 0 || item_id >= MAZE3D_MAX_ITEMS || !m->items[item_id].active) {
        Tcl_SetResult(interp, "invalid item", TCL_STATIC);
        return TCL_ERROR;
    }

    MazeItem *it = &m->items[item_id];
    Tcl_Obj *r = Tcl_NewDictObj();
    Tcl_DictObjPut(interp, r, Tcl_NewStringObj("id",-1), Tcl_NewIntObj(item_id));
    Tcl_DictObjPut(interp, r, Tcl_NewStringObj("name",-1), Tcl_NewStringObj(it->name,-1));
    Tcl_DictObjPut(interp, r, Tcl_NewStringObj("x",-1), Tcl_NewDoubleObj(it->x));
    Tcl_DictObjPut(interp, r, Tcl_NewStringObj("z",-1), Tcl_NewDoubleObj(it->z));
    Tcl_DictObjPut(interp, r, Tcl_NewStringObj("y_offset",-1), Tcl_NewDoubleObj(it->y_offset));
    Tcl_DictObjPut(interp, r, Tcl_NewStringObj("size",-1), Tcl_NewDoubleObj(it->size));
    Tcl_DictObjPut(interp, r, Tcl_NewStringObj("visible",-1), Tcl_NewIntObj(it->visible));
    Tcl_DictObjPut(interp, r, Tcl_NewStringObj("frame",-1), Tcl_NewIntObj(it->current_frame));
    Tcl_DictObjPut(interp, r, Tcl_NewStringObj("sprite_sheet_id",-1), Tcl_NewIntObj(it->sprite_sheet_id));
    Tcl_DictObjPut(interp, r, Tcl_NewStringObj("pickup_radius",-1), Tcl_NewDoubleObj(it->pickup_radius));
    Tcl_DictObjPut(interp, r, Tcl_NewStringObj("bob_amplitude",-1), Tcl_NewDoubleObj(it->bob_amplitude));
    Tcl_DictObjPut(interp, r, Tcl_NewStringObj("bob_speed",-1), Tcl_NewDoubleObj(it->bob_speed));
    Tcl_DictObjPut(interp, r, Tcl_NewStringObj("spin_speed",-1), Tcl_NewDoubleObj(it->spin_speed));
    Tcl_DictObjPut(interp, r, Tcl_NewStringObj("anim_playing",-1), Tcl_NewIntObj(it->anim_playing));
    Tcl_SetObjResult(interp, r);
    return TCL_OK;
}

/*========================================================================
 * Command Registration
 *========================================================================*/

void world_maze3d_register_commands(Tcl_Interp *interp, OBJ_LIST *olist)
{
    Tcl_CreateCommand(interp, "worldMaze3DEnable",
        (Tcl_CmdProc*)worldMaze3DEnableCmd, (ClientData)olist, NULL);
    Tcl_CreateCommand(interp, "worldMaze3DCamera",
        (Tcl_CmdProc*)worldMaze3DCameraCmd, (ClientData)olist, NULL);
    Tcl_CreateCommand(interp, "worldMaze3DMove",
        (Tcl_CmdProc*)worldMaze3DMoveCmd, (ClientData)olist, NULL);
    Tcl_CreateCommand(interp, "worldMaze3DRotate",
        (Tcl_CmdProc*)worldMaze3DRotateCmd, (ClientData)olist, NULL);
    Tcl_CreateCommand(interp, "worldMaze3DConfigure",
        (Tcl_CmdProc*)worldMaze3DConfigureCmd, (ClientData)olist, NULL);
    Tcl_CreateCommand(interp, "worldMaze3DInfo",
        (Tcl_CmdProc*)worldMaze3DInfoCmd, (ClientData)olist, NULL);
    Tcl_CreateCommand(interp, "worldMaze3DPlaceAt",
        (Tcl_CmdProc*)worldMaze3DPlaceAtCmd, (ClientData)olist, NULL);
    Tcl_CreateCommand(interp, "worldMaze3DRebuild",
        (Tcl_CmdProc*)worldMaze3DRebuildCmd, (ClientData)olist, NULL);
    Tcl_CreateCommand(interp, "worldMaze3DQueryCell",
        (Tcl_CmdProc*)worldMaze3DQueryCellCmd, (ClientData)olist, NULL);
    Tcl_CreateCommand(interp, "worldMaze3DItemAdd",
        (Tcl_CmdProc*)worldMaze3DItemAddCmd, (ClientData)olist, NULL);
    Tcl_CreateCommand(interp, "worldMaze3DItemShow",
        (Tcl_CmdProc*)worldMaze3DItemShowCmd, (ClientData)olist, NULL);
    Tcl_CreateCommand(interp, "worldMaze3DItemRemove",
        (Tcl_CmdProc*)worldMaze3DItemRemoveCmd, (ClientData)olist, NULL);
    Tcl_CreateCommand(interp, "worldMaze3DItemPosition",
        (Tcl_CmdProc*)worldMaze3DItemPositionCmd, (ClientData)olist, NULL);
    Tcl_CreateCommand(interp, "worldMaze3DItemConfigure",
        (Tcl_CmdProc*)worldMaze3DItemConfigureCmd, (ClientData)olist, NULL);
    Tcl_CreateCommand(interp, "worldMaze3DItemAnimate",
        (Tcl_CmdProc*)worldMaze3DItemAnimateCmd, (ClientData)olist, NULL);
    Tcl_CreateCommand(interp, "worldMaze3DItemCallback",
        (Tcl_CmdProc*)worldMaze3DItemCallbackCmd, (ClientData)olist, NULL);
    Tcl_CreateCommand(interp, "worldMaze3DItemList",
        (Tcl_CmdProc*)worldMaze3DItemListCmd, (ClientData)olist, NULL);
    Tcl_CreateCommand(interp, "worldMaze3DItemInfo",
        (Tcl_CmdProc*)worldMaze3DItemInfoCmd, (ClientData)olist, NULL);
}
