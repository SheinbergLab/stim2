cmake_minimum_required(VERSION 3.15)
project(stimdlls)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_BUILD_TYPE Release)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

###############################
# Platform Detection
###############################
option(STIM2_USE_GLES "use GLES" OFF)

if(UNIX AND NOT APPLE)
    set(LINUX TRUE)
endif()

# Auto-detect Raspberry Pi
if(LINUX AND (CMAKE_SYSTEM_PROCESSOR MATCHES "aarch" OR CMAKE_SYSTEM_PROCESSOR MATCHES "arm"))
    message(STATUS "Linux ARM detected - assuming Raspberry Pi, enabling GLES")
    set(STIM2_USE_GLES ON)
endif()

message(STATUS "Platform: ${CMAKE_SYSTEM_NAME} / ${CMAKE_SYSTEM_PROCESSOR}")
message(STATUS "STIM2_USE_GLES: ${STIM2_USE_GLES}")

###############################
# Common Definitions
###############################
add_definitions(-DUSE_TCL_STUBS)

if(STIM2_USE_GLES)
    add_definitions(-DSTIM2_USE_GLES=1)
endif()

if(LINUX)
    add_definitions(-DLINUX)
endif()

###############################
# Paths
###############################
set(APP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../src")
set(SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")

# Common include directories
include_directories(
    ${SRC_DIR}
    ${APP_DIR}
    ../../dlsh/src/
    ../../dlsh/src/lablib
)

# Platform-specific paths
if(WIN32)
    include_directories(c:/usr/local/include c:/usr/local/include/freetype)
    link_directories(c:/usr/local/lib/$ENV{VSCMD_ARG_TGT_ARCH})
elseif(APPLE)
    include_directories(
        /usr/local/include
        /usr/local/include/freetype2
        /opt/homebrew/include
        /opt/homebrew/include/freetype2
        /opt/homebrew/include/tcl-tk
    )
    link_directories(/usr/local/lib /opt/homebrew/lib)
    set(CMAKE_SKIP_RPATH TRUE)
else()
    include_directories(/usr/local/include /usr/include/freetype2)
    link_directories(/usr/local/lib)
endif()

# OpenGL
set(OpenGL_GL_PREFERENCE GLVND)
find_package(OpenGL REQUIRED)
include_directories(${OPENGL_INCLUDE_DIR})

###############################
# Shared Utility Sources (compiled into each module that needs them)
###############################
set(STIMUTILS_SOURCES
    ${SRC_DIR}/shaderutils.c
    ${SRC_DIR}/bstrlib.c
    ${SRC_DIR}/glsw.c
    ${APP_DIR}/glad.c
)

set(STIMIMAGE_SOURCES
    ${SRC_DIR}/shaderimage.c
    ${SRC_DIR}/targa.c
    ${SRC_DIR}/lodepng.c
)

###############################
# Platform-specific Libraries
###############################
if(WIN32)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(DLSH libdlsh.lib)
        set(GLFW glfw3.lib)
        set(TCLLIB tclstub.lib)
        set(APP_LIB stim2.lib)
        set(SPINE_LIB spine-c.lib)
        set(BOX2D_LIB libbox2d.lib)
        set(FREETYPE freetype.lib)
        set(PNG libpng16.lib)
        set(BOX2D_DEF_FILE "${SRC_DIR}/Box2Dstim.def")
        set(PRMUTIL "${APP_DIR}/prmutil.c")
    endif()
elseif(APPLE)
    set(DLSH dlsh)
    set(GLFW glfw3)
    set(TCLLIB tclstub)
    set(SPINE_LIB spine-c)
    set(BOX2D_LIB box2D)
    find_library(FREETYPE NAMES "libfreetype.a")
    find_library(PNG NAMES "libpng16.a")
else()
    # Linux
    set(DLSH dlsh)
    set(GLFW glfw3)
    set(TCLLIB tclstub)
    set(SPINE_LIB spine-c)
    find_library(BOX2D_LIB NAMES "libbox2d.a")
    find_library(LIBDL dl)
    find_library(LIBPTHREAD pthread)
    find_library(FREETYPE freetype)    
endif()

# Common link libraries
if(APPLE)
    set(CMAKE_MODULE_LINKER_FLAGS "-undefined dynamic_lookup")
endif()

set(COMMON_LIBS
    ${DLSH}
    ${GLFW}
    ${TCLLIB}
    ${OPENGL_LIBRARIES}
    ${LIBDL}
    ${LIBPTHREAD}
    ${APP_LIB}
)

###############################
# Helper function for modules
###############################
function(add_stim_module name)
    cmake_parse_arguments(MOD "NO_STIMUTILS;WITH_IMAGE" "" "SOURCES;LIBS;INCLUDES" ${ARGN})
    
    # Build source list
    set(ALL_SOURCES ${MOD_SOURCES})
    
    if(NOT MOD_NO_STIMUTILS)
        list(APPEND ALL_SOURCES ${STIMUTILS_SOURCES})
    endif()
    
    if(MOD_WITH_IMAGE)
        list(APPEND ALL_SOURCES ${STIMIMAGE_SOURCES})
    endif()
    
    # Use MODULE type - these are dynamically loaded plugins
    add_library(${name} MODULE ${ALL_SOURCES})
    
    target_link_libraries(${name} ${COMMON_LIBS} ${MOD_LIBS})
    
    if(MOD_INCLUDES)
        target_include_directories(${name} PRIVATE ${MOD_INCLUDES})
    endif()
    
    # Remove 'lib' prefix on all platforms
    set_target_properties(${name} PROPERTIES PREFIX "")
    
    # Keep .so extension on Linux, .dylib on macOS
    if(APPLE)
        set_target_properties(${name} PROPERTIES SUFFIX ".dylib")
    endif()
endfunction()

###############################
# Modules
###############################

# Basic modules using just stimutils
add_stim_module(polygon SOURCES ${SRC_DIR}/polygon.c)
add_stim_module(image SOURCES ${SRC_DIR}/image.c)

# =============================================================================
# LunaSVG Integration via FetchContent
# =============================================================================
include(FetchContent)

FetchContent_Declare(
    lunasvg
    GIT_REPOSITORY https://github.com/sammycage/lunasvg.git
    GIT_TAG        v3.5.0
    GIT_SHALLOW    TRUE
)

# Disable examples
set(LUNASVG_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(lunasvg)

# SVG module 
add_stim_module(svg SOURCES ${SRC_DIR}/svg.cpp LIBS lunasvg::lunasvg)

# Metagroup module
add_stim_module(metagroup SOURCES ${SRC_DIR}/metagroup.c)

# Shader module with image support
add_stim_module(shader
    SOURCES ${SRC_DIR}/shader.c
    WITH_IMAGE
)

# Mesh module with image support
add_stim_module(mesh
    SOURCES ${SRC_DIR}/mesh.c
    WITH_IMAGE
)

# Motion patch with simplex noise
add_stim_module(motionpatch
    SOURCES ${SRC_DIR}/motionpatch.c ${SRC_DIR}/open-simplex-noise.c
)

# Spine with image support
add_stim_module(spine
    SOURCES ${SRC_DIR}/spine.c
    WITH_IMAGE
    LIBS ${SPINE_LIB}
)

# Box2D (no stimutils needed)
if(WIN32)
    add_stim_module(box2d 
        NO_STIMUTILS
        SOURCES ${SRC_DIR}/box2d.c ${APP_DIR}/glad.c
        LIBS ${BOX2D_LIB} "-def:${BOX2D_DEF_FILE}"
    )
else()
    add_stim_module(box2d 
        NO_STIMUTILS
        SOURCES ${SRC_DIR}/box2d.c ${APP_DIR}/glad.c
        LIBS ${BOX2D_LIB}
    )
endif()

# World - 2D game world system
set(WORLD_SOURCES
    ${SRC_DIR}/world.c
    ${SRC_DIR}/world_camera.c
    ${SRC_DIR}/world_atlas.c
    ${SRC_DIR}/world_render.c
    ${SRC_DIR}/world_sprite.c
    ${SRC_DIR}/world_spritesheet.c    
    ${SRC_DIR}/world_tilemap.c    
    ${SRC_DIR}/tmx_xml.cpp
    ${SRC_DIR}/aseprite_json.cpp
    ${APP_DIR}/glad.c
)

add_stim_module(world
    NO_STIMUTILS
    SOURCES ${WORLD_SOURCES}
    INCLUDES ${SRC_DIR}
    LIBS ${BOX2D_LIB}
)

find_package(tinyxml2 QUIET)
if(tinyxml2_FOUND)
    target_link_libraries(world tinyxml2::tinyxml2)
else()
    target_sources(world PRIVATE ${SRC_DIR}/tinyxml2.cpp)
endif()

###############################
# Text module 
###############################
add_stim_module(text SOURCES ${SRC_DIR}/text.c)

###############################
# Video module (FFmpeg) - not on Windows
###############################
if(NOT WIN32)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(FFMPEG REQUIRED libavformat libavcodec libswscale libavutil)

    add_stim_module(video
        SOURCES ${SRC_DIR}/video.c
        INCLUDES ${FFMPEG_INCLUDE_DIRS}
        LIBS ${FFMPEG_LIBRARIES}
    )

    # Apple needs rpath for FFmpeg libs
    if(APPLE)
        add_custom_command(TARGET video POST_BUILD
            COMMAND ${CMAKE_INSTALL_NAME_TOOL} -add_rpath "/opt/homebrew/lib" $<TARGET_FILE:video>
        )
    endif()
endif()
