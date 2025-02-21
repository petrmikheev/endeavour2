module Ddr3Controller(
    input clk,
    input reset_n,

    // DDR interface, ignored in simulation
    input core_clk,
    input twd_clk,
    input tdqss_clk,
    input tac_clk,
    output reset,
    output cs,
    output ras,
    output cas,
    output we,
    output cke,
    output [15:0] addr,
    output [2:0] ba,
    output odt,
    output [2:0] shift,
    output [4:0] shift_sel,
    output shift_ena,
    output [1:0] o_dm_hi,
    output [1:0] o_dm_lo,
    input [1:0] i_dqs_hi,
    input [1:0] i_dqs_lo,
    output [1:0] o_dqs_hi,
    output [1:0] o_dqs_lo,
    output [1:0] o_dqs_oe,
    output [1:0] o_dqs_n_oe,
    input [15:0] i_dq_hi,
    input [15:0] i_dq_lo,
    output [15:0] o_dq_hi,
    output [15:0] o_dq_lo,
    output [15:0] o_dq_oe,

    // calibration -> always report OK
    input cal_ena,
    output cal_done,
    output cal_pass,
    output [7:0] cal_fail_log,

    // user interface, it is what we simulate here
    output reg [127:0] rd_data,
    output rd_ack,
    output rd_valid,
    input rd_en,
    input rd_addr_en,
    input [31:0] rd_addr,
    output rd_busy,
    input [15:0] wr_datamask,  // ignore, not used in adapter
    input [127:0] wr_data,
    output wr_ack,
    input wr_addr_en,
    input wr_en,
    input [31:0] wr_addr,
    output wr_busy
);

  assign cal_done = 1'b1;
  assign cal_pass = 1'b1;
  assign cal_fail_log = 8'h63;

  localparam MEM_SIZE = 1024*1024;  // in simulation use only 1 MB out of 1 GB
  localparam WORD_COUNT = MEM_SIZE / 16;
  reg [127:0] mem [0:WORD_COUNT - 1];

  reg [7:0] wr_ack_queue;
  reg [31:0] rd_addr_queue [15:0];
  reg [3:0] a_in, a_out;

  assign rd_busy = 1'b0;
  assign wr_busy = 1'b0;
  assign wr_ack = wr_ack_queue[0];

  reg [2:0] cnt;

  always @(posedge clk) begin
    if (reset) begin
      wr_ack_queue <= 0;
      a_in <= 0;
      a_out <= 0;
      cnt <= 0;
    end else begin
      wr_ack_queue <= {wr_en, wr_ack_queue[7:1]};
      if (wr_en) mem[wr_addr] <= wr_data;
      if (rd_addr_en) begin
        rd_addr_queue[a_in] <= rd_addr;
        a_in <= a_in + 1'b1;
      end
      rd_valid <= 0;
      if (rd_valid & rd_en) begin
        a_out <= a_out + 1'b1;
        rd_valid <= 0;
      end
      if (cnt == 2) begin
        cnt <= 0;
        if (a_in != a_out) begin
          rd_data <= mem[rd_addr_queue[a_out]];
          rd_valid <= 1;
        end
      end else cnt <= cnt + 1'b1;
    end
  end

endmodule
