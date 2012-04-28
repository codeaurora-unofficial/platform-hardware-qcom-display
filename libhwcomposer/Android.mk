LOCAL_PATH := $(call my-dir)

# HAL module implemenation, not prelinked and stored in
# hw/<OVERLAY_HARDWARE_MODULE_ID>.<ro.product.board>.so
include $(CLEAR_VARS)
LOCAL_PRELINK_MODULE := false
LOCAL_MODULE_PATH := $(TARGET_OUT_SHARED_LIBRARIES)/hw
LOCAL_SHARED_LIBRARIES := liblog libcutils libEGL libhardware libutils liboverlay
LOCAL_SHARED_LIBRARIES += libgenlock libQcomUI libmemalloc
ifeq ($(TARGET_BOARD_PLATFORM),copper)
LOCAL_SHARED_LIBRARIES += liboverlay2
endif

LOCAL_SRC_FILES := 	\
    hwcomposer.cpp \
    external_display_only.h

LOCAL_MODULE := hwcomposer.$(TARGET_BOARD_PLATFORM)
LOCAL_CFLAGS:= -DLOG_TAG=\"$(TARGET_BOARD_PLATFORM).hwcomposer\" -DDEBUG_CALC_FPS

LOCAL_C_INCLUDES += hardware/qcom/display/libgralloc
ifeq ($(TARGET_BOARD_PLATFORM),copper)
LOCAL_C_INCLUDES += hardware/qcom/display/badger/liboverlay2
LOCAL_C_INCLUDES += hardware/qcom/display/badger/liboverlay2/src
endif
LOCAL_C_INCLUDES += hardware/qcom/display/liboverlay
LOCAL_C_INCLUDES += hardware/qcom/display/libcopybit
LOCAL_C_INCLUDES += hardware/qcom/display/libgenlock
LOCAL_C_INCLUDES += hardware/qcom/display/libqcomui
LOCAL_C_INCLUDES += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/include

LOCAL_ADDITIONAL_DEPENDENCIES += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr
ifeq ($(TARGET_HAVE_HDMI_OUT),true)
LOCAL_CFLAGS += -DHDMI_DUAL_DISPLAY
endif
ifeq ($(TARGET_USES_OVERLAY),true)
LOCAL_CFLAGS += -DUSE_OVERLAY
ifeq ($(TARGET_BOARD_PLATFORM),copper)
LOCAL_CFLAGS += -DUSE_OVERLAY2
endif
endif
ifeq ($(TARGET_HAVE_BYPASS),true)
LOCAL_CFLAGS += -DCOMPOSITION_BYPASS
endif
ifeq ($(TARGET_USE_HDMI_AS_PRIMARY),true)
LOCAL_CFLAGS += -DHDMI_AS_PRIMARY
endif
ifeq ($(TARGET_USES_POST_PROCESSING),true)
LOCAL_CFLAGS += -DUSES_POST_PROCESSING
LOCAL_C_INCLUDES += $(TARGET_OUT_HEADERS)/pp/inc
endif
LOCAL_MODULE_TAGS := optional
include $(BUILD_SHARED_LIBRARY)
