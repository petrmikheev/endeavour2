module Ddr3Adapter
#(parameter ADDRESS_WIDTH, parameter SOURCE_WIDTH)
(
    input clk, input reset,
    input calibration_done,

    output             native_wr_en, output native_wr_addr_en,
    output reg  [31:0] native_wr_addr,
    output      [15:0] native_wr_datamask,
    output reg [127:0] native_wr_data,
    input              native_wr_busy,
    input              native_wr_ack,

    output             native_rd_en,
    output reg         native_rd_addr_en,
    input              native_rd_busy,
    input              native_rd_valid,
    output reg  [31:0] native_rd_addr,
    input      [127:0] native_rd_data,
    input              native_rd_ack,  // ignore

    input                     tl_a_valid,
    output                    tl_a_ready,
    input [2:0]               tl_a_opcode,  // supported only 4=Get, 0=PutFull
    input [2:0]               tl_a_param,   // always 0, ignore
    input [SOURCE_WIDTH-1:0]  tl_a_source,
    input [ADDRESS_WIDTH-1:0] tl_a_address,
    input [2:0]               tl_a_size,    // always 6, ignore
    input [7:0]               tl_a_mask,    // always '1, ignore
    input [63:0]              tl_a_data,
    input                     tl_a_corrupt, // ignore
    output                    tl_d_valid,
    input                     tl_d_ready,
    output [2:0]              tl_d_opcode,  // supported only 0=WriteAck, 1=ReadData
    output [2:0]              tl_d_param,   // always 0
    output [SOURCE_WIDTH-1:0] tl_d_source,
    output [2:0]              tl_d_size,    // always 6
    output                    tl_d_denied,  // always 0
    output reg [63:0]         tl_d_data,
    output                    tl_d_corrupt  // always 0
);

  reg wr_en;
  reg [2:0] wr_beat, rd_beat;
  reg [1:0] rd_cmd_counter;
  reg rd_wbuf_has_data;
  reg rd_has_data;
  reg native_rd_ready;
  reg [127:0] rd_wbuf;
  reg [63:0] rd_buf;

  assign native_wr_datamask = 16'h0000;
  assign native_wr_en = wr_en;
  assign native_wr_addr_en = wr_en;

  assign tl_d_denied  = 1'b0;
  assign tl_d_corrupt = 1'b0;
  assign tl_d_param   = 3'b0;
  assign tl_d_size    = 3'd6;

  localparam FIFO_SIZE = 8;
  localparam FIFO_WIDTH = $clog2(FIFO_SIZE);
  reg [SOURCE_WIDTH-1:0] rds_fifo [FIFO_SIZE-1:0];
  reg [SOURCE_WIDTH-1:0] wrs_fifo [FIFO_SIZE-1:0];
  reg [FIFO_WIDTH:0] rds_in, rds_out, wrs_in, wrs_out;
  reg [FIFO_WIDTH+2:0] wr_ack_counter;
  wire rds_full = rds_in == (rds_out ^ {1'b1, {FIFO_WIDTH{1'b0}}});
  wire wrs_full = wrs_in == (wrs_out ^ {1'b1, {FIFO_WIDTH{1'b0}}});
  wire wr_ack_pending = (wr_ack_counter[FIFO_WIDTH+2:2] != wrs_out) & (rd_beat == 0);

  wire a_ready_write = ~native_wr_busy & ~wrs_full;
  wire a_ready_read  = ~native_rd_busy & ~rds_full & (rd_cmd_counter == 0);

  assign tl_a_ready = (tl_a_opcode[2] ? a_ready_read : a_ready_write) & calibration_done;

  assign tl_d_valid = wr_ack_pending | rd_has_data;
  assign tl_d_opcode = wr_ack_pending ? 3'd0 : 3'd1;
  assign tl_d_source = wr_ack_pending ? wrs_fifo[wrs_out[FIFO_WIDTH-1:0]] : rds_fifo[rds_out[FIFO_WIDTH-1:0]];

  wire rd_fire = tl_d_ready & ~wr_ack_pending & rd_has_data;
  wire rd_wbuf_needed = ~rd_has_data | (rd_fire & rd_beat[0]);
  assign native_rd_en = ~rd_wbuf_has_data | (rd_wbuf_needed & ~native_rd_ready);
  wire native_rd_fire = native_rd_valid & native_rd_ready;

  always @(posedge clk or posedge reset) begin
    if (reset) begin
      wr_beat <= 0;
      rd_beat <= 0;
      rd_cmd_counter <= 0;
      wr_ack_counter <= 0;
      wr_en <= 0;
      native_rd_addr_en <= 0;
      rds_in <= 0;
      rds_out <= 0;
      wrs_in <= 0;
      wrs_out <= 0;
      rd_wbuf_has_data <= 0;
      rd_has_data <= 0;
      native_rd_ready <= 0;
    end else if (calibration_done) begin
      // rd stream
      native_rd_ready <= native_rd_en;
      if (native_rd_fire) begin
        rd_wbuf <= native_rd_data;
        rd_wbuf_has_data <= 1;
      end else if (rd_wbuf_needed)
        rd_wbuf_has_data <= 0;
      if (rd_wbuf_has_data & rd_wbuf_needed) begin
        {rd_buf, tl_d_data} <= rd_wbuf;
        rd_has_data <= 1;
      end
      if (rd_fire) begin
        if (~rd_beat[0])
          tl_d_data <= rd_buf;
        else if (~rd_wbuf_has_data) rd_has_data <= 0;
        rd_beat <= rd_beat + 1'b1;
        if (rd_beat == 3'd7) rds_out <= rds_out + 1'b1;
      end

      // command and wr stream
      if (~native_wr_busy) wr_en <= 0;
      if (~native_rd_busy) native_rd_addr_en <= 0;
      if (tl_a_valid & tl_a_ready) begin
        if (tl_a_opcode[2]) begin // Get
          rds_fifo[rds_in[FIFO_WIDTH-1:0]] <= tl_a_source;
          rds_in <= rds_in + 1'b1;
          native_rd_addr <= {tl_a_address[ADDRESS_WIDTH-1:6], 2'b0};
          rd_cmd_counter <= 2'd1;
          native_rd_addr_en <= 1;
        end else begin // PutFull
          wr_beat <= wr_beat + 1'b1;
          if (wr_beat[0]) begin
            native_wr_data[127:64] <= tl_a_data;
            wr_en <= 1;
            native_wr_addr <= {tl_a_address[ADDRESS_WIDTH-1:6], wr_beat[2:1]};
          end else
            native_wr_data[63:0] <= tl_a_data;
          if (wr_beat == 3'd7) begin
            wrs_fifo[wrs_in[FIFO_WIDTH-1:0]] <= tl_a_source;
            wrs_in <= wrs_in + 1'b1;
          end
        end
      end
      if (~native_rd_busy & |rd_cmd_counter) begin
        native_rd_addr_en <= 1;
        native_rd_addr[1:0] <= rd_cmd_counter[1:0];
        rd_cmd_counter <= rd_cmd_counter + 1'b1;
      end
      if (native_wr_ack) wr_ack_counter <= wr_ack_counter + 1'b1;
      if (tl_d_ready & wr_ack_pending) wrs_out <= wrs_out + 1'b1;
    end
  end

endmodule
