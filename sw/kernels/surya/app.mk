APP              := surya
$(APP)_BUILD_DIR := $(SN_ROOT)/sw/kernels/$(APP)/build
SRCS             := $(SN_ROOT)/sw/kernels/$(APP)/src/$(APP).c

# Use the surya path that bender resolves
SURYA_ROOT ?= $(shell $(SN_BENDER) path surya)
SURYA_GEN_DIR ?= $($(APP)_BUILD_DIR)/generated

# The suite must match the hardware configuration of SN_BENDER_DEFINES.
# gwaihir.json is the ENABLE_PACE=0, MX_FP_ADD=1 suite.
SURYA_TEST_JSON ?= $(SURYA_ROOT)/tests/gwaihir.json
SURYA_TEST_NAME ?= MX_M64_N128_P64_NM2_MXO
SURYA_SEED ?= 42

$(APP)_INCDIRS += $(SURYA_GEN_DIR)
$(APP)_INCDIRS += $(SURYA_ROOT)/sw

$(APP)_HEADERS += $(SURYA_GEN_DIR)/surya_workload.h

$(SURYA_GEN_DIR)/surya_workload.h: | $(SURYA_GEN_DIR)
	PYTHONPATH=$(SURYA_ROOT):$(PYTHONPATH) python -m surya_model.workloads.cli \
	  --json $(SURYA_TEST_JSON) --test_name $(SURYA_TEST_NAME) \
	  --PACE_CONFIG $(SURYA_ROOT)/configs/pace_defaults.json \
	  --seed $(SURYA_SEED) \
	  --output_dir $(SURYA_GEN_DIR) --no-debug

$(SURYA_GEN_DIR):
	mkdir -p $@

include $(SN_ROOT)/sw/kernels/common.mk
