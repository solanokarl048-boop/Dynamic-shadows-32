cmake_minimum_required(VERSION 3.10)
project(ShadowExtender)

# Build against plugin-sdk for GTA:SA (Android/ARM target).
# Set PLUGIN_SDK_DIR to your local checkout of
# https://github.com/DK22Pac/plugin-sdk before configuring, e.g.:
#   cmake -DPLUGIN_SDK_DIR=/path/to/plugin-sdk -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake -DANDROID_ABI=armeabi-v7a ..
if(NOT DEFINED PLUGIN_SDK_DIR)
    message(FATAL_ERROR "Set -DPLUGIN_SDK_DIR=/path/to/plugin-sdk")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(
    ${PLUGIN_SDK_DIR}
    ${PLUGIN_SDK_DIR}/plugin_sa
    ${PLUGIN_SDK_DIR}/shared
    ${PLUGIN_SDK_DIR}/shared/game
    src
)

add_library(ShadowExtender SHARED
    src/ShadowExtender.cpp
)

target_compile_definitions(ShadowExtender PRIVATE
    GTASA         # tell plugin-sdk which game to target
    ANDROID
)

# Strip and optimize for release-size .so, common for mod-loader plugins.
target_compile_options(ShadowExtender PRIVATE -Os -fno-exceptions -fno-rtti)

set_target_properties(ShadowExtender PROPERTIES
    LIBRARY_OUTPUT_NAME "ShadowExtender"
)
