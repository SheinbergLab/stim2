cmake_minimum_required(VERSION 3.15)
set(CMAKE_CXX_STANDARD 14)

project(stimdlls)

set(CMAKE_BUILD_TYPE Release)

option(STIM2_USE_GLES "use GLES" OFF)

add_definitions(-DUSE_INTERP_RESULT -DUSE_TCL_STUBS)

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
	link_directories(c:/usr/local/lib)
elseif(APPLE)
	include_directories(${APP_DIR} /usr/local/include /usr/local/include/freetype2 /opt/homebrew/include/freetype2)
	link_directories(/usr/local/lib)
else()
	add_definitions(-DLINUX)
	include_directories(${APP_DIR} /usr/local/include)
	link_directories(/usr/local/lib)
	set(TCL_INCLUDE_DIR /usr/include/tcl8.6)
	set(LIBDL dl)
	set(FREETYPE freetype)
	
endif()

set(OpenGL_GL_PREFERENCE GLVND)
find_package(OpenGL REQUIRED)
include_directories(${OPENGL_INCLUDE_DIR} ${TCL_INCLUDE_DIR} src)

set(GLAD "${CMAKE_CURRENT_SOURCE_DIR}/../src/glad.c")

if(WIN32)
set(PRMUTIL "${CMAKE_CURRENT_SOURCE_DIR}/../src/prmutil.c")
set(NEWTON_DEF_FILE "${CMAKE_CURRENT_SOURCE_DIR}/src/newtonstim.def")
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

###############################
# text
###############################
include_directories(/usr/include/freetype2)
 add_library(text SHARED src/text.c src/shaderimage.c src/shaderutils.c src/bstrlib.c src/glsw.c src/targa.c src/lodepng.c ${GLAD})
 
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
# newtonstim
###############################
if (WIN32)
add_library(newtonstim SHARED src/newtonstim.cpp ${GLAD})
elseif(LINUX)
# add_library(newtonstim SHARED src/newtonstim.cpp ${GLAD})
endif()


###############################
# sound
###############################
#include_directories(/usr/local/include/fmod)
#add_library(sound SHARED src/sound.c ${GLAD})

###############################
# Box2Dstim
###############################
add_library(Box2Dstim SHARED src/Box2Dstim.cpp ${GLAD})

###############################
# spine
###############################
 add_library(spine SHARED src/spine.c src/shaderutils.c src/bstrlib.c src/glsw.c src/lodepng.c ${GLAD})

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
	set(DLSH dlsh64.lib)
	set(LAB lablib64.lib)
	set(TCLLIB tclstub86.lib)
	set(CMAKE_EXE_MODULE_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /NODEFAULTLIB:libcmt /NODEFAULTLIB:MSCVRT /def:${DEF_FILE}")
	set(APP_LIB stim2.lib)
	set(SPINE_LIB spine-c.lib)
	set(NEWTON_LIB newton3_x64.lib)
	set(BINK_LIB bink2w64.lib)
	set(BOX2D_LIB box2d.lib)
	set(ZLIB zdll.lib)
	set(FREETYPE freetype.lib)
	set(FMOD_LIB fmod_vc.lib)
	set(PNG libpng16.lib)
elseif(CMAKE_SIZEOF_VOID_P EQUAL 4)
	set(DLSH dlsh.lib)
	set(LAB lablib.lib)
	set(TCLLIB tclstub86.lib)
	set(CMAKE_EXE_MODULE_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /NODEFAULTLIB:libcmt /NODEFAULTLIB:MSCVRT /def:${DEF_FILE}")
	set(APP_LIB "${CMAKE_CURRENT_SOURCE_DIR}/../build_win32/stim2.lib")
	set(SPINE_LIB spine-c.lib)
	set(NEWTON_LIB newton3_x86.lib)
	set(BINK_LIB bink2w32.lib)
	set(BOX2D_LIB box2d_x86.lib)
endif()
elseif(APPLE)
	set(MAIN_APP "${CMAKE_CURRENT_SOURCE_DIR}/../build_macos/stim2")
	set(TCLLIB tclstub8.6)
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
	
	foreach (module polygon metagroup shader mesh motionpatch Box2Dstim bink spine text)
	 set_target_properties(${module} PROPERTIES PREFIX "")
#	 set_target_properties(${module} PROPERTIES SUFFIX .bundle)
 	endforeach()

else()
	#	foreach (module polygon metagroup shader mesh motionpatch newtonstim Box2Dstim bink spine creature)
	foreach (module polygon metagroup shader mesh motionpatch text)
	 set_target_properties(${module} PROPERTIES PREFIX "")
 	endforeach()

	set(SPINE_LIB spine-c)
	set(NEWTON_LIB Newton)
	set(BOX2D_LIB box2d)
#	set(BINK_LIB Bink2x64)	
	set(BINK_LIB ${CMAKE_CURRENT_SOURCE_DIR}/../deps/bink/lib/linux/bink2lnx64.a)
	find_library(LIBDL dl)
	find_library(LIBPTHREAD pthread)
	find_library(LIBOPENAL openal)
	set(FMOD_LIB fmod)
	set(LAB lab)
	set(TCLLIB tclstub8.6)
endif()

foreach (module polygon metagroup shader mesh )
target_link_libraries(${module} ${DLSH} ${BUNDLE_LOAD} ${GLFW} ${TCLLIB} ${OPENGL_LIBRARIES} ${LIBDL} ${LIBPTHREAD} ${APP_LIB})
endforeach()

target_link_libraries(motionpatch ${DLSH} ${BUNDLE_LOAD} ${GLFW} ${TCLLIB} ${OPENGL_LIBRARIES} ${LIBDL} ${LIBPTHREAD} ${LIBMATH} ${APP_LIB})

target_link_libraries(text ${DLSH}  ${BUNDLE_LOAD} ${GLFW} ${TCLLIB} ${OPENGL_LIBRARIES} ${LIBDL} ${LIBPTHREAD} ${LIBMATH} ${APP_LIB} ${FREETYPE} ${PNG} ${ZLIB})

#target_link_libraries(sound ${DLSH} ${BUNDLE_LOAD} ${GLFW} ${TCLLIB} ${OPENGL_LIBRARIES} ${LIBDL} ${LIBPTHREAD} ${LIBMATH} ${APP_LIB} ${FMOD_LIB})

if(APPLE)
target_link_libraries(bink ${DLSH} ${BUNDLE_LOAD} ${GLFW} ${TCLLIB} ${OPENGL_LIBRARIES} "-framework CoreAudio" ${BINK_LIB} ${LIBDL} ${LIBPTHREAD})
elseif(WIN32)
  target_link_libraries(bink ${DLSH} ${BUNDLE_LOAD} ${GLFW} ${TCLLIB} ${OPENGL_LIBRARIES} ${LIBDL} ${LIBPTHREAD} ${BINK_LIB} ${APP_LIB})
else()
	target_link_libraries(bink ${DLSH} ${BUNDLE_LOAD} ${GLFW} ${TCLLIB} ${OPENGL_LIBRARIES} ${BINK_LIB} ${LIBOPENAL} ${LIBDL} ${LIBPTHREAD})
endif()
if (WIN32)
target_link_libraries(newtonstim ${DLSH} ${BUNDLE_LOAD} ${GLFW} ${TCLLIB} ${OPENGL_LIBRARIES} ${LIBDL} ${LIBPTHREAD} ${NEWTON_LIB} ${APP_LIB} "-def:${NEWTON_DEF_FILE}")
elseif(LINUX)
# target_link_libraries(newtonstim ${DLSH} ${BUNDLE_LOAD} ${GLFW} ${TCLLIB} ${OPENGL_LIBRARIES} ${LIBDL} ${LIBPTHREAD} ${NEWTON_LIB} ${APP_LIB})
endif()

if (WIN32)
target_link_libraries(Box2Dstim ${DLSH} ${BUNDLE_LOAD} ${GLFW} ${TCLLIB} ${OPENGL_LIBRARIES} ${LIBDL} ${LIBPTHREAD} ${BOX2D_LIB} ${APP_LIB} "-def:${BOX2D_DEF_FILE}")
else()
target_link_libraries(Box2Dstim ${DLSH} ${BUNDLE_LOAD} ${GLFW} ${TCLLIB} ${OPENGL_LIBRARIES} ${LIBDL} ${LIBPTHREAD} ${BOX2D_LIB} ${APP_LIB})
endif()

target_link_libraries(spine ${DLSH} ${BUNDLE_LOAD} ${GLFW} ${TCLLIB} ${OPENGL_LIBRARIES} ${LIBDL} ${LIBPTHREAD} ${SPINE_LIB} ${APP_LIB})
