# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: Copyright (C) 2024 Advanced Micro Devices, Inc.

ifeq ($(ARCH),aarch64)

# Build system: set up for docker build
ifneq ($(MKINFRA),)
include ${MKINFRA}/config_${ARCH}.mk
ifeq ($(DEBIAN_10),1)
KSRC ?= /sw/dsc-linux-5.15
else
KSRC ?= ${NICDIR}/buildroot/output/${ASIC}/linux-headers
endif
M = $(BLD_OUT_DIR)/ipc_submake
$(shell mkdir -p $(M))
KOPT += src=${TOPDIR}/platform/drivers/linux/ipc --debug V=1
KOPT += CROSS_COMPILE=$(TOOLCHAIN_PREFIX)-
endif

KOPT += ARCH=arm64
all: ipc_fw

else
all: ipc_host

endif

include dist.mk
KCFLAGS += -Ddrv_ver=\\\"$(VERSION)\\\"
KOPT += KCFLAGS=$(KCFLAGS)

KSRC ?= /lib/modules/$(shell uname -r)/build
M ?= $(CURDIR)

$(info ==================================)
$(info KSRC ${KSRC} M $(M))
$(info KOPT $(KOPT))
$(info ==================================)

ipc_host: git_tag
	cd $(KSRC) && . $(CURDIR)/kernel-gcc.sh && cd - && \
		$(MAKE) -C $(KSRC) M=$(M) modules $(KOPT)

ipc_fw: git_tag
	@echo "===> Building TAWK IPC driver "
	cd $(KSRC) && . $(CURDIR)/kernel-gcc.sh && cd - && \
		$(MAKE) -C $(KSRC) M=$(M) BUILD_PLATFORM=1 $(KOPT)

clean:
	$(MAKE) -C $(KSRC) M=$(M) clean $(KOPT)
