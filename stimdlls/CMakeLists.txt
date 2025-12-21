cmake_minimum_required(VERSION 3.15)
project(stimdlls)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_BUILD_TYPE Release)

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
# Shared Utility Library
###############################
# Build common utilities once as a static library
add_library(stimutils STATIC
    ${SRC_DIR}/shaderutils.c
    ${SRC_DIR}/bstrlib.c
    ${SRC_DIR}/glsw.c
    ${APP_DIR}/glad.c
)

# Image loading utilities (used by shader, text, mesh, spine)
add_library(stimimage STATIC
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
    set(BUNDLE_LOAD "-dynamiclib -undefined dynamic_lookup")
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
endif()

# Common link libraries
set(COMMON_LIBS
    ${DLSH}
    ${BUNDLE_LOAD}
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
    cmake_parse_arguments(MOD "" "" "SOURCES;LIBS" ${ARGN})
    
    add_library(${name} SHARED ${MOD_SOURCES})
    target_link_libraries(${name} stimutils ${COMMON_LIBS} ${MOD_LIBS})
    
    # Remove 'lib' prefix on all platforms
    set_target_properties(${name} PROPERTIES PREFIX "")
endfunction()

###############################
# Modules
###############################

# Basic modules using just stimutils
add_stim_module(polygon SOURCES ${SRC_DIR}/polygon.c)
add_stim_module(image SOURCES ${SRC_DIR}/image.c)
add_stim_module(svg SOURCES ${SRC_DIR}/svg.c)
add_stim_module(metagroup SOURCES ${SRC_DIR}/metagroup.c)

# Shader module with image support
add_stim_module(shader
    SOURCES ${SRC_DIR}/shader.c
    LIBS stimimage
)

# Mesh module with image support
add_stim_module(mesh
    SOURCES ${SRC_DIR}/mesh.c
    LIBS stimimage
)

# Motion patch with simplex noise
add_stim_module(motionpatch
    SOURCES ${SRC_DIR}/motionpatch.c ${SRC_DIR}/open-simplex-noise.c
)

# Spine with image support
add_stim_module(spine
    SOURCES ${SRC_DIR}/spine.c
    LIBS stimimage ${SPINE_LIB}
)

# Box2D (no shader utils needed)
add_library(box2d SHARED ${SRC_DIR}/box2d.c ${APP_DIR}/glad.c)
set_target_properties(box2d PROPERTIES PREFIX "")
if(WIN32)
    target_link_libraries(box2d ${COMMON_LIBS} ${BOX2D_LIB} "-def:${BOX2D_DEF_FILE}")
else()
    target_link_libraries(box2d ${COMMON_LIBS} ${BOX2D_LIB})
endif()

# Tilemap with integrated Box2D and TMX loading
add_library(tilemap SHARED ${SRC_DIR}/tilemap.c ${SRC_DIR}/tmx_xml.cpp ${APP_DIR}/glad.c)
set_target_properties(tilemap PROPERTIES PREFIX "")
target_link_libraries(tilemap ${COMMON_LIBS} ${BOX2D_LIB})
# tinyxml2 - either find package or include source
find_package(tinyxml2 QUIET)
if(tinyxml2_FOUND)
    target_link_libraries(tilemap tinyxml2::tinyxml2)
else()
    target_sources(tilemap PRIVATE ${SRC_DIR}/tinyxml2.cpp)
endif()

###############################
# Text module (not on Windows)
###############################
if(NOT WIN32)
    if(LINUX)
        find_package(ZLIB REQUIRED)
    endif()
    add_stim_module(text
        SOURCES ${SRC_DIR}/text.c
        LIBS stimimage ${FREETYPE} ${PNG} ${ZLIB_LIBRARIES}
    )
endif()

###############################
# Video module (FFmpeg) - not on Windows
###############################
if(NOT WIN32)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(FFMPEG REQUIRED libavformat libavcodec libswscale libavutil)

    add_library(video SHARED ${SRC_DIR}/video.c)
    target_link_libraries(video stimutils ${COMMON_LIBS} ${FFMPEG_LIBRARIES})
    target_include_directories(video PRIVATE ${FFMPEG_INCLUDE_DIRS})
    set_target_properties(video PROPERTIES PREFIX "")

    if(APPLE)
        set_target_properties(video PROPERTIES
            LINK_FLAGS "-Wl,-flat_namespace -Wl,-undefined,dynamic_lookup"
        )
        add_custom_command(TARGET video POST_BUILD
            COMMAND ${CMAKE_INSTALL_NAME_TOOL} -add_rpath "/opt/homebrew/lib" $<TARGET_FILE:video>
        )
    endif()
endif()