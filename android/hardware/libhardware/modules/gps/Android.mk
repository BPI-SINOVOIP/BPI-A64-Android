# Copyright (C) 2011 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE_PATH := $(TARGET_OUT_SHARED_LIBRARIES)/hw
LOCAL_CFLAGS += -Wno-unused-parameter
ifneq ($(USE_NDK),)
LOCAL_CFLAGS += -DUSE_NDK -Wall
LOCAL_LDLIBS += -llog
else
LOCAL_SHARED_LIBRARIES := liblog libcutils
endif
LOCAL_SRC_FILES := gps_ql.c \
	ql-log.c \
	config.c \
	extract.c \
	gps_i2c.c

LOCAL_MODULE_TAGS := optional

LOCAL_MODULE := gps.default
include $(BUILD_SHARED_LIBRARY)

ifeq (1,0)
include $(CLEAR_VARS)
LOCAL_SHARED_LIBRARIES := liblog libcutils
LOCAL_SRC_FILES := gps_ql.c \
	ql-log.c \
	config.c
LOCAL_CFLAGS += -DMAIN_TEST
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE := gps_ql
include $(BUILD_EXECUTABLE)
endif
