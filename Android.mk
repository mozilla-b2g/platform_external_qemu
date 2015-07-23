# the following test is made to detect that we were called
# through the 'm' or 'mm' build commands. if not, we use the
# standard QEMU Makefile
#
LOCAL_PATH:= $(call my-dir)
#include $(LOCAL_PATH)/Makefile.android

#
# Build standalone emulator
#
include $(CLEAR_VARS)
EMULATOR_PATH := $(abspath $(LOCAL_PATH))
EMULATOR_OUT ?= $(EMULATOR_PATH)/objs

LOCAL_IS_HOST_MODULE := true
LOCAL_MODULE := emulator-standalone
LOCAL_MODULE_CLASS := DATA
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_PATH := $(HOST_OUT)
include $(BUILD_PREBUILT)

$(LOCAL_INSTALLED_MODULE): $(LOCAL_BUILT_MODULE)
	@echo Install dir: $(HOST_OUT); \
	tar -xvz -f $(abspath $<) -C $(HOST_OUT)

EMULATOR_STANDALONE_ARCHIVE := $(EMULATOR_OUT)/emulator-standalone.tar.gz

.PHONY: $(LOCAL_BUILT_MODULE)
$(LOCAL_BUILT_MODULE):
	$(EMULATOR_PATH)/android-configure.sh --out-dir=$(EMULATOR_OUT) --verbose --no-tests && \
	$(MAKE) -C $(EMULATOR_PATH) && \
	rm -f $(EMULATOR_STANDALONE_ARCHIVE) && \
	tar -cvzP -f $(EMULATOR_STANDALONE_ARCHIVE) -C $(EMULATOR_OUT) --transform='s,^$(EMULATOR_OUT)/emulator,bin/emulator,' --show-transformed-names $(EMULATOR_OUT)/emulator* lib lib64 && \
	mkdir -p $(@D) && cp $(EMULATOR_STANDALONE_ARCHIVE) $@
