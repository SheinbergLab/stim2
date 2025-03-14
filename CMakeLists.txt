cmake_minimum_required(VERSION 3.15)
set(CMAKE_CXX_STANDARD 17)

if(NOT DEFINED PROJECT_VERSION)
    set(PROJECT_VERSION 0.0.0)
endif()
project(stim2 VERSION ${PROJECT_VERSION})

set(CMAKE_BUILD_TYPE Debug)
set(CMAKE_BUILD_TYPE Release)

set(EMBED_PYTHON False)

if (EMBED_PYTHON)
    add_definitions(-DEMBED_PYTHON)
    if (WIN32)
        if(CMAKE_SIZEOF_VOID_P EQUAL 8)
            set(ENV{PATH} "../../../../python/x86-64/python36;$ENV{PATH}")
        else()
            set(ENV{PATH} "../../../../python/x86/python36;$ENV{PATH}")
        endif()
    endif()
endif()

option(STIM2_USE_GLES "use GLES" OFF)
if (CMAKE_SYSTEM_NAME MATCHES "Linux")
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm")
        message(STATUS "${CMAKE_SYSTEM_NAME} and ${CMAKE_SYSTEM_PROCESSOR}: assuming Raspberry Pi, setting STIM2_USE_GLES.")
        set(STIM2_USE_GLES ON)
    endif()
endif()

if (EMBED_PYTHON)
    add_subdirectory(pybind11 REQUIRED)
endif()

add_definitions(-DIMGUI_IMPL_OPENGL_LOADER_GLAD)
set(IMGUI_DIR "imgui")

#add_definitions(-DJETSON_NANO)
#add_definitions(-DJETSON_XAVIER)

if (STIM2_USE_GLES)
    add_definitions(-DSTIM2_USE_GLES)
endif()

if(WIN32)
    include_directories(c:/usr/local/include ${IMGUI_DIR} ${IMGUI_DIR}/backends)
    link_directories(c:/usr/local/lib/$ENV{VSCMD_ARG_TGT_ARCH} ${PYTHON_LIB_DIR})
    add_definitions(-DNOMINMAX -D_WIN32)
elseif(APPLE)
    include_directories(/usr/local/include ${IMGUI_DIR} ${IMGUI_DIR}/backends)
    link_directories(/usr/local/lib)
else()
    include_directories(/usr/local/include ${IMGUI_DIR} ${IMGUI_DIR}/backends)
    link_directories(/usr/local/lib)
    set(LIBDL dl)
endif()

if(APPLE)
    set(ICON_NAME "stim2.icns")
    set(ICON_PATH ${PROJECT_SOURCE_DIR}/res/${ICON_NAME})
    set(foo_ICON ${ICON_PATH})
    set_source_files_properties(${foo_ICON} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)

    file(COPY ${ICON_PATH} DESTINATION "stim2.app/Contents/Resources")
    add_executable(stim2 MACOSX_BUNDLE ${ICON_PATH} src/stim2.cpp src/objgroup.c src/grobj.c
        src/tclproc.c src/prmutil.c src/timer.cpp src/glad.c src/rawapi.c src/imgui_console.cpp
        ${IMGUI_DIR}/imgui.cpp ${IMGUI_DIR}/imgui_demo.cpp ${IMGUI_DIR}/imgui_draw.cpp
        ${IMGUI_DIR}/imgui_tables.cpp ${IMGUI_DIR}/imgui_widgets.cpp
        ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp )

    add_custom_command(TARGET stim2 vPOST_BUILD COMMAND
        dylibbundler -of -b -x $<TARGET_FILE:stim2> -d ./stim2.app/Contents/MacOS/lib -p @executable_path/lib -i /usr/local/lib)

    add_custom_command(TARGET stim2 POST_BUILD COMMAND
        ${CMAKE_INSTALL_NAME_TOOL} -add_rpath "@executable_path/stimdlls"
        $<TARGET_FILE:stim2>)

    add_custom_command(TARGET stim2 POST_BUILD COMMAND
        ${CMAKE_INSTALL_NAME_TOOL} -add_rpath "@executable_path/../lib"
        $<TARGET_FILE:stim2>)

else()
    add_executable(stim2 src/stim2.cpp src/objgroup.c src/grobj.c
        src/tclproc.c src/prmutil.c src/timer.cpp src/glad.c src/rawapi.c src/imgui_console.cpp
        ${IMGUI_DIR}/imgui.cpp ${IMGUI_DIR}/imgui_demo.cpp ${IMGUI_DIR}/imgui_draw.cpp
        ${IMGUI_DIR}/imgui_tables.cpp ${IMGUI_DIR}/imgui_widgets.cpp
        ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp)
endif()

set(OpenGL_GL_PREFERENCE GLVND)
find_package(OpenGL REQUIRED)
include_directories(${OPENGL_INCLUDE_DIR} ${TCL_INCLUDE_DIR} src)

include_directories(${PYTHON_INCLUDE_DIR})
link_directories(${PYTHON_LIB_DIR})

if(WIN32)
    set(GLFW_LIB glfw3)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(TCLLIB tcl90)
    elseif(CMAKE_SIZEOF_VOID_P EQUAL 4)
        set(TCLLIB tcl90)
    endif()
    set(DEF_FILE "${CMAKE_CURRENT_SOURCE_DIR}/src/stim2.def")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS}  /NODEFAULTLIB:MSVCRTD /def:${DEF_FILE}")
elseif(APPLE)
    set(TCLLIB tcl9.0)
    # find_library(TCLLIB "libtcl9.0" tcl)
    set(COCOA "-framework Cocoa")
    set(GLFW_LIB glfw)
else()
    set(GLFW_LIB glfw3)
    find_library(GLFW NAMES "libglfw3.a")
    # set(GLFW /usr/local/lib/libglfw3.a)
    find_library(LIBDL dl)
    find_library(LIBPTHREAD pthread)
    find_library(TCLLIB tcl9.0)
    # set(JETSON_GPIO_LIB JetsonGPIO)
    add_link_options("-rdynamics")
endif()

# export symbols for shared objects loaded at runtime
set_property(TARGET stim2 PROPERTY ENABLE_EXPORTS ON)

if(EMBED_PYTHON)
    target_link_libraries(stim2 PRIVATE ${GLFW_LIB} ${TCLLIB} ${OPENGL_LIBRARIES} ${LIBDL} ${LIBPTHREAD} ${JETSON_GPIO_LIB} pybind11::embed)
else()
    if(WIN32)
        target_link_libraries(stim2 ${GLFW_LIB} ${TCLLIB} ${COCOA} ${OPENGL_LIBRARIES} ${LIBDL} ${LIBPTHREAD} ${JETSON_GPIO_LIB} zlibstatic)
    else()
        target_link_libraries(stim2 ${GLFW_LIB} ${TCLLIB} ${COCOA} ${OPENGL_LIBRARIES} ${LIBDL} ${LIBPTHREAD} ${JETSON_GPIO_LIB} z)
    endif()
endif()

install(TARGETS stim2 DESTINATION bin)

if(EMBED_PYTHON)
    if(WIN32)
        if(CMAKE_SIZEOF_VOID_P EQUAL 8)
            set(PYTHON_FOLDER python-3.6.4-embed-amd64)
        elseif(CMAKE_SIZEOF_VOID_P EQUAL 4)
            set(PYTHON_FOLDER python-3.6.4-embed-win32)
        endif()
        add_custom_command(TARGET stim2 POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/python/${PYTHON_FOLDER}
            "$<TARGET_FILE_DIR:stim2>")
    elseif(APPLE)

    else()

    endif()
endif()

set(CPACK_PACKAGE_NAME stim2)
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Multiplatform OpenGL presentation program.")
set(CPACK_PACKAGE_CONTACT SheinbergLab)

set(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS ${TCLLIB})
include(InstallRequiredSystemLibraries)

if(WIN32)
    # TODO
elseif(APPLE)
    # TODO
else()
    set(CPACK_PACKAGING_INSTALL_PREFIX "/usr/local")
    set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "zlib1g, libc6, libdecor-0-0, libegl1, libglx0, libopengl0, libwayland-client0, libwayland-cursor0, libwayland-egl1, libx11-6, libx11-xcb1, libxcursor1, libxext6, libxi6, libxinerama1, libxkbcommon0, libxrandr2, libxrender1, mesa-utils, libgl1-mesa-dri, libegl-mesa0")
    set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA ${CMAKE_CURRENT_SOURCE_DIR}/dpkg/postinst)
endif()

include(CPack)
