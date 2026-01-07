cmake_minimum_required(VERSION 3.15)
set(CMAKE_CXX_STANDARD 17)

if(NOT DEFINED PROJECT_VERSION)
    set(PROJECT_VERSION 0.0.0)
endif()
project(stim2 VERSION ${PROJECT_VERSION})

set(CMAKE_BUILD_TYPE Release)
# Uncomment for debug builds:
# set(CMAKE_BUILD_TYPE Debug)

# Platform detection for GLES
option(STIM2_USE_GLES "use GLES" OFF)
if (CMAKE_SYSTEM_NAME MATCHES "Linux")
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch" OR CMAKE_SYSTEM_PROCESSOR MATCHES "arm")
        message(STATUS "Linux and aarch/arm -- assuming Raspberry Pi, setting STIM2_USE_GLES.")
        set(STIM2_USE_GLES ON)
    endif()
endif()
message(STATUS "${CMAKE_SYSTEM_NAME} and ${CMAKE_SYSTEM_PROCESSOR}: STIM2_USE_GLES=${STIM2_USE_GLES}")

if (STIM2_USE_GLES)
    add_definitions(-DSTIM2_USE_GLES)
endif()

# ImGui configuration
add_definitions(-DIMGUI_IMPL_OPENGL_LOADER_GLAD)
set(IMGUI_DIR "imgui")

# =============================================================================
# uWebSockets and uSockets setup
# =============================================================================
include(FetchContent)

# Set timeouts for FetchContent
set(FETCHCONTENT_QUIET OFF)
set(CMAKE_TIMEOUT 120)

FetchContent_Declare(
    uwebsockets
    GIT_REPOSITORY https://github.com/uNetworking/uWebSockets.git
    GIT_TAG        v20.62.0
    GIT_SHALLOW    TRUE
    GIT_SUBMODULES ""
    TIMEOUT        120
)

FetchContent_Declare(
    usockets
    GIT_REPOSITORY https://github.com/uNetworking/uSockets.git
    GIT_TAG        v0.8.8
    GIT_SHALLOW    TRUE
    GIT_SUBMODULES ""
    TIMEOUT        120
)

FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE
)

FetchContent_MakeAvailable(usockets uwebsockets json)

# Add uWebSockets include directories
include_directories(${uwebsockets_SOURCE_DIR}/src)
include_directories(${usockets_SOURCE_DIR}/src)

# Disable SSL support in uWebSockets (we don't need it)
add_definitions(-DUWS_NO_ZLIB)
add_definitions(-DLIBUS_NO_SSL)

# Build uSockets library
set(USOCKETS_SOURCES
    ${usockets_SOURCE_DIR}/src/bsd.c
    ${usockets_SOURCE_DIR}/src/context.c
    ${usockets_SOURCE_DIR}/src/loop.c
    ${usockets_SOURCE_DIR}/src/socket.c
)

# Platform-specific event loop
if(APPLE OR WIN32)
    list(APPEND USOCKETS_SOURCES ${usockets_SOURCE_DIR}/src/eventing/libuv.c)
    set(WITH_LIBUV 1)
else()
    list(APPEND USOCKETS_SOURCES ${usockets_SOURCE_DIR}/src/eventing/epoll_kqueue.c)
    set(WITH_LIBUV 0)
endif()

# Check for additional source files
if(EXISTS ${usockets_SOURCE_DIR}/src/loop_data.c)
    list(APPEND USOCKETS_SOURCES ${usockets_SOURCE_DIR}/src/loop_data.c)
endif()

# NOTE: NOT adding SSL support - we don't need it for stim2
# (SSL files would be src/crypto/openssl.c and src/crypto/sni_tree.cpp)

message(STATUS "Building uSockets with sources: ${USOCKETS_SOURCES}")

# Create uSockets library
add_library(usockets STATIC ${USOCKETS_SOURCES})

# NO SSL support - we don't need it for stim2
# (This avoids OpenSSL dependency)

# Platform-specific event loop configuration
if(WIN32)
    target_compile_definitions(usockets PRIVATE LIBUS_USE_LIBUV)
    set(USOCKETS_LIBS ws2_32)
elseif(APPLE)
    target_compile_definitions(usockets PRIVATE LIBUS_USE_LIBUV)
    find_library(LIBUV_LIBRARIES NAMES uv libuv REQUIRED)
    if(NOT LIBUV_LIBRARIES)
        message(FATAL_ERROR "libuv is required on macOS. Install with: brew install libuv")
    endif()
    target_link_libraries(usockets PRIVATE ${LIBUV_LIBRARIES})
    set(USOCKETS_LIBS ${LIBUV_LIBRARIES})
else()
    target_compile_definitions(usockets PRIVATE LIBUS_USE_EPOLL)
    set(USOCKETS_LIBS)
endif()

# Add compile flags for uSockets
if(NOT WIN32)
    target_compile_options(usockets PRIVATE 
        $<$<COMPILE_LANGUAGE:C>:-std=c11>
        -flto
    )
endif()

# =============================================================================
# Platform-specific include and link directories
# =============================================================================
if(WIN32)
    include_directories(c:/usr/local/include ${IMGUI_DIR} ${IMGUI_DIR}/backends)
    link_directories(c:/usr/local/lib/$ENV{VSCMD_ARG_TGT_ARCH})
    add_definitions(-DNOMINMAX -D_WIN32)
elseif(APPLE)
    include_directories(
        "/usr/local/include" 
        "/opt/homebrew/include" 
        "/opt/homebrew/include/tcl-tk" 
        ${IMGUI_DIR} 
        ${IMGUI_DIR}/backends
    )
    link_directories("/usr/local/lib" "/opt/homebrew/lib")
else()
    include_directories(/usr/local/include ${IMGUI_DIR} ${IMGUI_DIR}/backends)
    link_directories(/usr/local/lib)
    set(LIBDL dl)
endif()

# =============================================================================
# Main executable
# =============================================================================
set(STIM2_SOURCES
    src/stim2.cpp
    src/objgroup.c
    src/objname.c
    src/grobj.c
    src/animate.cpp
    src/tclproc.c
    src/prmutil.c
    src/timer.cpp
    src/glad.c
    src/rawapi.c
    src/imgui_console.cpp
    src/WebSocketServer.cpp
    src/TclCompletion.cpp
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)

if(APPLE)
    list(APPEND STIM2_SOURCES src/SleepWakeHandler.mm)
    add_executable(stim2 MACOSX_BUNDLE ${STIM2_SOURCES})
else()
    add_executable(stim2 ${STIM2_SOURCES})
endif()

# Export symbols for shared objects loaded at runtime
set_property(TARGET stim2 PROPERTY ENABLE_EXPORTS ON)

# =============================================================================
# Dependencies
# =============================================================================
set(OpenGL_GL_PREFERENCE GLVND)
find_package(OpenGL REQUIRED)
include_directories(${OPENGL_INCLUDE_DIR} ${TCL_INCLUDE_DIR} src)

# =============================================================================
# Platform-specific library configuration
# =============================================================================
if(WIN32)
    set(GLFW_LIB glfw3)
    set(TCLLIB tcl90)
    set(DEF_FILE "${CMAKE_CURRENT_SOURCE_DIR}/src/stim2.def")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /NODEFAULTLIB:MSVCRTD /def:${DEF_FILE}")
    
    target_link_libraries(stim2 
        ${GLFW_LIB} 
        ${TCLLIB} 
        ${OPENGL_LIBRARIES} 
        zlibstatic
        usockets
        ${USOCKETS_LIBS}
        nlohmann_json::nlohmann_json
    )
    
elseif(APPLE)
    find_library(GLFW_LIB NAMES "libglfw3.a" REQUIRED)
    find_library(TCLLIB "tcl9.0" REQUIRED)
    
    get_filename_component(LIBTCL_ACTUAL ${TCLLIB} REALPATH)
    get_filename_component(LIBTCL_DIR ${LIBTCL_ACTUAL} DIRECTORY)
    
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -framework Cocoa -framework IOKit -framework OpenGL -framework QuartzCore")
    
    # Export symbols for dynamically loaded modules (stimdlls)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-export_dynamic")
    
    target_link_libraries(stim2 
        ${GLFW_LIB} 
        ${TCLLIB} 
        ${OPENGL_LIBRARIES} 
        z
        usockets
        ${USOCKETS_LIBS}
        nlohmann_json::nlohmann_json
    )
    
else()
    # Linux
    set(GLFW_LIB glfw3)
    find_library(GLFW NAMES "libglfw3.a")
    find_library(LIBDL dl REQUIRED)
    find_library(LIBPTHREAD pthread REQUIRED)
    find_library(TCLLIB tcl9.0 REQUIRED)
    
    add_link_options("-rdynamic")
    
    target_link_libraries(stim2 
        ${GLFW_LIB} 
        ${TCLLIB} 
        ${OPENGL_LIBRARIES} 
        ${LIBDL} 
        ${LIBPTHREAD} 
        z
        usockets
        ${USOCKETS_LIBS}
        nlohmann_json::nlohmann_json
    )
endif()

# =============================================================================
# Installation
# =============================================================================
set(CPACK_PACKAGE_NAME stim2)
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Multiplatform OpenGL presentation program.")
set(CPACK_PACKAGE_CONTACT SheinbergLab)
set(CPACK_PACKAGE_VENDOR SheinbergLab)
set(CPACK_COMPONENTS_ALL stim2)

if(WIN32)
    install(TARGETS stim2 DESTINATION bin)
    
elseif(APPLE)
    # macOS bundle configuration
    set_target_properties(stim2 PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME stim2
        MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
        MACOSX_BUNDLE_GUI_IDENTIFIER org.sheinberglab.stim2
        MACOSX_BUNDLE_ICON_FILE stim2
    )
    
    # Copy app icon
    target_sources(stim2 PRIVATE ${CMAKE_SOURCE_DIR}/macos/stim2.icns)
    set_source_files_properties(${CMAKE_SOURCE_DIR}/macos/stim2.icns 
        PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
    
    set(CMAKE_SKIP_RPATH TRUE)
    set(APP_BUNDLE ${CMAKE_BINARY_DIR}/Release/stim2.app)
    
    # Fix up bundle
    install(
        CODE "include(BundleUtilities)
        set(BU_CHMOD_BUNDLE_ITEMS ON)
        fixup_bundle(\"${APP_BUNDLE}\" \"\" \"\")"
        COMPONENT stim2
    )
    
    # Copy TCL library files
    if(LIBTCL_DIR)
        install(
            DIRECTORY ${LIBTCL_DIR}/tcl9.0/
            DESTINATION ${APP_BUNDLE}/Contents/lib/tcl9.0
            COMPONENT stim2
        )
    endif()
    
    # Add stimdlls
    file(GLOB STIMDLLS stimdlls/build/Release/*.dylib)
    message("Bundling STIMDLLS: ${STIMDLLS}")
    install(
        FILES ${STIMDLLS}
        DESTINATION ${APP_BUNDLE}/Contents/MacOS/stimdlls
        COMPONENT stim2
    )
    
    # Add shaders
    file(GLOB STIM_SHADERS shaders/*.glsl)
    message("Bundling shaders: ${STIM_SHADERS}")
    install(
        FILES ${STIM_SHADERS}
        DESTINATION ${APP_BUNDLE}/Contents/Resources/shaders
        COMPONENT stim2
    )
    
    # Add modules (Tcl modules for workspace system)
    message("Bundling modules from modules/")
    install(
        DIRECTORY modules/
        DESTINATION ${APP_BUNDLE}/Contents/Resources/modules
        COMPONENT stim2
        FILES_MATCHING 
        PATTERN "*.tm"
    )
    
    # Add examples
    message("Bundling examples from examples/")
    install(
        DIRECTORY examples/
        DESTINATION ${APP_BUNDLE}/Contents/Resources/examples
        COMPONENT stim2
        FILES_MATCHING 
        PATTERN "*.tcl"
        PATTERN "INDEX"
    )

    # Add assets
    message("Bundling assets from assets/")
    install(
        DIRECTORY assets/
        DESTINATION ${APP_BUNDLE}/Contents/Resources/assets
        COMPONENT stim2
    )

    # Add fonts
    message("Bundling fonts from fonts/")
    install(
        DIRECTORY fonts/
        DESTINATION ${APP_BUNDLE}/Contents/Resources/fonts
        COMPONENT stim2
    )

    # Add config
    set(CONFIG_SCRIPT config/stim2.cfg)
    message("Bundling config script: ${CONFIG_SCRIPT}")
    install(
        FILES ${CONFIG_SCRIPT}
        DESTINATION ${APP_BUNDLE}/Contents/Resources
        COMPONENT stim2
    )
    
    # Add www directory for WebSocket console (preserves subdirectories)
    message("Bundling web console from www/")
    install(
        DIRECTORY www/
        DESTINATION ${APP_BUNDLE}/Contents/Resources/www
        COMPONENT stim2
        FILES_MATCHING 
        PATTERN "*.html"
        PATTERN "*.css"
        PATTERN "*.js"
    )
    
    # Make stimdlls relocatable
    set(DYLIBBUNDLER_COMMAND "dylibbundler --bundle-deps -s /usr/local/lib -s deps/bink/lib/macos --dest-dir ${APP_BUNDLE}/Contents/Frameworks --install-path @rpath")
    install(
        CODE "file(GLOB BUNDLE_STIMDLLS ${APP_BUNDLE}/Contents/MacOS/stimdlls/*.dylib)"
        CODE "list(JOIN BUNDLE_STIMDLLS \" -x \" FIX_FILES)"
        CODE "separate_arguments(FIX_FILES_ARGS UNIX_COMMAND \$\{FIX_FILES\})"
        CODE "message(\"Fixing stimdlls:\")"
        CODE "execute_process(COMMAND_ECHO STDOUT COMMAND ${DYLIBBUNDLER_COMMAND} -x \$\{FIX_FILES_ARGS\})"
        CODE "execute_process(COMMAND_ECHO STDOUT COMMAND ${CMAKE_INSTALL_NAME_TOOL} -add_rpath @executable_path/../Frameworks $<TARGET_FILE:stim2>)"
        CODE "execute_process(COMMAND_ECHO STDOUT COMMAND rm -f ${APP_BUNDLE}/Contents/Frameworks/libdlsh.dylib)"
        COMPONENT stim2
    )
    
    # Code signing (if configured)
    if(DEFINED "CMAKE_XCODE_ATTRIBUTE_DEVELOPMENT_TEAM")
        set(CMAKE_XCODE_ATTRIBUTE_OTHER_CODE_SIGN_FLAGS "--strict --timestamp --options=runtime")
        set(CMAKE_XCODE_ATTRIBUTE_CODE_SIGN_INJECT_BASE_ENTITLEMENTS "NO")
        set(CMAKE_XCODE_ATTRIBUTE_CODE_SIGN_STYLE "Manual")
        set(CMAKE_OSX_DEPLOYMENT_TARGET "14.0")
        set(CMAKE_XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "Developer ID Application")
        
        set(CODESIGN_COMMAND /usr/bin/codesign --force --verify ${CMAKE_XCODE_ATTRIBUTE_OTHER_CODE_SIGN_FLAGS} --entitlements ${CMAKE_CURRENT_SOURCE_DIR}/macos/stim2.entitlements --sign)
        install(
            CODE "file(GLOB BUNDLE_DYLIBS ${APP_BUNDLE}/Contents/Frameworks/*.dylib)"
            CODE "execute_process(COMMAND ${CODESIGN_COMMAND} \"${CMAKE_XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY}\" \$\{BUNDLE_DYLIBS\})"
            CODE "file(GLOB BUNDLE_STIMDLLS ${APP_BUNDLE}/Contents/MacOS/stimdlls/*.dylib)"
            CODE "execute_process(COMMAND ${CODESIGN_COMMAND} \"${CMAKE_XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY}\" \$\{BUNDLE_STIMDLLS\})"
            CODE "execute_process(COMMAND ${CODESIGN_COMMAND} \"${CMAKE_XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY}\" ${APP_BUNDLE}/Contents/MacOS/stim2)"
            CODE "execute_process(COMMAND ${CODESIGN_COMMAND} \"${CMAKE_XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY}\" ${APP_BUNDLE})"
            COMPONENT stim2
        )
    endif()
    
    install(TARGETS stim2 BUNDLE DESTINATION . COMPONENT stim2)
    
else()
    # Linux
    set(CPACK_PACKAGING_INSTALL_PREFIX "/usr/local")
    
    # Distro suffix for Debian packages
    if(NOT DEFINED DISTRO_SUFFIX)
        set(DISTRO_SUFFIX "")
    endif()
    
    # Normalize architecture name
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64")
        set(PACKAGE_ARCH "arm64")
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64")
        set(PACKAGE_ARCH "amd64")
    else()
        set(PACKAGE_ARCH ${CMAKE_SYSTEM_PROCESSOR})
    endif()
    
    if(DISTRO_SUFFIX)
        set(CPACK_DEBIAN_FILE_NAME "${PROJECT_NAME}_${PROJECT_VERSION}_${PACKAGE_ARCH}_${DISTRO_SUFFIX}.deb")
    else()
        set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
    endif()
    
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "zlib1g, libc6, libdecor-0-0, libegl1, libglx0, libopengl0, libwayland-client0, libwayland-cursor0, libwayland-egl1, libx11-6, libx11-xcb1, libxcursor1, libxext6, libxi6, libxinerama1, libxkbcommon0, libxrandr2, libxrender1, mesa-utils, libgl1-mesa-dri, libegl-mesa0, libopenal1, libfreetype6, cage, mpv")
    set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA ${CMAKE_CURRENT_SOURCE_DIR}/dpkg/postinst)
    
    install(TARGETS stim2 DESTINATION "/usr/local/stim2")
    
    # Add stimdlls
    file(GLOB STIMDLLS stimdlls/build/*.so)
    message("Adding STIMDLLS: ${STIMDLLS}")
    install(
        FILES ${STIMDLLS}
        DESTINATION "/usr/local/stim2/stimdlls"
        COMPONENT stim2
    )
    
    # Add shaders
    file(GLOB STIM_SHADERS shaders/*.glsl)
    message("Adding shaders: ${STIM_SHADERS}")
    install(
        FILES ${STIM_SHADERS}
        DESTINATION "/usr/local/stim2/shaders"
        COMPONENT stim2
    )
    
    # Add modules (Tcl modules for workspace system)
    message("Bundling modules from modules/")
    install(
        DIRECTORY modules/
        DESTINATION "/usr/local/stim2/modules"
        COMPONENT stim2
        FILES_MATCHING 
        PATTERN "*.tm"
    )
    
    # Add examples
    message("Bundling examples from examples/")
    install(
        DIRECTORY examples/
        DESTINATION "/usr/local/stim2/examples"
        COMPONENT stim2
        FILES_MATCHING 
        PATTERN "*.tcl"
        PATTERN "INDEX"
    )

    # Add assets
    message("Bundling assets from assets/")
    install(
        DIRECTORY assets/
        DESTINATION "/usr/local/stim2/assets"
        COMPONENT stim2
    )

    # Add local
    message("Bundling local from local/")
    install(
        DIRECTORY local/
        DESTINATION "/usr/local/stim2/local"
        COMPONENT stim2
    )
	
    # Add fonts
    message("Bundling fonts from fonts/")
    install(
        DIRECTORY fonts/
        DESTINATION "/usr/local/stim2/fonts"
        COMPONENT stim2
    )
    
    # Add www directory for WebSocket console (preserves subdirectories)
    message("Bundling web console from www/")
    install(
        DIRECTORY www/
        DESTINATION "/usr/local/stim2/www"
        COMPONENT stim2
        FILES_MATCHING 
        PATTERN "*.html"
        PATTERN "*.css"
        PATTERN "*.js"
    )
    
    # Add config
    set(CONFIG_SCRIPT config/linux.cfg)
    message("Bundling config script: ${CONFIG_SCRIPT}")
    install(
        FILES ${CONFIG_SCRIPT}
        DESTINATION /usr/local/stim2/config
        COMPONENT stim2
    )
    
    # Add systemd service
    set(SYSTEMD_SCRIPT systemd/stim2.service)
    set(SYSTEMD_SCRIPT_X11 systemd/stim2-x11.service)
    message("Bundling systemd service: ${SYSTEMD_SCRIPT}, ${SYSTEMD_SCRIPT_X11}")
    install(
        FILES ${SYSTEMD_SCRIPT} ${SYSTEMD_SCRIPT_X11}
        DESTINATION /usr/local/stim2/systemd
        COMPONENT stim2
    )
endif()

include(CPack)
