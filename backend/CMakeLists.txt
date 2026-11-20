cmake_minimum_required(VERSION 3.20)
project(vgm_engine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)

include(FetchContent)

FetchContent_Declare(
    libvgm
    GIT_REPOSITORY https://github.com/ValleyBell/libvgm.git
    GIT_TAG master
)

FetchContent_MakeAvailable(libvgm)

add_library(vgm_engine SHARED
    src/vgm_engine.cpp
    src/libvgm_bridge.cpp
)

target_include_directories(vgm_engine
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${libvgm_SOURCE_DIR}
)

target_link_libraries(vgm_engine PRIVATE libvgm)

target_compile_definitions(vgm_engine PRIVATE VGM_ENGINE_EXPORTS)
