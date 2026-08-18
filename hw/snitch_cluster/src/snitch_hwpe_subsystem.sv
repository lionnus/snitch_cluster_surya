// Copyright 2025-2026 ETH Zurich and University of Bologna.
// Solderpad Hardware License, Version 0.51, see LICENSE for details.
// SPDX-License-Identifier: SHL-0.51

module snitch_hwpe_subsystem
  import reqrsp_pkg::*;
#(
  parameter type         tcdm_req_t   = logic,
  parameter type         tcdm_rsp_t   = logic,
  parameter type         periph_req_t = logic,
  parameter type         periph_rsp_t = logic,
  parameter int unsigned IdWidth      = 8,
  parameter int unsigned NrCores      = 8
) (
  input logic clk_i,
  input logic rst_ni,

  output tcdm_req_t tcdm_req_o,
  input  tcdm_rsp_t tcdm_rsp_i,

  input  periph_req_t hwpe_ctrl_req_i,
  output periph_rsp_t hwpe_ctrl_rsp_o,

  output logic [NrCores-1:0] hwpe_evt_o
);

  localparam int unsigned BankByteWidth = surya_hwpe_pkg::BankBitwidth / surya_hwpe_pkg::ByteWidth;
  localparam int unsigned Mp = (surya_hwpe_pkg::TotBw
      + (surya_hwpe_pkg::MisalignedAccesses ? surya_hwpe_pkg::BankBitwidth : 0))
      / surya_hwpe_pkg::BankBitwidth;

  logic busy;
  logic [NrCores-1:0][surya_hwpe_pkg::RegfileNEvt-1:0] evt;

  logic [Mp-1:0] tcdm_req;
  logic [Mp-1:0] tcdm_gnt;
  logic [Mp-1:0][31:0] tcdm_add;
  logic [Mp-1:0] tcdm_wen;
  logic [Mp-1:0][BankByteWidth-1:0] tcdm_be;
  logic [Mp-1:0][surya_hwpe_pkg::BankBitwidth-1:0] tcdm_data;
  logic [Mp-1:0][surya_hwpe_pkg::BankBitwidth-1:0] tcdm_r_data;
  logic [Mp-1:0] tcdm_r_valid;

  logic periph_req;
  logic periph_gnt;
  logic [31:0] periph_add;
  logic periph_wen;
  logic [3:0] periph_be;
  logic [31:0] periph_data;
  logic [IdWidth-1:0] periph_id;
  logic [31:0] periph_r_data;
  logic periph_r_valid;
  logic [IdWidth-1:0] periph_r_id;

  initial begin
    assert (Mp == 1)
      else $fatal(1, "Surya Snitch integration expects one 512-bit TCDM port, got %0d", Mp);
  end

  always_comb begin
    tcdm_req_o.q_valid = tcdm_req[0];
    tcdm_req_o.q.addr  = tcdm_add[0];
    tcdm_req_o.q.write = ~tcdm_wen[0];
    tcdm_req_o.q.strb  = tcdm_be[0];
    tcdm_req_o.q.data  = tcdm_data[0];
    tcdm_req_o.q.amo   = AMONone;
    tcdm_req_o.q.user  = '0;

    tcdm_gnt     = '0;
    tcdm_r_data  = '0;
    tcdm_r_valid = '0;
    tcdm_gnt[0]     = tcdm_rsp_i.q_ready;
    tcdm_r_data[0]  = tcdm_rsp_i.p.data;
    tcdm_r_valid[0] = tcdm_rsp_i.p_valid;
  end

  assign periph_req  = hwpe_ctrl_req_i.q_valid;
  assign periph_add  = {24'h0, hwpe_ctrl_req_i.q.addr[7:0]};
  assign periph_wen  = ~hwpe_ctrl_req_i.q.write;
  assign periph_be   = hwpe_ctrl_req_i.q.strb;
  assign periph_data = hwpe_ctrl_req_i.q.data;
  assign periph_id   = hwpe_ctrl_req_i.q.user;

  assign hwpe_ctrl_rsp_o.q_ready = periph_gnt;
  assign hwpe_ctrl_rsp_o.p.data  = periph_r_data;
  assign hwpe_ctrl_rsp_o.p_valid = periph_r_valid;

  for (genvar i = 0; i < NrCores; i++) begin : gen_hwpe_evt
    assign hwpe_evt_o[i] = evt[i][0];
  end

  surya_hwpe_top_wrap #(
    .IdWidth  (IdWidth),
    .NumCores (NrCores)
  ) i_surya_top (
    .clk_i,
    .rst_ni,
    .busy_o         (busy),
    .evt_o          (evt),
    .tcdm_req       (tcdm_req),
    .tcdm_gnt       (tcdm_gnt),
    .tcdm_add       (tcdm_add),
    .tcdm_wen       (tcdm_wen),
    .tcdm_be        (tcdm_be),
    .tcdm_data      (tcdm_data),
    .tcdm_r_data    (tcdm_r_data),
    .tcdm_r_valid   (tcdm_r_valid),
    .periph_req     (periph_req),
    .periph_gnt     (periph_gnt),
    .periph_add     (periph_add),
    .periph_wen     (periph_wen),
    .periph_be      (periph_be),
    .periph_data    (periph_data),
    .periph_id      (periph_id),
    .periph_r_data  (periph_r_data),
    .periph_r_valid (periph_r_valid),
    .periph_r_id    (periph_r_id)
  );

endmodule
