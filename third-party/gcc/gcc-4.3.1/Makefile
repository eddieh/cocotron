##
# Makefile for gcc
##

# Project info
Project = gcc
Version	= 4.3.1

PROJECT_DIR = $(Project)-$(Version)
PROJECT_PATH = $(THIRD_PARTY_PATH)/$(Project)/$(PROJECT_DIR)/$(PROJECT_DIR)
BUILD_DIR = $(Project)-$(Version)
BUILD_PATH = $(BUILDROOT)/$(BUILD_DIR)

CONFIG_FLAGS := --prefix=$(INSTALL_PATH)
CONFIG_FLAGS += --target=$(COMPILER_TARGET)
CONFIG_FLAGS += --with-gnu-as
CONFIG_FLAGS += --with-gnu-ld
CONFIG_FLAGS += --with-headers=$(INSTALL_PATH)/$(COMPILER_TARGET)/include
CONFIG_FLAGS += --without-newlib
CONFIG_FLAGS += --disable-multilib
CONFIG_FLAGS += --disable-libssp
CONFIG_FLAGS += --disable-nls
CONFIG_FLAGS += --enable-languages="c,objc,c++,obj-c++"
CONFIG_FLAGS += --with-gmp=$(INSTALL_PATH)
CONFIG_FLAGS += --enable-decimal-float
CONFIG_FLAGS += --with-mpfr=$(INSTALL_PATH)
CONFIG_FLAGS += --enable-checking=release
CONFIG_FLAGS += --enable-objc-gc

ABI = $(TARGET_ABI)
CFLAGS = "-m${ABI}"

all:
	rm -rf $(BUILD_PATH)
	mkdir -p $(BUILD_PATH)
	cd $(BUILD_PATH) && $(PROJECT_PATH)/configure CC=$(CC) \
	  CFLAGS=$(CFLAGS) $(CONFIG_FLAGS)
	cd $(BUILD_PATH) && make
	cd $(BUILD_PATH) && make install
