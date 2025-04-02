module SdcardController (
  input reset, input clk,

  output        sdcard_clk,
  input         sdcard_cmd_IN,
  output        sdcard_cmd_OUT,
  output        sdcard_cmd_OE,
  input   [3:0] sdcard_data_IN,
  output  [3:0] sdcard_data_OUT,
  output  [3:0] sdcard_data_OE,
  input         sdcard_ndetect,
  output        sdcard_vdd_sel_3v3,

  input   [4:0] apb_PADDR,
  input         apb_PSEL,
  input         apb_PENABLE,
  output        apb_PREADY,
  input         apb_PWRITE,
  input  [31:0] apb_PWDATA,
  output [31:0] apb_PRDATA,
  output        apb_PSLVERROR,

  output        interrupt
);

  reg stb_done = 0;
  wire stb = apb_PENABLE & apb_PSEL & ~stb_done;
  always @(posedge clk) begin
    if (reset) stb_done <= 1'b0;
    else if (stb) stb_done <= 1'b1;
    else if (apb_PREADY) stb_done <= 1'b0;
  end

  wire cmd_tristate;
  wire [3:0] dat_tristate;
  wire vdd_1v8;

  assign sdcard_cmd_OE = ~cmd_tristate;
  assign sdcard_data_OE = ~dat_tristate;
  assign sdcard_vdd_sel_3v3 = ~vdd_1v8;

  sdio_top #(
    .OPT_LITTLE_ENDIAN(1),
    .LGFIFO(9),
    .OPT_EMMC(0),
    .OPT_SERDES(0),
    .OPT_DDR(0),
    .OPT_CARD_DETECT(1),
    .OPT_1P8V(1)
  ) impl(
    .i_clk(clk), // 96-104mhz
    .i_reset(reset),
    //.i_hsclk() clk x4

    .o_ck(sdcard_clk),

    /*.io_cmd(sdcard_cmd),
    .io_dat(sdcard_data),*/

    .io_cmd_tristate(cmd_tristate),
    .o_cmd(sdcard_cmd_OUT),
    .i_cmd(sdcard_cmd_IN),
    .io_dat_tristate(dat_tristate),
    .o_dat(sdcard_data_OUT),
    .i_dat(sdcard_data_IN),

    .i_card_detect(~sdcard_ndetect),
    .o_int(interrupt),
    .o_1p8v(vdd_1v8), .i_1p8v(vdd_1v8),

    .i_wb_addr(apb_PADDR[4:2]),
    .i_wb_data(apb_PWDATA),
    .o_wb_data(apb_PRDATA),
    .o_wb_ack(apb_PREADY),
    .o_wb_stall(apb_PSLVERROR),
    .i_wb_we(apb_PWRITE),
    .i_wb_cyc(apb_PENABLE & apb_PSEL),
    .i_wb_stb(stb),
    .i_wb_sel(4'b1111),

    // unused
    .s_valid(0),
    .s_ready(),
    .s_data(0),
    .m_valid(),
    .m_ready(0),
    .m_data(),
    .m_last(),
    .o_dma_cyc(), .o_dma_stb(), .o_dma_we(),
    .o_dma_addr(),
    .o_dma_data(),
    .o_dma_sel(),
    .i_dma_stall(0),
    .i_dma_ack(0),
    .i_dma_data(0),
    .i_dma_err(0),
    .o_debug(),
    .i_hsclk(0),
    .i_ds(0),
    .o_hwreset_n()
  );

endmodule

`ifndef VERILATOR
`define VERILATOR 1  // it is a hack to avoid inout pins
`include "../sdspi/rtl/sdio_top.v"
`include "../sdspi/rtl/sdio.v"
`include "../sdspi/rtl/sdwb.v"
`include "../sdspi/rtl/sdckgen.v"
`include "../sdspi/rtl/sdcmd.v"
`include "../sdspi/rtl/sdtxframe.v"
`include "../sdspi/rtl/sdrxframe.v"
`include "../sdspi/rtl/sdfrontend.v"
`endif
