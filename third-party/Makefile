THIRD_PARTY_V1 := mingwrt/mingwrt-3.20
THIRD_PARTY_V1 += mingw32api/w32api-3.17-2
THIRD_PARTY_V1 += binutils/binutils-2.21.1
THIRD_PARTY_V1 += gmp/gmp-4.2.3
THIRD_PARTY_V1 += mpfr/mpfr-2.3.2
THIRD_PARTY_V1 += gcc/gcc-4.3.1


export PREFIX SRCROOT BUILDROOT DSTROOT TARGET_PLATFORM TARGET_ARCH \
 TARGET_ABI COMPILER_TARGET PRODUCT_PATH INSTALL_PATH INTERFACE_PATH \
 THIRD_PARTY_PATH CC

# PRODUCT_PATH 		~/Library/Developer/Cocotron/1.0
# INSTALL_PATH		~/Library/Developer/Cocotron/1.0/Windows/i386/gcc-4.3.1
# INTERFACE_PATH	~/Library/Developer/Cocotron/1.0/Windows/i386/gcc-4.3.1/i386-pc-mingw32msvc

MODULES = $(THIRD_PARTY_V1)

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
