// Copyright 2026 ETH Zurich and University of Bologna.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0

#include "snrt.h"

#include <stdint.h>
#include <stdio.h>

#define SURYA_BASE_ADDR \
  ((uintptr_t)snrt_cluster_alias()->zeromem.mem + sizeof(snrt_cluster_alias()->zeromem.mem))

#include "surya_hal.h"
#include "surya_workload.h"

static const surya_hw_config_t surya_hw_config = {
    .cim_inner  = SURYA_HW_CIM_INNER,
    .cim_outer  = SURYA_HW_CIM_OUTER,
    .n_accum    = SURYA_HW_N_ACCUM,
    .n_cim      = SURYA_HW_N_CIM,
    .optimal_bw = SURYA_HW_OPTIMAL_BW,
};

static void* local_a;
static void* local_b;
static void* local_c;
static void* local_nq;
static void* local_mx_scale;
static void* local_mx_out_scale;

static void* l1_alloc_aligned(size_t size, uintptr_t alignment) {
  uintptr_t next = (uintptr_t)snrt_l1_next();
  uintptr_t aligned = (next + alignment - 1) & ~(alignment - 1);
  snrt_l1_update_next((void*)aligned);
  return snrt_l1_alloc(size);
}

static surya_task_config_t build_task0(void) {
  surya_task_config_t task = {};

  task.a_ptr                = (uint8_t*)local_a;
  task.b_ptr                = (uint8_t*)local_b;
  task.c_ptr                = (int8_t*)local_c;
  task.c_init_ptr           = (void*)TASK0_C_INIT_PTR;
  task.nq_ptr               = (uint32_t*)local_nq;
  task.pace_ptr             = (uint16_t*)TASK0_PACE_PTR;
  task.pace_inv_ptr         = (uint16_t*)TASK0_PACE_INV_PTR;
  task.pace_eps             = TASK0_PACE_EPS;
  task.pace_eps_const       = TASK0_PACE_EPS_CONST;
  task.mx_scale_ptr         = (uint8_t*)local_mx_scale;
  task.mx_block_size        = TASK0_MX_BLOCK_SIZE;
  task.mx_out_scale_ptr     = (uint8_t*)local_mx_out_scale;
  task.mx_out_scale_golden  = (uint8_t*)TASK0_MX_OUT_SCALE_PTR;
  task.mx_out_scale_size    = TASK0_MX_OUT_SCALE_SIZE;
  task.mx_out_int8          = TASK0_MX_OUT_INT8;
  task.m                    = TASK0_M;
  task.n                    = TASK0_N;
  task.p                    = TASK0_P;
  task.op_mode              = (cim_op_mode_t)TASK0_OP_MODE;
  task.norm_mode            = (cim_norm_mode_t)TASK0_NORM_MODE;
  task.accum_init_mode      = (cim_accum_init_mode_t)TASK0_ACCUM_INIT_MODE;
  task.dw_output            = TASK0_DW_OUTPUT;
  task.dw_pad               = TASK0_DW_PAD;
  task.dw_stride            = TASK0_DW_STRIDE;
  task.dw_h_in              = TASK0_DW_H_IN;
  task.dw_w_in              = TASK0_DW_W_IN;
  task.nq_dim               = TASK0_NQ_DIM;
  task.a_signed             = TASK0_A_SIGNED;
  task.b_signed             = TASK0_B_SIGNED;
  task.relu                 = TASK0_RELU;
  task.out_unsigned         = TASK0_OUT_UNSIGNED;
  task.force_rr_priority    = TASK0_FORCE_RR_PRIORITY;
  task.compute              = TASK0_COMPUTE;
  task.streamout            = TASK0_STREAMOUT;
  task.accum_continue       = TASK0_ACCUM_CONTINUE;
  task.reuse_cim            = TASK0_REUSE_CIM;
  task.cim_context          = TASK0_CIM_CONTEXT;
  task.prefetch             = TASK0_PREFETCH;
  task.disable_zigzag       = TASK0_DISABLE_ZIGZAG;
  task.disable_weight_reuse = TASK0_DISABLE_WEIGHT_REUSE;
  task.soft_clear_state     = TASK0_SOFT_CLEAR_STATE;
  task.check_accum          = TASK0_CHECK_ACCUM;
  task.c_golden             = (int8_t*)TASK0_C_GOLDEN;
  task.c_size               = TASK0_C_SIZE;

  return task;
}

static int compare_bytes(const char* name, const uint8_t* got, const uint8_t* expected,
                         uint32_t size) {
  int errors = 0;

  for (uint32_t i = 0; i < size; i++) {
    if (got[i] != expected[i]) {
      if (errors < 10) {
        printf("%s mismatch at %u: expected 0x%02x, got 0x%02x\n", name, (unsigned)i,
               expected[i], got[i]);
      }
      errors++;
    }
  }

  if (errors == 0) {
    printf("%s PASS (%u bytes)\n", name, (unsigned)size);
  } else {
    printf("%s FAIL (%d/%u mismatches)\n", name, errors, (unsigned)size);
  }

  return errors;
}

static void dump_ptr(const char* name, const void* ptr) {
  printf("%s = 0x%08x\n", name, (unsigned)(uintptr_t)ptr);
}

int main() {
  int total_errors = 0;

  snrt_int_clr_mcip();

  if (snrt_is_dm_core()) {
    local_a            = l1_alloc_aligned(TASK0_MATRIX_A_SIZE, 64);
    local_b            = l1_alloc_aligned(TASK0_MATRIX_B_SIZE, 64);
    local_c            = l1_alloc_aligned(TASK0_C_BUF_SIZE, 64);
    local_nq           = l1_alloc_aligned(TASK0_NORMQUANT_SIZE * sizeof(uint32_t), 64);
    local_mx_scale     = l1_alloc_aligned(TASK0_MX_SCALE_SIZE, 64);
    local_mx_out_scale = l1_alloc_aligned(TASK0_MX_OUT_SCALE_SIZE ? TASK0_MX_OUT_SCALE_SIZE : 1, 64);

    snrt_dma_start_1d(local_a, (void*)TASK0_A_PTR, TASK0_MATRIX_A_SIZE);
    snrt_dma_start_1d(local_b, (void*)TASK0_B_PTR, TASK0_MATRIX_B_SIZE);
    snrt_dma_start_1d(local_nq, (void*)TASK0_NQ_PTR, TASK0_NORMQUANT_SIZE * sizeof(uint32_t));
    snrt_dma_start_1d(local_mx_scale, (void*)TASK0_MX_SCALE_PTR, TASK0_MX_SCALE_SIZE);
    snrt_dma_wait_all();
  }
  snrt_cluster_hw_barrier();

  if (snrt_cluster_core_idx() == 0) {
    printf("[Cycle: %u] Starting Surya MX kernel\n", snrt_mcycle());
    printf("Task shape: M=%u N=%u P=%u, MX block=%u, MX_OUT_INT8=%u\n", (unsigned)TASK0_M,
           (unsigned)TASK0_N, (unsigned)TASK0_P, (unsigned)TASK0_MX_BLOCK_SIZE,
           (unsigned)TASK0_MX_OUT_INT8);
    printf("local_a %p\n", local_a);
    printf("local_b %p\n", local_b);
    printf("local_c %p\n", local_c);
    printf("local_nq %p\n", local_nq);
    printf("local_mx_scale %p\n", local_mx_scale);
    printf("local_mx_out_scale %p\n", local_mx_out_scale);
    // total_errors += compare_bytes("DMA A", (const uint8_t*)local_a, (const uint8_t*)TASK0_A_PTR,
    //                               TASK0_MATRIX_A_SIZE);
    total_errors += compare_bytes("DMA B", (const uint8_t*)local_b, (const uint8_t*)TASK0_B_PTR,
                                  10);
    // total_errors += compare_bytes("DMA NQ", (const uint8_t*)local_nq, (const uint8_t*)TASK0_NQ_PTR,
    //                               TASK0_NORMQUANT_SIZE * sizeof(uint32_t));
    // total_errors += compare_bytes("DMA MX scale", (const uint8_t*)local_mx_scale,
    //                               (const uint8_t*)TASK0_MX_SCALE_PTR, TASK0_MX_SCALE_SIZE);
    const int input_errors = total_errors;
    if (input_errors != 0) {
      printf("[Cycle: %u] Input DMA failed; not starting Surya\n", snrt_mcycle());
    } else {
      surya_soft_clear();
      for (volatile int i = 0; i < 10; i++)
        ;

      while (surya_acquire() < 0)
        ;

      surya_task_config_t task                    = build_task0();
      surya_regif__hwpe_ctrl_job_dep_t surya_cfg = surya_derive_config(task, surya_hw_config);

      printf("[Cycle: %u] Surya acquired and configured\n", snrt_mcycle());
      surya_program(&task, &surya_cfg);
      surya_trigger();

      while (surya_status() != 0)
        ;

      printf("[Cycle: %u] Surya done\n", snrt_mcycle());

      total_errors += compare_bytes("C", (const uint8_t*)local_c, (const uint8_t*)TASK0_C_GOLDEN,
                                    TASK0_C_SIZE);

      // if (TASK0_MX_OUT_SCALE_SIZE) {
      //   total_errors += compare_bytes("MX output scale", (const uint8_t*)local_mx_out_scale,
      //                                 (const uint8_t*)TASK0_MX_OUT_SCALE_PTR,
      //                                 TASK0_MX_OUT_SCALE_SIZE);
      // }

      if (total_errors == 0) {
        printf("[Cycle: %u] Surya MX kernel PASS\n", snrt_mcycle());
      } else {
        printf("[Cycle: %u] Surya MX kernel FAIL: %d total mismatches\n", snrt_mcycle(),
               total_errors);
      }
    }
  }

  snrt_cluster_hw_barrier();
  return total_errors;
}
