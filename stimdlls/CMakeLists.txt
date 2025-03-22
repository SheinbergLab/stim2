cmake_minimum_required(VERSION 3.15)
set(CMAKE_CXX_STANDARD 14)

project(stimdlls)

set(CMAKE_BUILD_TYPE Release)

option(STIM2_USE_GLES "use GLES" OFF)

add_definitions(-DUSE_TCL_STUBS)

if (STIM2_USE_GLES)
add_definitions(-DSTIM2_USE_GLES)
endif()

set(APP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../src")

if(UNIX AND NOT APPLE)
    set(LINUX TRUE)
endif()

include_directories(../../dlsh/src/ ../../dlsh/src/lablib ../deps/bink/include)

if(WIN32)
	include_directories(${APP_DIR} c:/usr/local/include c:/usr/local/include/freetype)
	link_directories(c:/usr/local/lib/$ENV{VSCMD_ARG_TGT_ARCH})
elseif(APPLE)
	include_directories(${APP_DIR} /usr/local/include /usr/local/include/freetype2 /opt/homebrew/include/freetype2)
	link_directories(/usr/local/lib)
else()
	add_definitions(-DLINUX)
	include_directories(${APP_DIR} /usr/local/include)
	link_directories(/usr/local/lib)
	set(LIBDL dl)
	set(FREETYPE freetype)
	
endif()

set(OpenGL_GL_PREFERENCE GLVND)
find_package(OpenGL REQUIRED)
include_directories(${OPENGL_INCLUDE_DIR} ${TCL_INCLUDE_DIR} src)

set(GLAD "${CMAKE_CURRENT_SOURCE_DIR}/../src/glad.c")

if(WIN32)
set(PRMUTIL "${CMAKE_CURRENT_SOURCE_DIR}/../src/prmutil.c")
set(BOX2D_DEF_FILE "${CMAKE_CURRENT_SOURCE_DIR}/src/Box2Dstim.def")
endif()

###############################
# polygon
###############################
 add_library(polygon SHARED src/polygon.c src/shaderutils.c src/bstrlib.c src/glsw.c ${GLAD})

###############################
# metagroup
###############################
 add_library(metagroup SHARED src/metagroup.c ${GLAD})

###############################
# shader
###############################
 add_library(shader SHARED src/shader.c src/shaderimage.c src/shaderutils.c src/bstrlib.c src/glsw.c src/targa.c src/lodepng.c ${GLAD})

if (NOT WIN32)
###############################
# text
###############################
include_directories(/usr/include/freetype2)
 add_library(text SHARED src/text.c src/shaderimage.c src/shaderutils.c src/bstrlib.c src/glsw.c src/targa.c src/lodepng.c ${GLAD})
endif()

###############################
# mesh
###############################
 add_library(mesh SHARED src/mesh.c src/shaderimage.c src/shaderutils.c src/bstrlib.c src/glsw.c src/targa.c src/lodepng.c ${GLAD})

###############################
# motionpatch
###############################
 add_library(motionpatch SHARED src/motionpatch.c src/shaderutils.c src/bstrlib.c src/glsw.c src/open-simplex-noise.c ${GLAD})

###############################
# grannystim
###############################
#if(APPLE)
#set(CMAKE_C_FLAGS  "${CMAKE_C_FLAGS} -D_MACOSX=1")
#endif()
# add_library(grannystim SHARED src/grannystim.c src/shaderutils.c src/bstrlib.c src/glsw.c ${GLAD})

###############################
# bink
###############################
if(APPLE)
add_library(bink SHARED src/bink.c src/shaderutils.c src/bstrlib.c src/glsw.c src/binktexturesGL.cpp src/setup_audioMac.m)
elseif(WIN32)
  add_library(bink SHARED src/bink.c src/shaderutils.c src/bstrlib.c src/glsw.c src/binktexturesGL.cpp ${GLAD} ${PRMUTIL})
else()
	add_library(bink SHARED src/bink.c src/shaderutils.c src/bstrlib.c src/glsw.c src/binktexturesGL.cpp ${GLAD} ${PRMUTIL})
endif()


###############################
# sound
###############################
#include_directories(/usr/local/include/fmod)
#add_library(sound SHARED src/sound.c ${GLAD})

###############################
# spine
###############################
 add_library(spine SHARED src/spine.c src/shaderutils.c src/bstrlib.c src/glsw.c src/lodepng.c ${GLAD})

###############################
# Box2D
###############################
add_library(box2d SHARED src/box2d.c ${GLAD})

###############################
# creature
###############################
if(APPLE)
#include_directories(src/creature src/creature/glm src/creature/gason)
#add_library(creature SHARED src/creature.cpp src/shaderutils.c src/bstrlib.c src/glsw.c src/creature/gason/gason.cpp src/creature/MeshBone.cpp src/creature/CreatureModule.cpp)
elseif(WIN32)

else()

endif()



set(DLSH dlsh)
set(GLFW glfw3)

if(WIN32)
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
	set(DLSH libdlsh.lib)
	set(TCLLIB tclstub.lib)
	set(CMAKE_EXE_MODULE_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /NODEFAULTLIB:libcmt /NODEFAULTLIB:MSCVRT /def:${DEF_FILE}")
	set(APP_LIB stim2.lib)
	set(SPINE_LIB spine-c.lib)
	set(BINK_LIB bink2winrt_$ENV{VSCMD_ARG_TGT_ARCH}.uni10.lib)
	set(BOX2D_LIB box2d.lib)
	set(FREETYPE freetype.lib)
	set(FMOD_LIB fmod_vc.lib)
	set(PNG libpng16.lib)
endif()
elseif(APPLE)
	set(MAIN_APP "${CMAKE_CURRENT_SOURCE_DIR}/../build_macos/stim2")
	set(TCLLIB tclstub)
	set(FMOD_LIB fmod)
### See if can use: -undefined dynamic_lookup
#	set(BUNDLE_LOAD "-bundle_loader ${MAIN_APP}")
	set(BUNDLE_LOAD "-dynamiclib -undefined dynamic_lookup")
	find_library(BINK_LIB, Bink2Mac)
	set(NEWTON_LIB Newton)
	set(SPINE_LIB spine-c)
	set(BINK_LIB Bink2Mac)
	set(BOX2D_LIB box2D)
	find_library(FREETYPE NAMES "libfreetype.a")
	find_library(PNG NAMES "libpng16.a")
	
	foreach (module polygon metagroup shader mesh motionpatch box2d bink spine text)
	 set_target_properties(${module} PROPERTIES PREFIX "")
#	 set_target_properties(${module} PROPERTIES SUFFIX .bundle)
 	endforeach()

elseif(WIN32)
	foreach (module polygon metagroup shader mesh motionpatch box2d)
	 set_target_properties(${module} PROPERTIES PREFIX "")	  
 	endforeach()

	set(SPINE_LIB spine-c)
	set(BOX2D_LIB box2d)
	set(BINK_LIB ${CMAKE_CURRENT_SOURCE_DIR}/../deps/bink/lib/windows/bink2w64.lib)
	find_library(LIBDL dl)
	find_library(LIBPTHREAD pthread)
	find_library(LIBOPENAL openal)
	set(FMOD_LIB fmod)
	set(LAB lab)
	set(TCLLIB tclstub)
else()
	#	foreach (module polygon metagroup shader mesh motionpatch newtonstim box2d bink spine creature)
	foreach (module polygon metagroup shader mesh motionpatch text box2d)
	 set_target_properties(${module} PROPERTIES PREFIX "")
 	endforeach()

	set(SPINE_LIB spine-c)
	set(NEWTON_LIB Newton)
	find_library(BOX2D_LIB NAMES "libbox2d.a")
#	set(BOX2D_LIB box2d)
#	set(BINK_LIB Bink2x64)

if (CMAKE_SYSTEM_NAME MATCHES "Linux")
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch" OR CMAKE_SYSTEM_PROCESSOR MATCHES "arm")
	set(BINK_LIB ${CMAKE_CURRENT_SOURCE_DIR}/../deps/bink/lib/linux/bink2lnxarm64.a)
    else()
	set(BINK_LIB ${CMAKE_CURRENT_SOURCE_DIR}/../deps/bink/lib/linux/bink2lnx64.a)
    endif()
    
endif()

	find_library(LIBDL dl)
	find_library(LIBPTHREAD pthread)
	find_library(LIBOPENAL openal)
	set(FMOD_LIB fmod)
	set(LAB lab)
	set(TCLLIB tclstub)
endif()

foreach (module polygon metagroup shader mesh )
target_link_libraries(${module} ${DLSH} ${BUNDLE_LOAD} ${GLFW} ${TCLLIB} ${OPENGL_LIBRARIES} ${LIBDL} ${LIBPTHREAD} ${APP_LIB})
endforeach()

target_link_libraries(motionpatch ${DLSH} ${BUNDLE_LOAD} ${GLFW} ${TCLLIB} ${OPENGL_LIBRARIES} ${LIBDL} ${LIBPTHREAD} ${LIBMATH} ${APP_LIB})

if(NOT WIN32)
target_link_libraries(text ${DLSH}  ${BUNDLE_LOAD} ${GLFW} ${TCLLIB} ${OPENGL_LIBRARIES} ${LIBDL} ${LIBPTHREAD} ${LIBMATH} ${APP_LIB} ${FREETYPE} ${PNG} ${ZLIB})
endif()

#target_link_libraries(sound ${DLSH} ${BUNDLE_LOAD} ${GLFW} ${TCLLIB} ${OPENGL_LIBRARIES} ${LIBDL} ${LIBPTHREAD} ${LIBMATH} ${APP_LIB})

if(APPLE)
target_link_libraries(bink ${DLSH} ${BUNDLE_LOAD} ${GLFW} ${TCLLIB} ${OPENGL_LIBRARIES} "-framework CoreAudio" ${BINK_LIB} ${LIBDL} ${LIBPTHREAD})
elseif(WIN32)
  target_link_libraries(bink ${DLSH} ${BUNDLE_LOAD} ${GLFW} ${TCLLIB} ${OPENGL_LIBRARIES} ${LIBDL} ${LIBPTHREAD} ${BINK_LIB} ${APP_LIB})
else()
	target_link_libraries(bink ${DLSH} ${BUNDLE_LOAD} ${GLFW} ${TCLLIB} ${OPENGL_LIBRARIES} ${BINK_LIB} ${LIBOPENAL} ${LIBDL} ${LIBPTHREAD})
endif()

if (WIN32)
target_link_libraries(box2d ${DLSH} ${BUNDLE_LOAD} ${GLFW} ${TCLLIB} ${OPENGL_LIBRARIES} ${LIBDL} ${LIBPTHREAD} ${BOX2D_LIB} ${APP_LIB} "-def:${BOX2D_DEF_FILE}")
else()
target_link_libraries(box2d ${DLSH} ${BUNDLE_LOAD} ${GLFW} ${TCLLIB} ${OPENGL_LIBRARIES} ${LIBDL} ${LIBPTHREAD} ${BOX2D_LIB} ${APP_LIB})
endif()

target_link_libraries(spine ${DLSH} ${BUNDLE_LOAD} ${GLFW} ${TCLLIB} ${OPENGL_LIBRARIES} ${LIBDL} ${LIBPTHREAD} ${SPINE_LIB} ${APP_LIB})
