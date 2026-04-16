# Common includes (ASIC-independent)
include $(PLATFORM_PATH)/docker-dpu-base.mk
include $(PLATFORM_PATH)/docker-dpu.mk
include $(PLATFORM_PATH)/one-image.mk
include $(PLATFORM_PATH)/sdk.mk
include $(PLATFORM_PATH)/docker-syncd-pensando.mk
include $(PLATFORM_PATH)/dsc-drivers.mk

# Define ASIC variable with default to elba
ASIC ?= elba
export ASIC

# ASIC-specific includes and configuration
ifeq ($(ASIC),salina)
    $(info *** Building Pensando for Salina ASIC ***)

    # Include Salina platform modules (Leni)
    include $(PLATFORM_PATH)/sonic-platform-modules-leni.mk

    # Apply external patches for Salina kernel
    override INCLUDE_EXTERNAL_PATCHES := y
    override EXTERNAL_KERNEL_PATCH_LOC := $(BUILD_WORKDIR)/$(PLATFORM_PATH)/non-upstream-patches/
    override EXTERNAL_KERNEL_PATCH_URL :=
    export INCLUDE_EXTERNAL_PATCHES
    export EXTERNAL_KERNEL_PATCH_LOC
    export EXTERNAL_KERNEL_PATCH_URL
else
    $(info *** Building Pensando for Elba ASIC ***)

    # Include Elba platform modules (DPU)
    include $(PLATFORM_PATH)/sonic-platform-modules-dpu.mk

    # Do NOT apply external patches for Elba
    override INCLUDE_EXTERNAL_PATCHES := n
    override EXTERNAL_KERNEL_PATCH_LOC :=
    override EXTERNAL_KERNEL_PATCH_URL :=
    export INCLUDE_EXTERNAL_PATCHES
    export EXTERNAL_KERNEL_PATCH_LOC
    export EXTERNAL_KERNEL_PATCH_URL
endif

SONIC_ALL += $(SONIC_ONE_IMAGE) \
             $(DOCKER_FPM)

# Inject pensando sai into syncd
$(SYNCD)_DEPENDS += $(PENSANDO_SAI)
$(SYNCD)_UNINSTALLS += $(PENSANDO_SAI)

#Runtime dependency on pensando sai is set only for syncd
$(SYNCD)_RDEPENDS += $(PENSANDO_SAI)
