PREFIX =
SRCROOT = $(PWD)
BUILDROOT = $(PWD)/build
DSTROOT = $(HOME)/Library/Developer

PRODUCT_NAME = Cocotron
PRODUCT_VERSION = 1.0

TARGET_PLATFORM = Windows
TARGET_ARCH = i386
TARGET_ABI = 32

COMPILER = gcc
COMPILER_VERSION = 4.3.1
COMPILER_TARGET = i386-pc-mingw32msvc

COMPILER_DIR = $(COMPILER)-$(COMPILER_VERSION)

PRODUCT_PATH = $(DSTROOT)/$(PRODUCT_NAME)/$(PRODUCT_VERSION)
INTERFACE_PATH = $(PRODUCT_PATH)/PlatformInterfaces/$(COMPILER_TARGET)
INSTALL_PATH = $(PRODUCT_PATH)/$(TARGET_PLATFORM)/$(TARGET_ARCH)/$(COMPILER_DIR)
TOOL_PATH = $(PRODUCT_PATH)/bin
THIRD_PARTY_PATH = $(SRCROOT)/third-party

export PREFIX SRCROOT BUILDROOT DSTROOT TARGET_PLATFORM TARGET_ARCH \
 TARGET_ABI PRODUCT_PATH INSTALL_PATH THIRD_PARTY_PATH


MODULES = third-party

all:
	@for dir in $(MODULES); do ${MAKE} $@ -C $$dir; exit_status=$$?; \
	if [ $$exit_status -ne 0 ]; then exit $$exit_status; fi; done

debug d:
	@for dir in $(MODULES); do ${MAKE} $@ -C $$dir; exit_status=$$?; \
	if [ $$exit_status -ne 0 ]; then exit $$exit_status; fi; done

release r:
	@for dir in $(MODULES); do ${MAKE} $@ -C $$dir; exit_status=$$?; \
	if [ $$exit_status -ne 0 ]; then exit $$exit_status; fi; done

analyze:
	@for dir in $(MODULES); do ${MAKE} $@ -C $$dir; exit_status=$$?; \
	if [ $$exit_status -ne 0 ]; then exit $$exit_status; fi; done

clean:
	@for dir in $(MODULES); do ${MAKE} $@ -C $$dir; exit_status=$$?; \
	if [ $$exit_status -ne 0 ]; then exit $$exit_status; fi; done
