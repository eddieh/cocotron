##
# Makefile for binutils
##

# Project info
Project = binutils
Version	= 2.21.1

PROJECT_DIR = $(Project)-$(Version)
PROJECT_PATH = $(THIRD_PARTY_PATH)/$(Project)/$(PROJECT_DIR)/$(PROJECT_DIR)
BUILD_DIR = $(Project)-$(Version)
BUILD_PATH = $(BUILDROOT)/$(BUILD_DIR)
ABI = $(TARGET_ABI)

CFLAGS = "-m${ABI} -Wformat=0 -Wno-error"

all:
	rm -rf $(BUILD_PATH)
	mkdir -p $(BUILD_PATH)
	cd $(BUILD_PATH) && CC=$(CC) CFLAGS=$(CFLAGS) $(PROJECT_PATH)/configure \
	    --prefix=$(INSTALL_PATH) --target=$(COMPILER_TARGET)
	cd $(BUILD_PATH) && make
	cd $(BUILD_PATH) && make install
