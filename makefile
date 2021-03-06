# All Rights Reserved by Kedacom
# Author: Summer Shang
# Date  : Jun. 19 2018

GCC := gcc
GXX := g++

SRC_DIR := source
OUT_DIR := build

#$(error $(C_FILES))

LIBYUV_BASE := /root/tao/libyuv
KEDACOM_BASE := /root/tao/platformdaily

INC_DIR := $(LIBYUV_BASE)/include
INC_DIR += ./include 
INC_DIR += ./libyuv/include 
INC_DIR += $(KEDACOM_BASE)/10-common/include/system
INC_DIR += $(KEDACOM_BASE)/10-common/include/video1


LIB_DIR := $(LIBYUV_BASE)
LIB_DIR += $(KEDACOM_BASE)/10-common/lib/debug/linux_x64
LIB_DIR += $(KEDACOM_BASE)/10-common/lib/debug/linux_x64/media

CFLAGS  := $(foreach a, $(INC_DIR), -I$(a))
LDFLAGS := $(foreach a, $(LIB_DIR), -L$(a))

OBJS := mergertest.o \
	mcmerge.o

FINAL_OBJS := $(foreach obj, $(OBJS), $(OUT_DIR)/$(obj))

LIBS := yuv \
	imageunit_linux \
	imf \
	intlc \
	videomanage_linux \
	svml
LDFLAGS += $(foreach lib, $(LIBS), -l$(lib))

#CFLAGS += -DMERGE_KEDACOM
CFLAGS += -DMERGE_MCMERGE -DMCMERGE_DEBUG

#CFLAGS += -Wall -Wextra

TARGET := scaler

all:$(TARGET)

scaler:$(FINAL_OBJS)
	$(GXX) -o $@ $^ $(LDFLAGS)

$(OUT_DIR)/%.o:$(SRC_DIR)/%.cpp
	@test -d $(OUT_DIR) || mkdir -p $(OUT_DIR)
	$(GXX) -g -c $<  -o $@ $(CFLAGS) -fpermissive

$(OUT_DIR)/%.o:$(SRC_DIR)/%.c
	@test -d $(OUT_DIR) || mkdir -p $(OUT_DIR)
	$(GCC) -g -c $<  -o $@ $(CFLAGS) -fpermissive

.PHONY:clean
clean:
	rm -rf $(OUT_DIR)/*.o $(TARGET)
