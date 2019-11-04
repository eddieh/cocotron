##
# Makefile for mpfr
##

# Project info
Project = mpfr
Version	= 2.3.2

GmpProject = gmp
GmpVersion = 4.2.3

PROJECT_DIR = $(Project)-$(Version)
PROJECT_PATH = $(THIRD_PARTY_PATH)/$(Project)/$(PROJECT_DIR)/$(PROJECT_DIR)
BUILD_DIR = $(Project)-$(Version)
BUILD_PATH = $(BUILDROOT)/$(BUILD_DIR)

GMP_BUILD_DIR = $(GmpProject)-$(GmpVersion)
GMP_BUILD_PATH = $(BUILDROOT)/$(GMP_BUILD_DIR)
LIB_PATH = $(INSTALL_PATH)/lib

ABI = $(TARGET_ABI)
CFLAGS="-m${ABI}"

all:
	rm -rf $(BUILD_PATH)
	mkdir -p $(BUILD_PATH)
	cd $(BUILD_PATH) && $(PROJECT_PATH)/configure CC=$(CC) \
	  CFLAGS=$(CFLAGS) --prefix=$(INSTALL_PATH) --with-gmp=$(INSTALL_PATH)
	cd $(BUILD_PATH) && make
	cd $(BUILD_PATH) && make install
