cmake_minimum_required(VERSION 3.20)
project(ModernGekkoMod LANGUAGES C)

set(MODERNGEKKO_SOURCE_DIR "" CACHE PATH "ModernGekko source directory")
set(MOD_GAME_ID "" CACHE STRING "Target disc ID")
set(MOD_ID "example_mod" CACHE STRING "Mod ID")
set(MOD_VERSION "1.0.0" CACHE STRING "Mod version")
set(MOD_DISPLAY_NAME "Example Mod" CACHE STRING "Mod display name")
set(MOD_PATCH_ADDRESS "" CACHE STRING "Guest function address")
set(MOD_SYMBOL_HEADER "" CACHE FILEPATH "DolRecomp generated symbol header")

if(NOT IS_DIRECTORY "${MODERNGEKKO_SOURCE_DIR}/include/moderngekko")
    message(FATAL_ERROR "Set MODERNGEKKO_SOURCE_DIR to the ModernGekko checkout")
endif()
if(MOD_GAME_ID STREQUAL "")
    message(FATAL_ERROR "Set MOD_GAME_ID to the target disc ID")
endif()
if(MOD_PATCH_ADDRESS STREQUAL "")
    message(FATAL_ERROR "Set MOD_PATCH_ADDRESS to a guest function address")
endif()

add_library(mod SHARED mod.c)
target_include_directories(mod PRIVATE "${MODERNGEKKO_SOURCE_DIR}/include")
target_compile_definitions(mod PRIVATE
    MOD_GAME_ID="${MOD_GAME_ID}"
    MOD_ID="${MOD_ID}"
    MOD_VERSION="${MOD_VERSION}"
    MOD_DISPLAY_NAME="${MOD_DISPLAY_NAME}"
    MOD_PATCH_ADDRESS=${MOD_PATCH_ADDRESS}
)
if(NOT MOD_SYMBOL_HEADER STREQUAL "")
    if(MSVC)
        target_compile_options(mod PRIVATE "/FI${MOD_SYMBOL_HEADER}")
    else()
        target_compile_options(mod PRIVATE "-include${MOD_SYMBOL_HEADER}")
    endif()
endif()
set_target_properties(mod PROPERTIES
    PREFIX ""
    OUTPUT_NAME "mod"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${MOD_ID}.mgm"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${MOD_ID}.mgm"
)
