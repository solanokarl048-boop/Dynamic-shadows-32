# Matches the real AML/aml-psdk mod layout: the SDK as a submodule at
# ./aml-psdk (hyphen -- matches its own internal #include <aml-psdk/...>
# paths), plus a ./mod/ folder holding AML's own loader headers
# (amlmod.h, iaml.h, interface.h -- typically obtained from AML's
# "template_of_mod" starter project) alongside this project's own
# mod/IniConfig.h helper.
#
# Both aml-psdk/ and mod/ are referenced from main.cpp as
# #include <aml-psdk/game_sa/...> and #include <mod/amlmod.h>, so the
# only include path needed is the project root itself.

LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := AML_PSDK_ShadowExtender

LOCAL_SRC_FILES := main.cpp

LOCAL_C_INCLUDES := $(LOCAL_PATH)

LOCAL_CPPFLAGS := -std=c++17 -Os -fno-exceptions -fno-rtti -DANDROID
LOCAL_LDLIBS := -llog

include $(BUILD_SHARED_LIBRARY)

LOCAL_C_INCLUDES += $(LOCAL_PATH)/aml-psdk

