LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES:= \
	screencap.cpp

LOCAL_SHARED_LIBRARIES := \
	libcutils \
	libutils \
	libbinder \
	libskia \
    libui \
    libgui

LOCAL_MODULE:= screencap-gonk

LOCAL_MODULE_TAGS := optional eng

LOCAL_C_INCLUDES += \
	external/skia/include/core \
	external/skia/include/effects \
	external/skia/include/images \
	external/skia/src/ports \
	external/skia/include/utils \
	frameworks/base/include/surfaceflinger \
	frameworks/base/include/gui

ifneq (,$(wildcard external/skia/include/core/SkData.h))
LOCAL_CFLAGS += -DWITH_SKDATA
endif

include $(BUILD_EXECUTABLE)

$(LOCAL_INSTALLED_MODULE):
	cp $< $@
	ln -sf screencap-gonk $(TARGET_OUT_EXECUTABLES)/screencap
