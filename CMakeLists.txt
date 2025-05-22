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
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch" OR CMAKE_SYSTEM_PROCESSOR MATCHES "arm")
        message(STATUS "Linux and aarch/arm -- assuming Raspberry Pi, setting STIM2_USE_GLES.")
        set(STIM2_USE_GLES ON)
    endif()
endif()
message(STATUS "${CMAKE_SYSTEM_NAME} and ${CMAKE_SYSTEM_PROCESSOR}: STIM2_USE_GLES=${STIM2_USE_GLES}")

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
    # libglm and libtcl may be installed from source or by homebrew.
    include_directories("/usr/local/include" "/opt/homebrew/include" "/opt/homebrew/include/tcl-tk" ${IMGUI_DIR} ${IMGUI_DIR}/backends)
    link_directories("/usr/local/lib" "/opt/homebrew/lib")
else()
    include_directories(/usr/local/include ${IMGUI_DIR} ${IMGUI_DIR}/backends)
    link_directories(/usr/local/lib)
    set(LIBDL dl)
endif()

if(APPLE)
    add_executable(stim2 MACOSX_BUNDLE src/stim2.cpp src/objgroup.c src/grobj.c
        src/tclproc.c src/prmutil.c src/timer.cpp src/glad.c src/rawapi.c src/imgui_console.cpp
        ${IMGUI_DIR}/imgui.cpp ${IMGUI_DIR}/imgui_demo.cpp ${IMGUI_DIR}/imgui_draw.cpp
        ${IMGUI_DIR}/imgui_tables.cpp ${IMGUI_DIR}/imgui_widgets.cpp
        ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp )
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
    find_library(GLFW_LIB NAMES "libglfw3.a")

    # set(TCLLIB tcl9.1)
    # TCL 9.1 is not yet available from homebrew.
    # Locate TCL library files, as installed by homebrew.
    find_library(TCLLIB "tcl9.0")
    get_filename_component(LIBTCL_ACTUAL ${TCLLIB} REALPATH)
    get_filename_component(LIBTCL_DIR ${LIBTCL_ACTUAL} DIRECTORY)

    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -framework Cocoa -framework IOKit -framework OpenGL -framework QuartzCore")
else()
    set(GLFW_LIB glfw3)
    find_library(GLFW NAMES "libglfw3.a")
    # set(GLFW /usr/local/lib/libglfw3.a)
    find_library(LIBDL dl)
    find_library(LIBPTHREAD pthread)
    find_library(TCLLIB tcl9.1)
    # set(JETSON_GPIO_LIB JetsonGPIO)
    add_link_options("-rdynamics")
endif()

# export symbols for shared objects loaded at runtime
set_property(TARGET stim2 PROPERTY ENABLE_EXPORTS ON)

if(EMBED_PYTHON)
    target_link_libraries(stim2 PRIVATE ${GLFW_LIB} ${TCLLIB} ${OPENGL_LIBRARIES} ${LIBDL} ${LIBPTHREAD} ${JETSON_GPIO_LIB} pybind11::embed)
else()
    if(WIN32)
        target_link_libraries(stim2 ${GLFW_LIB} ${TCLLIB} ${OPENGL_LIBRARIES} ${LIBDL} ${LIBPTHREAD} ${JETSON_GPIO_LIB} zlibstatic)
    else()
        target_link_libraries(stim2 ${GLFW_LIB} ${TCLLIB} ${OPENGL_LIBRARIES} ${LIBDL} ${LIBPTHREAD} ${JETSON_GPIO_LIB} z)
    endif()
endif()

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
set(CPACK_PACKAGE_VENDOR SheinbergLab)
set(CPACK_COMPONENTS_ALL stim2)

set(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS ${TCLLIB})
include(InstallRequiredSystemLibraries)

if(WIN32)
    install(TARGETS stim2 DESTINATION bin)
elseif(APPLE)
    # Wrap the stim2 executable in a stim2.app bundle.
    set_target_properties(stim2 PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME stim2
        MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
        MACOSX_BUNDLE_GUI_IDENTIFIER org.sheinberglab.stim2
        MACOSX_BUNDLE_ICON_FILE stim2
    )

    # Copy the app icon that will show in finder, dock, etc.
    target_sources(stim2 PRIVATE ${CMAKE_SOURCE_DIR}/macos/stim2.icns)
    set_source_files_properties(${CMAKE_SOURCE_DIR}/macos/stim2.icns PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")

    # "Fix up" the app bundle to make it relocatable:
    #  - Copy dylib dependencies into the bundle.
    #  - Configure the executable and dylibs to load dylibs from within the bundle.
    set(CMAKE_SKIP_RPATH TRUE)
    set(APP_BUNDLE ${CMAKE_BINARY_DIR}/Release/stim2.app)
    install(
        CODE "include(BundleUtilities)
        set(BU_CHMOD_BUNDLE_ITEMS ON)
        fixup_bundle(\"${APP_BUNDLE}\" \"\" \"\")"
        COMPONENT stim2
    )

    if(LIBTCL_DIR)
        # Copy TCL library files into the bundle as well.
        install(
            DIRECTORY ${LIBTCL_DIR}/tcl9.0/
            DESTINATION ${APP_BUNDLE}/Contents/lib/tcl9.0
            COMPONENT stim2
        )
    endif()

    # Add stimdlls (if build previously with ./stimdlls/CMakeLists.txt)
    file(GLOB STIMDLLS stimdlls/build/Release/*.dylib)
    message("Bundling STIMDLLS: ${STIMDLLS}")
    install(
        FILES ${STIMDLLS}
        DESTINATION ${APP_BUNDLE}/Contents/stimdlls/build_macos
        COMPONENT stim2
    )

    # Make each stimdll relocatable:
    #  - Copy any dylib dependencies into the bundle.
    #  - Configure the each stimdll to load dylibs from within the bundle.
    set(DYLIBBUNDLER_COMMAND "dylibbundler --bundle-deps --no-codesign -s /usr/local/lib -s deps/bink/lib/macos --dest-dir ${APP_BUNDLE}/Contents/Frameworks --install-path @executable_path/../Frameworks/")
    install(
        CODE "file(GLOB BUNDLE_STIMDLLS ${APP_BUNDLE}/Contents/stimdlls/build_macos/*.dylib)"
        CODE "list(JOIN BUNDLE_STIMDLLS \" -x \" FIX_FILES)"
        CODE "separate_arguments(FIX_FILES_ARGS UNIX_COMMAND \$\{FIX_FILES\})"
        CODE "message(\"Fixing stimdlls:\")"
        CODE "execute_process(COMMAND_ECHO STDOUT COMMAND ${DYLIBBUNDLER_COMMAND} -x \$\{FIX_FILES_ARGS\})"
        COMPONENT stim2
    )

    if(DEFINED "CMAKE_XCODE_ATTRIBUTE_DEVELOPMENT_TEAM")
        # Configure for apple code signing and notarization.
        set(CMAKE_XCODE_ATTRIBUTE_OTHER_CODE_SIGN_FLAGS "--strict --timestamp --options=runtime")
        set(CMAKE_XCODE_ATTRIBUTE_CODE_SIGN_INJECT_BASE_ENTITLEMENTS "NO")
        set(CMAKE_XCODE_ATTRIBUTE_CODE_SIGN_STYLE "Manual")
        set(CMAKE_OSX_DEPLOYMENT_TARGET "14.0")
        set(CMAKE_XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "Developer ID Application")

        # "Fixing up" the app bundle invalidates previously signed binaries.
        # Go back and sign things again: the executable, any relocated dylibs, and the bundle itself.
        set(CODESIGN_COMMAND /usr/bin/codesign --force --verify ${CMAKE_XCODE_ATTRIBUTE_OTHER_CODE_SIGN_FLAGS} --entitlements ${CMAKE_CURRENT_SOURCE_DIR}/macos/stim2.entitlements --sign)
        install(
            CODE "file(GLOB BUNDLE_DYLIBS ${APP_BUNDLE}/Contents/Frameworks/*.dylib)"
            CODE "execute_process(COMMAND ${CODESIGN_COMMAND} \"${CMAKE_XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY}\" \$\{BUNDLE_DYLIBS\})"
            CODE "file(GLOB BUNDLE_STIMDLLS ${APP_BUNDLE}/Contents/stimdlls/build_macos/*.dylib)"
            CODE "execute_process(COMMAND ${CODESIGN_COMMAND} \"${CMAKE_XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY}\" \$\{BUNDLE_STIMDLLS\})"
            CODE "execute_process(COMMAND ${CODESIGN_COMMAND} \"${CMAKE_XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY}\" ${APP_BUNDLE}/Contents/MacOS/stim2)"
            CODE "execute_process(COMMAND ${CODESIGN_COMMAND} \"${CMAKE_XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY}\" ${APP_BUNDLE})"
            COMPONENT stim2
        )
    endif()

    # We want to install the "Fixed up" and re-signed bundle, so this instruction goes last.
    install(TARGETS stim2 BUNDLE DESTINATION . COMPONENT stim2)
else()
    set(CPACK_PACKAGING_INSTALL_PREFIX "/usr/local")
    set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "zlib1g, libc6, libdecor-0-0, libegl1, libglx0, libopengl0, libwayland-client0, libwayland-cursor0, libwayland-egl1, libx11-6, libx11-xcb1, libxcursor1, libxext6, libxi6, libxinerama1, libxkbcommon0, libxrandr2, libxrender1, mesa-utils, libgl1-mesa-dri, libegl-mesa0")
    set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA ${CMAKE_CURRENT_SOURCE_DIR}/dpkg/postinst)

    install(TARGETS stim2 DESTINATION bin)
endif()

include(CPack)
