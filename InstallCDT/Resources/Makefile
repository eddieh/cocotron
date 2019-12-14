##
# Makefile for installing Xcode Specifications
##

SCRIPTS = $(SRCROOT)/InstallCDT/Resources/scripts
RESOURCES_SPECS =  $(SRCROOT)/InstallCDT/Resources/Specifications

all:
	"${SCRIPTS}/createSpecifications.sh" $(TARGET_PLATFORM) \
	    $(TARGET_ARCH) $(PRODUCT_NAME) $(PRODUCT_VERSION) \
	    $(COMPILER_TARGET) "${RESOURCES_SPECS}" $(COMPILER) \
	    $(COMPILER_VERSION)
