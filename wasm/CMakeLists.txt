cmake_minimum_required(VERSION 3.15)
project(stim2_shape_wasm LANGUAGES C CXX)

if(NOT EMSCRIPTEN)
    message(FATAL_ERROR "Configure with emcmake so this links WebAssembly.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# Same pin as the svg module in stimdlls/CMakeLists.txt. plutovg comes
# along as lunasvg's rasterizer; the export does not use the SVG parser.
set(LUNASVG_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    lunasvg
    GIT_REPOSITORY https://github.com/sammycage/lunasvg.git
    GIT_TAG        v3.5.0
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(lunasvg)

# Tcl 9.1 from deps/tcl, the same commit stim2's submodule pins. Static,
# no load command, no zipfs. emconfigure is what makes the configure
# tests run under node.
set(TCL_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../deps/tcl)
set(TCL_BUILD ${CMAKE_BINARY_DIR}/tcl)
set(TCL_PREFIX ${CMAKE_BINARY_DIR}/tcl-install)
set(TCL_LIB ${TCL_PREFIX}/lib/libtcl9.1.a)
add_custom_command(
    OUTPUT ${TCL_LIB}
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/build_tcl.sh ${TCL_SRC} ${TCL_BUILD} ${TCL_PREFIX}
    DEPENDS ${TCL_SRC}/unix/configure ${CMAKE_CURRENT_SOURCE_DIR}/build_tcl.sh
    COMMENT "Building Tcl 9.1 for WebAssembly"
    VERBATIM
)
add_custom_target(tcl_wasm DEPENDS ${TCL_LIB})

# stim2's command table and the graphics modules. Glad is the GLES3
# header, so gl* calls are WebGL 2. Sockets and imgui stay out.
# dlsh's statistics, JSON, Arrow, and MessagePack commands are
# compiled only because they live in the same files; their libraries
# are stubbed and not used.

# FFmpeg built with emcc. video.c opens files with avformat_open_input,
# so the five libraries it includes on the Pi are linked here.
set(FFMPEG_PREFIX ${CMAKE_BINARY_DIR}/ffmpeg-install)
set(FFMPEG_LIBS
    ${FFMPEG_PREFIX}/lib/libavformat.a
    ${FFMPEG_PREFIX}/lib/libavcodec.a
    ${FFMPEG_PREFIX}/lib/libswscale.a
    ${FFMPEG_PREFIX}/lib/libswresample.a
    ${FFMPEG_PREFIX}/lib/libavutil.a
)
add_custom_command(
    OUTPUT ${FFMPEG_LIBS}
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/build_ffmpeg.sh ${CMAKE_BINARY_DIR}/ffmpeg-build ${FFMPEG_PREFIX}
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/build_ffmpeg.sh
    COMMENT "Building FFmpeg for WebAssembly"
    VERBATIM
)
add_custom_target(ffmpeg_wasm DEPENDS ${FFMPEG_LIBS})

# The same commits the stim2 repo pins. Box2D is the v3 C library.
# Spine-c is compiled here rather than through its cmake, because that
# file rewrites the global C and C++ flags.
set(BOX2D_VALIDATE OFF CACHE BOOL "" FORCE)
set(BOX2D_UNIT_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../deps/box2d
    ${CMAKE_CURRENT_BINARY_DIR}/box2d EXCLUDE_FROM_ALL)
set(SPINE_C_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../deps/spine-runtimes/spine-c/spine-c)
file(GLOB SPINE_C_SOURCES CONFIGURE_DEPENDS ${SPINE_C_DIR}/src/spine/*.c)
add_library(spine_runtime STATIC ${SPINE_C_SOURCES})
target_include_directories(spine_runtime PUBLIC ${SPINE_C_DIR}/include)
set_target_properties(spine_runtime PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED YES)
set(STIM2_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../src)
set(MOD_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../stimdlls/src)
set(DLSH_SRC /home/lab/systems/dlsh/src)
add_executable(shape_raster
    shape_export.cpp
    obj_host.cpp
    frame.cpp
    app_stubs.c
    ${STIM2_SRC}/animate.cpp
    ${STIM2_SRC}/timer.cpp
    ${STIM2_SRC}/swapsync.cpp
    ${STIM2_SRC}/rawapi.c
    glfw_time.c
    ${MOD_SRC}/shape_raster.cpp
    ${STIM2_SRC}/grobj.c
    ${STIM2_SRC}/objgroup.c
    ${STIM2_SRC}/objname.c
    ${STIM2_SRC}/tclproc.c
    ${STIM2_SRC}/prmutil.c
    ${MOD_SRC}/metagroup.c
    ${MOD_SRC}/polygon.c
    ${MOD_SRC}/image.c
    ${MOD_SRC}/text.c
    ${MOD_SRC}/shader.c
    ${MOD_SRC}/mesh.c
    ${MOD_SRC}/motionpatch.c
    ${MOD_SRC}/open-simplex-noise.c
    ${MOD_SRC}/svg.cpp
    ${MOD_SRC}/shape.cpp
    ${MOD_SRC}/shaderutils.c
    ${MOD_SRC}/bstrlib.c
    ${MOD_SRC}/glsw.c
    ${MOD_SRC}/shaderimage.c
    ${MOD_SRC}/targa.c
    ${MOD_SRC}/lodepng.c
    ${MOD_SRC}/box2d.c
    ${MOD_SRC}/spine.c
    ${MOD_SRC}/world.c
    ${MOD_SRC}/world_camera.c
    ${MOD_SRC}/world_atlas.c
    ${MOD_SRC}/world_render.c
    ${MOD_SRC}/world_sprite.c
    ${MOD_SRC}/world_spritesheet.c
    ${MOD_SRC}/world_tilemap.c
    ${MOD_SRC}/world_maze3d.c
    ${MOD_SRC}/video.c
    ${MOD_SRC}/tmx_xml.cpp
    ${MOD_SRC}/aseprite_json.cpp
    ${MOD_SRC}/tinyxml2.cpp
    dlsh_stubs.c
    ${DLSH_SRC}/tcl_dl.c
    ${DLSH_SRC}/dlref.c
    ${DLSH_SRC}/dlwide.c
    ${DLSH_SRC}/dlarith.c
    ${DLSH_SRC}/dfana.c
    ${DLSH_SRC}/lablib/df.c
    ${DLSH_SRC}/lablib/dfutils.c
    ${DLSH_SRC}/lablib/dynio.c
    ${DLSH_SRC}/lablib/flipfuncs.c
    ${DLSH_SRC}/lablib/randvars.c
)
# randvars.c picks its entropy source from LINUX. Emscripten does not
# define that, and the fallback reads a member timeval does not have.
set_source_files_properties(${DLSH_SRC}/lablib/randvars.c PROPERTIES
    COMPILE_DEFINITIONS "LINUX"
)
# shader.c and mesh.c are separate plugins on the Pi, so each has its
# own copy of this table. One wasm image needs two names.
set_source_files_properties(${MOD_SRC}/mesh.c PROPERTIES
    COMPILE_DEFINITIONS "shaderProgramTable=meshShaderProgramTable;find_shader_program=mesh_find_shader_program"
)
# image.c, spine.c, and world_atlas.c each embed stb_image. Keep image.c's
# copy, and make the other two private to their own file.
set_source_files_properties(${MOD_SRC}/spine.c ${MOD_SRC}/world_atlas.c PROPERTIES
    COMPILE_FLAGS "-include ${CMAKE_CURRENT_SOURCE_DIR}/stb_static.h"
)
add_dependencies(shape_raster tcl_wasm ffmpeg_wasm)
target_include_directories(shape_raster PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/glstub
    ${lunasvg_SOURCE_DIR}/include
    ../stimdlls/src
    ${STIM2_SRC}
    ${TCL_PREFIX}/include
    ${DLSH_SRC}
    ${DLSH_SRC}/lablib
    ${CMAKE_CURRENT_SOURCE_DIR}/dlsh_stubs
    ${TCL_SRC}/compat/zlib
    ${FFMPEG_PREFIX}/include
)
target_compile_definitions(shape_raster PRIVATE STATIC_BUILD STIM2_USE_GLES=1)
target_link_libraries(shape_raster PRIVATE box2d spine_runtime lunasvg::lunasvg plutovg::plutovg ${TCL_LIB} ${FFMPEG_LIBS})

# plutovg's rasterizer setjmp's, which Emscripten implements in the JS
# glue. A later GitHub job runs this same link. The page loads
# shape_raster.js, and that file instantiates shape_raster.wasm.
target_link_options(shape_raster PRIVATE
    "SHELL:-s MODULARIZE=1"
    "SHELL:-s EXPORT_NAME=createShapeRaster"
    "SHELL:-s ENVIRONMENT=web,node"
    "SHELL:-s EXPORTED_FUNCTIONS=[_stim_eval,_stim_error,_stim_result,_stim_load_dg,_stim_gl_init,_shape_bytes,_shape_side,_shape_stride,_shape_span,_stim_redraw,_stim_draw_cx,_stim_draw_cy,_stim_draw_hx,_stim_draw_hy,_stim_draw_opacity,_stim_draw_side,_stim_draw_stride,_stim_draw_bytes,_stim_set_screen,_stim_frame,_stim_wants_frame,_stim_scene_moves,_stim_timer_ticks,_malloc,_free]"
    "SHELL:-s EXPORTED_RUNTIME_METHODS=[HEAPU8,FS]"
    "SHELL:-s ALLOW_MEMORY_GROWTH=1"
    "SHELL:-s MIN_WEBGL_VERSION=2"
    "SHELL:-s MAX_WEBGL_VERSION=2"
    "SHELL:-s GL_PREINITIALIZED_CONTEXT=1"
    "SHELL:-msimd128"
    --no-entry
)
set_target_properties(shape_raster PROPERTIES OUTPUT_NAME "shape_raster")

# Trial mp4 encoder. libx264 stays out of shape_raster: stim2 is MIT
# and x264 is GPL. The page loads trial_encode.js beside the stim module.
set(X264_PREFIX ${CMAKE_BINARY_DIR}/x264-install)
set(X264_LIB ${X264_PREFIX}/lib/libx264.a)
add_custom_command(
    OUTPUT ${X264_LIB}
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/build_x264.sh ${CMAKE_BINARY_DIR}/x264-build ${X264_PREFIX}
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/build_x264.sh
    COMMENT "Building libx264 for WebAssembly"
    VERBATIM
)
add_custom_target(x264_wasm DEPENDS ${X264_LIB})
add_executable(trial_encode encode.cpp)
add_dependencies(trial_encode x264_wasm)
target_include_directories(trial_encode PRIVATE ${X264_PREFIX}/include)
target_link_libraries(trial_encode PRIVATE ${X264_LIB})
target_link_options(trial_encode PRIVATE
    "SHELL:-s MODULARIZE=1"
    "SHELL:-s EXPORT_NAME=createTrialEncode"
    "SHELL:-s ENVIRONMENT=web,worker,node"
    "SHELL:-s EXPORTED_FUNCTIONS=[_enc_open,_enc_rgba,_enc_flush,_enc_sample,_enc_sample_size,_enc_sample_key,_enc_avcc,_enc_avcc_size,_enc_error,_enc_close,_malloc,_free]"
    "SHELL:-s EXPORTED_RUNTIME_METHODS=[HEAPU8]"
    "SHELL:-s ALLOW_MEMORY_GROWTH=1"
    --no-entry
)
set_target_properties(trial_encode PROPERTIES OUTPUT_NAME "trial_encode")
