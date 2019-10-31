##
# Makefile for gmp
##

# Project info
Project = gmp
Version	= 4.2.3

PROJECT_DIR=$(Project)-$(Version)
PROJECT_PATH = $(THIRD_PARTY_PATH)/$(Project)/$(PROJECT_DIR/$(PROJECT_DIR)
BUILD_DIR = $(Project)-$(Version)
BUILD_PATH = $(BUILDROOT)/$(BUILD_DIR)
ABI = $(TARGET_ABI)

all:
	rm -rf $(BUILD_PATH)
	mkdir -p $(BUILD_PATH)
	cd $(BUILD_PATH) && ABI=$(ABI) $(PROJECT_PATH)/configure --prefix=$(INSTALL_PATH)
	cd $(BUILD_PATH) && make
	cd $(BUILD_PATH) && make install
