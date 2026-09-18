# Alternative to CMakeLists.txt for loaders/toolchains that still expect
# classic ndk-build. Point PLUGIN_SDK_DIR at your plugin-sdk checkout.

LOCAL_PATH := $(call my-dir)
PLUGIN_SDK_DIR := $(LOCAL_PATH)/../plugin-sdk

include $(CLEAR_VARS)

LOCAL_MODULE := ShadowExtender
LOCAL_SRC_FILES := src/ShadowExtender.cpp

LOCAL_C_INCLUDES := \
    $(PLUGIN_SDK_DIR) \
    $(PLUGIN_SDK_DIR)/plugin_sa \
    $(PLUGIN_SDK_DIR)/shared \
    $(PLUGIN_SDK_DIR)/shared/game \
    $(LOCAL_PATH)/src

LOCAL_CPPFLAGS := -std=c++17 -Os -fno-exceptions -fno-rtti -DGTASA -DANDROID
LOCAL_LDLIBS := -llog

include $(BUILD_SHARED_LIBRARY)

