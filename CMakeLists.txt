cmake_minimum_required(VERSION 3.20)
if(APPLE)
    project(ModernGekko LANGUAGES C CXX OBJCXX)
else()
    project(ModernGekko LANGUAGES C CXX)
endif()

include(CTest)
include(CheckIncludeFileCXX)

if(WIN32)
    set(MODERNGEKKO_DYNAMIC_MODULES_DEFAULT ON)
else()
    check_include_file_cxx(dlfcn.h MODERNGEKKO_HAS_DLFCN)
    set(MODERNGEKKO_DYNAMIC_MODULES_DEFAULT ${MODERNGEKKO_HAS_DLFCN})
endif()
option(MODERNGEKKO_ENABLE_DYNAMIC_MODULES
       "Load game modules through the host dynamic-library API"
       ${MODERNGEKKO_DYNAMIC_MODULES_DEFAULT})

option(MODERNGEKKO_ENABLE_DOLPHIN_RUNTIME
       "Build the RecompCore/Dolphin production chassis" ON)
option(MODERNGEKKO_ENABLE_DOLPHIN_TESTS
       "Build the imported Dolphin unit-test targets" OFF)
set(MODERNGEKKO_FRONTEND_SDK "" CACHE PATH
    "Optional non-root sysroot containing X11 and audio development packages")

if(MODERNGEKKO_FRONTEND_SDK)
    set(_moderngekko_sdk_usr "${MODERNGEKKO_FRONTEND_SDK}/usr")
    set(_moderngekko_sdk_lib "${_moderngekko_sdk_usr}/lib/${CMAKE_LIBRARY_ARCHITECTURE}")
    if(NOT IS_DIRECTORY "${_moderngekko_sdk_usr}/include" OR
       NOT IS_DIRECTORY "${_moderngekko_sdk_lib}")
        message(FATAL_ERROR
            "MODERNGEKKO_FRONTEND_SDK is not a usable sysroot: ${MODERNGEKKO_FRONTEND_SDK}")
    endif()

    list(PREPEND CMAKE_PREFIX_PATH "${_moderngekko_sdk_usr}")
    list(PREPEND CMAKE_INCLUDE_PATH "${_moderngekko_sdk_usr}/include")
    list(PREPEND CMAKE_LIBRARY_PATH "${_moderngekko_sdk_lib}")
    link_directories("${_moderngekko_sdk_lib}")

    set(_moderngekko_pkgconfig
        "${_moderngekko_sdk_lib}/pkgconfig:${_moderngekko_sdk_usr}/share/pkgconfig")
    if(DEFINED ENV{PKG_CONFIG_PATH} AND NOT "$ENV{PKG_CONFIG_PATH}" STREQUAL "")
        string(APPEND _moderngekko_pkgconfig ":$ENV{PKG_CONFIG_PATH}")
    endif()
    set(ENV{PKG_CONFIG_PATH} "${_moderngekko_pkgconfig}")
    set(ENV{PKG_CONFIG_SYSROOT_DIR} "${MODERNGEKKO_FRONTEND_SDK}")

    # FindALSA does not use pkg-config on all supported CMake releases.
    if(EXISTS "${_moderngekko_sdk_lib}/libasound.so")
        set(ALSA_INCLUDE_DIR "${_moderngekko_sdk_usr}/include" CACHE PATH "" FORCE)
        set(ALSA_LIBRARY "${_moderngekko_sdk_lib}/libasound.so" CACHE FILEPATH "" FORCE)
    endif()
endif()

if(MODERNGEKKO_ENABLE_DOLPHIN_RUNTIME)
    set(MODERNGEKKO_ENABLE_IMGUI_LAUNCHER ON CACHE BOOL "" FORCE)
    set(ENABLE_QT OFF CACHE BOOL "" FORCE)
    set(ENABLE_NOGUI OFF CACHE BOOL "" FORCE)
    set(ENABLE_CLI_TOOL OFF CACHE BOOL "" FORCE)
    set(ENABLE_TESTS ${MODERNGEKKO_ENABLE_DOLPHIN_TESTS} CACHE BOOL "" FORCE)
    set(ENABLE_AUTOUPDATE OFF CACHE BOOL "" FORCE)
    set(ENABLE_ANALYTICS OFF CACHE BOOL "" FORCE)
    set(USE_RETRO_ACHIEVEMENTS OFF CACHE BOOL "" FORCE)
    set(USE_DISCORD_PRESENCE OFF CACHE BOOL "" FORCE)
    set(USE_MGBA OFF CACHE BOOL "" FORCE)
    set(USE_UPNP OFF CACHE BOOL "" FORCE)
    set(ENCODE_FRAMEDUMPS OFF CACHE BOOL "" FORCE)
    set(ENABLE_LLVM OFF CACHE BOOL "" FORCE)
    # Cubeb is Dolphin's only audio backend on macOS
    if(APPLE)
        set(ENABLE_CUBEB ON CACHE BOOL "" FORCE)
    else()
        set(ENABLE_CUBEB OFF CACHE BOOL "" FORCE)
    endif()
    set(ENABLE_CUBEB OFF CACHE BOOL "" FORCE)
    set(ENABLE_HWDB OFF CACHE BOOL "" FORCE)
    set(ENABLE_EVDEV OFF CACHE BOOL "" FORCE)
    set(ENABLE_WAYLAND ON CACHE BOOL "" FORCE)
    set(ENABLE_EGL OFF CACHE BOOL "" FORCE)
    set(USE_SYSTEM_SDL3 OFF CACHE BOOL "" FORCE)
    # SDL bundles its own copy of the macOS hidapi backend
    if(APPLE)
        set(SDL_HIDAPI OFF CACHE BOOL "" FORCE)
    endif()
    set(LINUX_LOCAL_DEV ON CACHE BOOL "" FORCE)
    set(DISTRIBUTOR "ModernGekko" CACHE STRING "" FORCE)
    add_subdirectory(vendor/dolphin EXCLUDE_FROM_ALL)
    if(MODERNGEKKO_FRONTEND_SDK)
        # These two imported targets historically rely on X11 headers living
        # in the compiler's default /usr/include search path.
        target_include_directories(common SYSTEM PRIVATE "${_moderngekko_sdk_usr}/include")
        if(TARGET videovulkan)
            target_include_directories(videovulkan SYSTEM PRIVATE
                "${_moderngekko_sdk_usr}/include")
        endif()
    endif()
else()
    set(FMT_INSTALL OFF CACHE BOOL "" FORCE)
    set(FMT_TEST OFF CACHE BOOL "" FORCE)
    add_subdirectory(vendor/fmt/fmt EXCLUDE_FROM_ALL)
endif()

if(BUILD_TESTING)
    add_subdirectory(vendor/dolphin/DolRecomp)
else()
    add_subdirectory(vendor/dolphin/DolRecomp EXCLUDE_FROM_ALL)
endif()

add_library(moderngekko_abi INTERFACE)
add_library(ModernGekko::ABI ALIAS moderngekko_abi)

target_include_directories(moderngekko_abi
    INTERFACE
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:include>"
)

add_library(moderngekko_legacy STATIC
    src/audio/audio_system.cpp
    src/hardware/mmio_bus.cpp
    src/hardware/disc_interface.cpp
    src/hardware/processor_interface.cpp
    src/gpu/gx_command_processor.cpp
    src/gpu/gx_state_backend.cpp
    src/gpu/gx_vertex_loader.cpp
    src/gpu/gx_texture_decoder.cpp
    src/gpu/gx_pipeline.cpp
    src/input/controller.cpp
    src/memory/address_space.cpp
    src/runtime/module.cpp
    src/runtime/module_loader.cpp
    src/runtime/game.cpp
    src/runtime/legacy_runtime.cpp
    src/timing/event_scheduler.cpp
    vendor/dolphin_legacy/Core/Boot/BootMemory.cpp
    vendor/dolphin_legacy/Core/Boot/DolReader.cpp
    vendor/dolphin_legacy/Core/PowerPC/Interpreter/IntegerBranch.cpp
    vendor/dolphin_legacy/Core/PowerPC/ConditionRegister.cpp
)
add_library(ModernGekko::LegacyRuntime ALIAS moderngekko_legacy)

add_library(moderngekko STATIC
    src/runtime/module.cpp
    src/runtime/module_loader.cpp
    src/runtime/game.cpp
)
add_library(ModernGekko::Runtime ALIAS moderngekko)

add_library(moderngekko_dolphin_video STATIC
    src/gpu/dolphin_video_host.cpp
    src/gpu/dolphin_shader_compiler.cpp
    vendor/dolphin_legacy/VideoCommon/BPMemory.cpp
    vendor/dolphin_legacy/VideoCommon/XFMemory.cpp
    vendor/dolphin_legacy/VideoCommon/ShaderGenCommon.cpp
    vendor/dolphin_legacy/VideoCommon/LightingShaderGen.cpp
    vendor/dolphin_legacy/VideoCommon/PixelShaderGen.cpp
    vendor/dolphin_legacy/VideoCommon/VertexShaderGen.cpp
    vendor/dolphin_legacy/VideoCommon/GeometryShaderGen.cpp
    vendor/dolphin_legacy/VideoCommon/DriverDetails.cpp
    vendor/dolphin_legacy/VideoCommon/UberShaderCommon.cpp
    vendor/dolphin_legacy/VideoCommon/UberShaderPixel.cpp
    vendor/dolphin_legacy/VideoCommon/UberShaderVertex.cpp
)
if(APPLE)
    target_sources(moderngekko_dolphin_video PRIVATE
        vendor/dolphin_legacy/Common/CommonFuncsObjC.mm)
    target_link_libraries(moderngekko_dolphin_video PUBLIC "-framework Foundation")
endif()
add_library(ModernGekko::DolphinVideo ALIAS moderngekko_dolphin_video)
set_target_properties(moderngekko_dolphin_video PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)
target_compile_features(moderngekko_dolphin_video PUBLIC cxx_std_23)
if(MSVC)
    target_compile_options(moderngekko_dolphin_video PUBLIC /Zc:preprocessor)
    target_compile_definitions(moderngekko_dolphin_video PUBLIC UNICODE _UNICODE)
endif()
target_include_directories(moderngekko_dolphin_video
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
           "${CMAKE_CURRENT_SOURCE_DIR}/vendor/dolphin_legacy"
           "${CMAKE_CURRENT_SOURCE_DIR}/vendor/picojson"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/dolphin_shims"
)
target_link_libraries(moderngekko_dolphin_video PUBLIC fmt::fmt)

target_link_libraries(moderngekko_legacy PUBLIC ModernGekko::ABI fmt::fmt)
target_compile_features(moderngekko_legacy PUBLIC cxx_std_23)
target_include_directories(moderngekko_legacy PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/vendor/dolphin_legacy")

target_link_libraries(moderngekko PUBLIC ModernGekko::ABI PRIVATE fmt::fmt)
if(MODERNGEKKO_ENABLE_DOLPHIN_RUNTIME)
    target_sources(moderngekko PRIVATE
        src/runtime/dolphin_runtime.cpp
        vendor/dolphin/Source/Core/DolphinNoGUI/Platform.cpp
        vendor/dolphin/Source/Core/DolphinNoGUI/PlatformHeadless.cpp
    )
    if(APPLE)
        # Dolphin's windowed backend for macOS
        target_sources(moderngekko PRIVATE
            vendor/dolphin/Source/Core/DolphinNoGUI/PlatformMacos.mm)
        set_source_files_properties(
            vendor/dolphin/Source/Core/DolphinNoGUI/PlatformMacos.mm
            PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
        target_link_libraries(moderngekko PRIVATE
            "${APPKIT_LIBRARY}" "${COREFOUNDATION_LIBRARY}" "${IOK_LIBRARY}")
        target_compile_definitions(moderngekko PRIVATE MODERNGEKKO_HAVE_COCOA=1)
    endif()
    target_include_directories(moderngekko PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/vendor/dolphin/GXRuntime/include")
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        target_compile_definitions(moderngekko PRIVATE _ARCH_64=1)
    else()
        target_compile_definitions(moderngekko PRIVATE _ARCH_32=1)
    endif()
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "x64|x86_64|amd64|AMD64")
        target_compile_definitions(moderngekko PRIVATE _M_X86_64=1)
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|ARM64")
        target_compile_definitions(moderngekko PRIVATE _M_ARM_64=1)
    else()
        target_compile_definitions(moderngekko PRIVATE _M_GENERIC=1)
    endif()
    target_link_libraries(moderngekko PRIVATE core uicommon)
    if(TARGET PkgConfig::X11)
        target_sources(moderngekko PRIVATE
            vendor/dolphin/Source/Core/DolphinNoGUI/PlatformX11.cpp)
        target_link_libraries(moderngekko PRIVATE PkgConfig::X11)
        if(TARGET PkgConfig::XRANDR)
            target_link_libraries(moderngekko PRIVATE PkgConfig::XRANDR)
            target_compile_definitions(moderngekko PRIVATE HAVE_XRANDR=1)
        endif()
        target_compile_definitions(moderngekko PRIVATE HAVE_X11=1)
    endif()
    if(TARGET PkgConfig::WAYLAND)
        find_program(MODERNGEKKO_WAYLAND_SCANNER wayland-scanner
            HINTS "${_moderngekko_sdk_usr}/bin" REQUIRED)
        find_file(MODERNGEKKO_XDG_SHELL_XML xdg-shell.xml
            HINTS "${_moderngekko_sdk_usr}/share/wayland-protocols/stable/xdg-shell"
                  "/usr/share/wayland-protocols/stable/xdg-shell"
            REQUIRED)
        find_file(MODERNGEKKO_XDG_DECORATION_XML xdg-decoration-unstable-v1.xml
            HINTS "${_moderngekko_sdk_usr}/share/wayland-protocols/unstable/xdg-decoration"
                  "/usr/share/wayland-protocols/unstable/xdg-decoration"
            REQUIRED)
        set(_moderngekko_wayland_generated
            "${CMAKE_CURRENT_BINARY_DIR}/generated/wayland")
        set(_moderngekko_xdg_header
            "${_moderngekko_wayland_generated}/xdg-shell-client-protocol.h")
        set(_moderngekko_xdg_code
            "${_moderngekko_wayland_generated}/xdg-shell-protocol.c")
        set(_moderngekko_decoration_header
            "${_moderngekko_wayland_generated}/xdg-decoration-client-protocol.h")
        set(_moderngekko_decoration_code
            "${_moderngekko_wayland_generated}/xdg-decoration-protocol.c")
        add_custom_command(
            OUTPUT "${_moderngekko_xdg_header}" "${_moderngekko_xdg_code}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory
                    "${_moderngekko_wayland_generated}"
            COMMAND "${MODERNGEKKO_WAYLAND_SCANNER}" client-header
                    "${MODERNGEKKO_XDG_SHELL_XML}" "${_moderngekko_xdg_header}"
            COMMAND "${MODERNGEKKO_WAYLAND_SCANNER}" private-code
                    "${MODERNGEKKO_XDG_SHELL_XML}" "${_moderngekko_xdg_code}"
            DEPENDS "${MODERNGEKKO_XDG_SHELL_XML}"
            VERBATIM)
        add_custom_command(
            OUTPUT "${_moderngekko_decoration_header}" "${_moderngekko_decoration_code}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory
                    "${_moderngekko_wayland_generated}"
            COMMAND "${MODERNGEKKO_WAYLAND_SCANNER}" client-header
                    "${MODERNGEKKO_XDG_DECORATION_XML}" "${_moderngekko_decoration_header}"
            COMMAND "${MODERNGEKKO_WAYLAND_SCANNER}" private-code
                    "${MODERNGEKKO_XDG_DECORATION_XML}" "${_moderngekko_decoration_code}"
            DEPENDS "${MODERNGEKKO_XDG_DECORATION_XML}"
            VERBATIM)
        target_sources(moderngekko PRIVATE
            vendor/dolphin/Source/Core/DolphinNoGUI/PlatformWayland.cpp
            "${_moderngekko_xdg_code}"
            "${_moderngekko_decoration_code}")
        target_include_directories(moderngekko PRIVATE
            "${_moderngekko_wayland_generated}")
        if(MODERNGEKKO_FRONTEND_SDK)
            target_link_libraries(moderngekko PRIVATE
                "${_moderngekko_sdk_lib}/libwayland-client.so")
        else()
            target_link_libraries(moderngekko PRIVATE PkgConfig::WAYLAND)
        endif()
        target_compile_definitions(moderngekko PRIVATE HAVE_WAYLAND=1)
        target_compile_definitions(videovulkan PRIVATE HAVE_WAYLAND=1)
        if(MODERNGEKKO_FRONTEND_SDK)
            target_link_directories(moderngekko PRIVATE "${_moderngekko_sdk_lib}")
        endif()
    endif()
endif()
if(MODERNGEKKO_ENABLE_DYNAMIC_MODULES)
    target_sources(moderngekko_legacy PRIVATE vendor/dolphin_legacy/Common/DynamicLibrary.cpp)
    target_link_libraries(moderngekko_legacy PRIVATE ${CMAKE_DL_LIBS})
    target_compile_definitions(moderngekko_legacy PRIVATE MODERNGEKKO_ENABLE_DYNAMIC_MODULES=1)
    if(NOT MODERNGEKKO_ENABLE_DOLPHIN_RUNTIME)
        target_sources(moderngekko PRIVATE vendor/dolphin_legacy/Common/DynamicLibrary.cpp)
        target_link_libraries(moderngekko PRIVATE ${CMAKE_DL_LIBS})
    endif()
    target_compile_definitions(moderngekko PRIVATE MODERNGEKKO_ENABLE_DYNAMIC_MODULES=1)
endif()
target_compile_features(moderngekko PUBLIC cxx_std_23)
if(MODERNGEKKO_ENABLE_DOLPHIN_RUNTIME)
    target_include_directories(moderngekko PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/vendor/dolphin/Source/Core")
else()
    target_include_directories(moderngekko PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/vendor/dolphin_legacy")
endif()

add_executable(moderngekko-module-info
    tools/module_info.cpp
)
target_link_libraries(moderngekko-module-info
    PRIVATE ModernGekko::Runtime
)

if(MODERNGEKKO_ENABLE_DOLPHIN_RUNTIME)
    add_executable(moderngekko-run tools/moderngekko_run.cpp)
    target_sources(moderngekko-run PRIVATE tools/frontend_config.cpp)
    target_include_directories(moderngekko-run PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/tools")
    if(MODERNGEKKO_FRONTEND_SDK)
        set_property(TARGET moderngekko-run PROPERTY BUILD_RPATH
            "${_moderngekko_sdk_lib}")
    endif()
    target_link_libraries(moderngekko-run PRIVATE ModernGekko::Runtime)
    add_custom_command(TARGET moderngekko-run POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/vendor/dolphin/Data/Sys"
                "$<TARGET_FILE_DIR:moderngekko-run>/Sys")

    add_executable(moderngekko-launcher
        tools/moderngekko_launcher.cpp
        tools/frontend_config.cpp
        vendor/dolphin/Externals/imgui/imgui/backends/imgui_impl_sdl3.cpp
        vendor/dolphin/Externals/imgui/imgui/backends/imgui_impl_sdlrenderer3.cpp)
    target_include_directories(moderngekko-launcher PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/tools"
        "${CMAKE_CURRENT_SOURCE_DIR}/vendor/dolphin/Source/Core"
        "${CMAKE_CURRENT_SOURCE_DIR}/vendor/dolphin/Externals/imgui/imgui/backends")
    if(TARGET SDL3-static)
        set(_moderngekko_sdl_target SDL3-static)
    elseif(TARGET SDL3::SDL3-shared)
        set(_moderngekko_sdl_target SDL3::SDL3-shared)
    elseif(TARGET SDL3::SDL3)
        set(_moderngekko_sdl_target SDL3::SDL3)
    else()
        message(FATAL_ERROR "No usable SDL3 target is available for the launcher")
    endif()
    # The launcher only calls the game-inspection surface directly, but the
    # Dolphin static libraries still require the Host_* callbacks implemented
    # in the runtime object. Keep that object from being discarded by the
    # static linker.
    if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.24)
        target_link_libraries(moderngekko-launcher PRIVATE
            "$<LINK_LIBRARY:WHOLE_ARCHIVE,moderngekko>"
            discio imgui ${_moderngekko_sdl_target})
    elseif(UNIX AND NOT APPLE)
        target_link_libraries(moderngekko-launcher PRIVATE
            "-Wl,--whole-archive" moderngekko "-Wl,--no-whole-archive"
            discio imgui ${_moderngekko_sdl_target})
    else()
        target_link_libraries(moderngekko-launcher PRIVATE
            ModernGekko::Runtime discio imgui ${_moderngekko_sdl_target})
    endif()
    set_target_properties(moderngekko-launcher PROPERTIES OUTPUT_NAME "ModernGekko")
    if(MODERNGEKKO_FRONTEND_SDK)
        set_property(TARGET moderngekko-launcher PROPERTY BUILD_RPATH
            "${_moderngekko_sdk_lib}")
    endif()
    if(UNIX AND NOT APPLE)
        set_target_properties(moderngekko-run moderngekko-launcher PROPERTIES
            INSTALL_RPATH "$ORIGIN/lib")
    endif()
    add_dependencies(moderngekko-launcher moderngekko-run)

    add_executable(moderngekko-port tools/moderngekko_port.cpp)
    target_link_libraries(moderngekko-port PRIVATE ModernGekko::Runtime)
    target_compile_definitions(moderngekko-port PRIVATE
        MODERNGEKKO_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_dependencies(moderngekko-port dolrecomp moderngekko-run)
    add_custom_command(TARGET moderngekko-port POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:dolrecomp>"
                "$<TARGET_FILE_DIR:moderngekko-port>")
endif()

if(BUILD_TESTING)
    add_executable(moderngekko_game_inspect_test tests/game_inspect_test.cpp)
    target_link_libraries(moderngekko_game_inspect_test PRIVATE ModernGekko::LegacyRuntime)
    add_test(NAME moderngekko.game_inspect COMMAND moderngekko_game_inspect_test)

    add_executable(moderngekko_module_abi_test
        tests/module_abi_test.c
    )
    target_link_libraries(moderngekko_module_abi_test
        PRIVATE ModernGekko::LegacyRuntime
    )
    add_test(NAME moderngekko.module_abi
             COMMAND moderngekko_module_abi_test)

    add_library(moderngekko_test_module SHARED
        tests/fixtures/test_module.c
    )
    target_link_libraries(moderngekko_test_module
        PRIVATE ModernGekko::ABI
    )
    set_target_properties(moderngekko_test_module PROPERTIES
        PREFIX ""
        OUTPUT_NAME "moderngekko_test_module"
    )
    add_library(moderngekko_missing_export_module SHARED
        tests/fixtures/missing_export.c)
    set_target_properties(moderngekko_missing_export_module PROPERTIES
        PREFIX "" OUTPUT_NAME "moderngekko_missing_export_module")

    add_executable(moderngekko_module_loader_test
        tests/module_loader_test.cpp
    )
    target_link_libraries(moderngekko_module_loader_test
        PRIVATE ModernGekko::LegacyRuntime
    )
    target_compile_definitions(moderngekko_module_loader_test
        PRIVATE
            MODERNGEKKO_TEST_MODULE_PATH="$<TARGET_FILE:moderngekko_test_module>"
            MODERNGEKKO_MISSING_EXPORT_MODULE_PATH="$<TARGET_FILE:moderngekko_missing_export_module>"
    )
    add_dependencies(moderngekko_module_loader_test moderngekko_test_module
                     moderngekko_missing_export_module)
    add_test(NAME moderngekko.module_loader
             COMMAND moderngekko_module_loader_test)

    add_executable(moderngekko_cpu_primitives_test
        tests/cpu_primitives_test.cpp
    )
    target_link_libraries(moderngekko_cpu_primitives_test
        PRIVATE ModernGekko::LegacyRuntime
    )
    target_include_directories(moderngekko_cpu_primitives_test
        PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/vendor/dolphin_legacy"
    )
    add_test(NAME moderngekko.cpu_primitives
             COMMAND moderngekko_cpu_primitives_test)

    add_executable(moderngekko_address_space_test
        tests/address_space_test.cpp
    )
    target_link_libraries(moderngekko_address_space_test
        PRIVATE ModernGekko::LegacyRuntime
    )
    add_test(NAME moderngekko.address_space
             COMMAND moderngekko_address_space_test)

    add_library(moderngekko_runtime_test_module SHARED
        tests/fixtures/runtime_test_module.c
    )
    target_link_libraries(moderngekko_runtime_test_module
        PRIVATE ModernGekko::ABI
    )
    set_target_properties(moderngekko_runtime_test_module PROPERTIES
        PREFIX ""
        OUTPUT_NAME "moderngekko_runtime_test_module"
    )

    add_executable(moderngekko_runtime_test
        tests/runtime_test.cpp
    )
    target_link_libraries(moderngekko_runtime_test
        PRIVATE ModernGekko::LegacyRuntime
    )
    target_compile_definitions(moderngekko_runtime_test
        PRIVATE
            MODERNGEKKO_RUNTIME_TEST_MODULE_PATH="$<TARGET_FILE:moderngekko_runtime_test_module>"
    )
    add_dependencies(moderngekko_runtime_test moderngekko_runtime_test_module)
    add_test(NAME moderngekko.runtime
             COMMAND moderngekko_runtime_test)

    add_executable(moderngekko_dol_loader_test
        tests/dol_loader_test.cpp
    )
    target_link_libraries(moderngekko_dol_loader_test
        PRIVATE ModernGekko::LegacyRuntime
    )
    add_test(NAME moderngekko.dol_loader
             COMMAND moderngekko_dol_loader_test)

    add_executable(moderngekko_boot_environment_test
        tests/boot_environment_test.cpp
    )
    target_link_libraries(moderngekko_boot_environment_test
        PRIVATE ModernGekko::LegacyRuntime
    )
    add_test(NAME moderngekko.boot_environment
             COMMAND moderngekko_boot_environment_test)

    add_executable(moderngekko_gekko_definitions_test
        tests/gekko_definitions_test.cpp
    )
    target_link_libraries(moderngekko_gekko_definitions_test
        PRIVATE ModernGekko::LegacyRuntime
    )
    target_include_directories(moderngekko_gekko_definitions_test
        PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/vendor/dolphin_legacy"
    )
    add_test(NAME moderngekko.gekko_definitions
             COMMAND moderngekko_gekko_definitions_test)

    add_executable(moderngekko_interpreter_test
        tests/interpreter_test.cpp
    )
    target_link_libraries(moderngekko_interpreter_test
        PRIVATE ModernGekko::LegacyRuntime
    )
    add_test(NAME moderngekko.interpreter
             COMMAND moderngekko_interpreter_test)

    add_executable(moderngekko_hardware_core_test
        tests/hardware_core_test.cpp
    )
    target_link_libraries(moderngekko_hardware_core_test
        PRIVATE ModernGekko::LegacyRuntime
    )
    add_test(NAME moderngekko.hardware_core
             COMMAND moderngekko_hardware_core_test)

    add_executable(moderngekko_io_services_test
        tests/io_services_test.cpp
    )
    target_link_libraries(moderngekko_io_services_test
        PRIVATE ModernGekko::LegacyRuntime
    )
    add_test(NAME moderngekko.io_services
             COMMAND moderngekko_io_services_test)

    add_executable(moderngekko_audio_system_test
        tests/audio_system_test.cpp
    )
    target_link_libraries(moderngekko_audio_system_test
        PRIVATE ModernGekko::LegacyRuntime
    )
    add_test(NAME moderngekko.audio_system
             COMMAND moderngekko_audio_system_test)

    add_executable(moderngekko_gx_commands_test
        tests/gx_commands_test.cpp
    )
    target_link_libraries(moderngekko_gx_commands_test
        PRIVATE ModernGekko::LegacyRuntime
    )
    add_test(NAME moderngekko.gx_commands
             COMMAND moderngekko_gx_commands_test)

    add_executable(moderngekko_gx_state_test
        tests/gx_state_test.cpp
    )
    target_link_libraries(moderngekko_gx_state_test
        PRIVATE ModernGekko::LegacyRuntime
    )
    add_test(NAME moderngekko.gx_state
             COMMAND moderngekko_gx_state_test)

    add_executable(moderngekko_gx_vertex_test
        tests/gx_vertex_test.cpp
    )
    target_link_libraries(moderngekko_gx_vertex_test
        PRIVATE ModernGekko::LegacyRuntime
    )
    add_test(NAME moderngekko.gx_vertex
             COMMAND moderngekko_gx_vertex_test)

    add_executable(moderngekko_gx_conformance_test
        tests/gx_conformance_test.cpp
    )
    target_link_libraries(moderngekko_gx_conformance_test
        PRIVATE ModernGekko::LegacyRuntime
    )
    add_test(NAME moderngekko.gx_conformance
             COMMAND moderngekko_gx_conformance_test)

    add_executable(moderngekko_gx_texture_test
        tests/gx_texture_test.cpp
    )
    target_link_libraries(moderngekko_gx_texture_test
        PRIVATE ModernGekko::LegacyRuntime
    )
    add_test(NAME moderngekko.gx_texture
             COMMAND moderngekko_gx_texture_test)

    add_executable(moderngekko_gx_pipeline_test
        tests/gx_pipeline_test.cpp
    )
    target_link_libraries(moderngekko_gx_pipeline_test
        PRIVATE ModernGekko::LegacyRuntime
    )
    add_test(NAME moderngekko.gx_pipeline
             COMMAND moderngekko_gx_pipeline_test)

    add_executable(moderngekko_dolphin_shader_test
        tests/dolphin_shader_test.cpp
    )
    target_link_libraries(moderngekko_dolphin_shader_test
        PRIVATE ModernGekko::DolphinVideo
    )
    add_test(NAME moderngekko.dolphin_shader
             COMMAND moderngekko_dolphin_shader_test)
endif()
