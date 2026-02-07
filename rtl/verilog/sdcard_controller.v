module SdcardController (
  input reset, input clk,

  output        sdcard_clk_LO,
  output        sdcard_clk_HI,
  input         sdcard_cmd_IN_LO,
  input         sdcard_cmd_IN_HI,
  output        sdcard_cmd_OUT,
  output        sdcard_cmd_OE,
  input   [3:0] sdcard_data_IN_LO,
  input   [3:0] sdcard_data_IN_HI,
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

  wire vdd_1v8;
  assign sdcard_vdd_sel_3v3 = ~vdd_1v8;

  wire inverse = &apb_PADDR[4:3];
  wire [31:0] rdata_be;
  assign apb_PRDATA = inverse ? {rdata_be[7:0], rdata_be[15:8], rdata_be[23:16], rdata_be[31:24]} : rdata_be;

  wire cfg_ddr, cfg_ds, cfg_dscmd;
  wire [4:0] cfg_sample_shift;
  wire [7:0] sdclk;
  wire w_crcack, w_crcnak;
  wire cmd_en, cmd_collision, cmd_tristate;
  wire [1:0] cmd_data;
  wire data_en, data_tristate, rx_en;
  wire [31:0] tx_data;
  wire [1:0] rply_strb, rply_data;
  wire card_busy;
  wire [1:0] rx_strb;
  wire [15:0] rx_data;

  sdio #(
    //.OPT_LITTLE_ENDIAN(1),  doesn't work - with this option i_wb_data, o_wb_data are still big endian
    .LGFIFO(9),
    .OPT_EMMC(0),
    .OPT_SERDES(0),
    .OPT_DDR(1),
    .OPT_CARD_DETECT(1),
    .OPT_1P8V(1)
  ) u_sdio (
    .i_clk(clk),
    .i_reset(reset),

    .i_wb_addr({apb_PADDR[4] & ~apb_PADDR[3], apb_PADDR[3:2]}),
    .i_wb_data(inverse ? {apb_PWDATA[7:0], apb_PWDATA[15:8], apb_PWDATA[23:16], apb_PWDATA[31:24]} : apb_PWDATA),
    .o_wb_data(rdata_be),
    .o_wb_ack(apb_PREADY),
    .o_wb_stall(apb_PSLVERROR),
    .i_wb_we(apb_PWRITE),
    .i_wb_cyc(apb_PENABLE & apb_PSEL),
    .i_wb_stb(stb),
    .i_wb_sel(4'b1111),

    .i_card_detect(~sdcard_ndetect),
    .o_int(interrupt),
    .o_1p8v(vdd_1v8), .i_1p8v(vdd_1v8),

    // interface to frontend
    .o_cfg_ddr(cfg_ddr), .o_cfg_ds(cfg_ds), .o_cfg_dscmd(cfg_dscmd),
    .o_cfg_sample_shift(cfg_sample_shift),
    .o_sdclk(sdclk),
    .o_cmd_en(cmd_en), .o_cmd_tristate(cmd_tristate),
    .o_cmd_data(cmd_data),
    .o_data_en(data_en), .o_data_tristate(data_tristate),
    .o_rx_en(rx_en),
    .o_tx_data(tx_data),
    .i_cmd_strb(rply_strb), .i_cmd_data(rply_data),
    .i_cmd_collision(cmd_collision),
    .i_card_busy(card_busy),
    .i_rx_strb(rx_strb),
    .i_rx_data(rx_data),
    .i_crcack(w_crcack), .i_crcnak(w_crcnak),

    // unused
    .S_AC_VALID(1'b0), .S_AC_DATA(0),
    .S_AD_VALID(1'b0), .S_AD_DATA(0),
    .s_valid(1'b0),
    .s_ready(),
    .s_data(0),
    .m_valid(),
    .m_ready(1'b0),
    .m_data(),
    .m_last(),
    .o_dma_cyc(), .o_dma_stb(), .o_dma_we(),
    .o_dma_addr(),
    .o_dma_data(),
    .o_dma_sel(),
    .i_dma_stall(1'b0),
    .i_dma_ack(1'b0),
    .i_dma_data(0),
    .i_dma_err(1'b0),
    .o_hwreset_n()
  );

  sdfrontend_ddr u_sdfrontend (
    .i_clk(clk), .i_reset(reset),
    .i_cfg_ddr(cfg_ddr), .i_cfg_ds(cfg_ds), .i_cfg_dscmd(cfg_dscmd),
    .i_sample_shift(cfg_sample_shift),
    .i_sdclk(sdclk),
    .i_cmd_en(cmd_en), .i_cmd_tristate(cmd_tristate),
    .i_cmd_data(cmd_data), .o_data_busy(card_busy),
    .i_data_en(data_en), .i_data_tristate(data_tristate),
    .i_tx_data(tx_data),
    .i_rx_en(rx_en),
    .o_cmd_strb(rply_strb),
    .o_cmd_data(rply_data),
    .o_cmd_collision(cmd_collision),
    .o_crcack(w_crcack), .o_crcnak(w_crcnak),
    .o_rx_strb(rx_strb),
    .o_rx_data(rx_data),

    .sdcard_clk_LO(sdcard_clk_LO),
    .sdcard_clk_HI(sdcard_clk_HI),
    .sdcard_cmd_IN_LO(sdcard_cmd_IN_LO),
    .sdcard_cmd_IN_HI(sdcard_cmd_IN_HI),
    .sdcard_cmd_OUT(sdcard_cmd_OUT),
    .sdcard_cmd_OE(sdcard_cmd_OE),
    .sdcard_data_IN_LO(sdcard_data_IN_LO),
    .sdcard_data_IN_HI(sdcard_data_IN_HI),
    .sdcard_data_OUT(sdcard_data_OUT),
    .sdcard_data_OE(sdcard_data_OE)
  );

endmodule

// ***
// Below is modified copy of https://github.com/ZipCPU/sdspi/blob/master/rtl/sdfrontend.v
// Original author: Dan Gisselquist
// License: GPL, v3
// ***

module sdfrontend_ddr #(
		parameter [0:0]	OPT_CRCTOKEN = 1'b1,
		parameter BUSY_CLOCKS = 16,
		parameter HWBIAS = 0,
		parameter NUMIO = 4
	) (
		// {{{
		input	wire		i_clk,
		// Configuration
		input	wire		i_reset,
		input	wire		i_cfg_ddr,
		input	wire		i_cfg_ds, i_cfg_dscmd,
		input	wire	[4:0]	i_sample_shift,
		// Control signals
		// Tx path
		// {{{
		// MSB "first" incoming data.
		input	wire	[7:0]	i_sdclk,
		// Verilator lint_off SYNCASYNCNET
		input	wire		i_cmd_en, i_cmd_tristate,
		// Verilator lint_on  SYNCASYNCNET
		input	wire	[1:0]	i_cmd_data,
		//
		input	wire		i_data_en, i_rx_en, i_data_tristate,
		input	wire	[31:0]	i_tx_data,
		// }}}
		output	wire		o_data_busy,
		// Synchronous Rx path
		// {{{
		output	wire	[1:0]	o_cmd_strb,
		output	wire	[1:0]	o_cmd_data,
		output	wire		o_cmd_collision,
		//
		output	wire		o_crcack, o_crcnak,
		//
		output	wire	[1:0]	o_rx_strb,
		output	wire	[15:0]	o_rx_data,

    output        sdcard_clk_LO,
    output        sdcard_clk_HI,
    input         sdcard_cmd_IN_LO,
    input         sdcard_cmd_IN_HI,
    output        sdcard_cmd_OUT,
    output        sdcard_cmd_OE,
    input   [3:0] sdcard_data_IN_LO,
    input   [3:0] sdcard_data_IN_HI,
    output  [3:0] sdcard_data_OUT,
    output  [3:0] sdcard_data_OE
  );

	// Local declarations
	// {{{
	genvar		gk;
	reg		dat0_busy, wait_for_busy;
	reg	[$clog2(BUSY_CLOCKS+1)-1:0]	busy_count;
	reg		last_ck, sync_ack, sync_nak;
	wire	[7:0]	w_pedges, next_pedge, next_nedge, next_dedge;
	reg	[4:0]	acknak_sreg;

	reg	ackd;
	// }}}

	// Common setup
	// {{{
	initial	last_ck = 1'b0;
	always @(posedge i_clk)
		last_ck <= i_sdclk[0];

	assign	next_pedge = ~{ last_ck, i_sdclk[7:1] } &  i_sdclk[7:0];
	assign	next_nedge =  { last_ck, i_sdclk[7:1] } & ~i_sdclk[7:0];
	assign	next_dedge = next_pedge | (i_cfg_ddr ? next_nedge : 8'h0);

  // {{{
  // Notes:
  // {{{
  // The idea is, if we only have DDR elements and no SERDES
  // elements, can we do better than with just IOs?
  //
  // The answer is, Yes.  Even though we aren't going to run at
  // 2x the clock speed w/o OPT_SERDES, we can output a DDR clk,
  // and we can also access sub-sample timing via IDDR elements.
  // Even in DDR mode, however, there will be no possibility of
  // two outputs per clock.
  //
  // Fastest clock supported = incoming clock speed
  //	Practically, you won't be likely to achieve this unless
  //	you get really lucky, but it is technically the fastest
  //	speed this version supports.
  // A more realistic speed will be the incoming clock speed / 2,
  //	and done with more reliability than the non-DDR mode.
  // }}}

  // Local declarations
  // {{{
  wire	[1:0]	w_cmd;
  wire	[15:0]	w_dat;

  reg	[5:0]	ck_sreg;
  reg	[6:0]	pck_sreg, ck_psreg;
  reg	[1:0]	sample_ck, cmd_sample_ck, sample_pck;
  reg		resp_started, r_last_cmd_enabled,
      r_cmd_strb, r_cmd_data, r_rx_strb;
  reg	[1:0]	io_started;
  reg	[7:0]	r_rx_data;
  reg	[1:0]	busy_delay;
  wire	[HWBIAS+7:0]	wide_pedge, wide_dedge, wide_cmdedge;
  // }}}

  assign sdcard_clk_LO = i_sdclk[7];
  assign sdcard_clk_HI = i_sdclk[3];

  // CMD
  always @(posedge i_clk)
    r_last_cmd_enabled <= i_cmd_en;

  assign sdcard_cmd_OE = !i_cmd_tristate;
  assign sdcard_cmd_OUT = i_reset || i_cmd_data[1];
  assign w_cmd = {sdcard_cmd_IN_LO, sdcard_cmd_IN_HI};

  // DATA
  // {{{
  for(gk=0; gk<NUMIO; gk=gk+1)
  begin : DRIVE_DDR_IO
    assign sdcard_data_OE[gk] = !i_data_tristate;
    assign sdcard_data_OUT[gk] = i_reset || i_tx_data[24+gk];
    assign w_dat[gk+8] = sdcard_data_IN_LO[gk];
    assign w_dat[gk] = sdcard_data_IN_HI[gk];
  end for(gk=NUMIO; gk<8; gk=gk+1)
  begin : NO_DDR_IO
    assign	{ w_dat[8+gk], w_dat[gk] } = 2'b00;
  end
  // }}}

  // sample_ck
  // {{{
  assign	wide_dedge = { ck_sreg[HWBIAS+5:0], |next_dedge[7:4], |next_dedge[3:0] };

  initial	ck_sreg = 0;
  always @(posedge i_clk)
  if (i_data_en || i_cfg_ds)
    ck_sreg <= 0;
  else
    ck_sreg <= wide_dedge[HWBIAS+5:0];

  initial	sample_ck = 0;
  always @(*)
  if (i_data_en || !i_rx_en || i_cfg_ds)
    sample_ck = 0;
  else
    // Verilator lint_off WIDTH
    sample_ck = wide_dedge[HWBIAS +: 8] >> i_sample_shift[4:2];
    // Verilator lint_on  WIDTH
  // }}}

  // sample_pck -- positive edge data sampl clock
  // {{{
  assign	wide_pedge = { ck_psreg[HWBIAS+5:0], |next_pedge[7:4], |next_pedge[3:0] };

  initial	ck_psreg = 0;
  always @(posedge i_clk)
  if (i_data_en)
    ck_psreg <= 0;
  else
    ck_psreg <= wide_pedge[HWBIAS+5:0];

  initial	sample_pck = 0;
  always @(*)
  if (i_data_en)
    sample_pck = 0;
  else
    // Verilator lint_off WIDTH
    sample_pck = wide_pedge[HWBIAS +: 8] >> i_sample_shift[4:2];
    // Verilator lint_on  WIDTH
  // }}}

  // cmd_sample_ck: When do we sample the command line?
  // {{{
  assign	wide_cmdedge = { pck_sreg[HWBIAS+5:0], |next_pedge[7:4], |next_pedge[3:0] };

  always @(posedge i_clk)
  if (i_reset || i_cmd_en || r_last_cmd_enabled || i_cfg_dscmd)
    pck_sreg <= 0;
  else
    pck_sreg <= wide_cmdedge[HWBIAS + 5:0];

  always @(*)
  if (i_cmd_en || r_last_cmd_enabled || i_cfg_dscmd)
    cmd_sample_ck = 0;
  else
    // Verilator lint_off WIDTH
    cmd_sample_ck = wide_cmdedge[HWBIAS +: 8] >> i_sample_shift[4:2];
    // Verilator lint_on  WIDTH
  // }}}

  // CRC TOKEN detection
  // {{{
  always @(posedge i_clk)
  if(i_reset || i_data_en || i_cfg_ds || !OPT_CRCTOKEN)
    acknak_sreg <= -1;
  else if (acknak_sreg[4])
  begin
    if (sample_pck[1:0] == 2'b11 && acknak_sreg[3])
      acknak_sreg <= { acknak_sreg[2:0], w_dat[8], w_dat[0] };
    else if (sample_pck[1])
      acknak_sreg <= { acknak_sreg[3:0], w_dat[8] };
    else if (sample_pck[0])
      acknak_sreg <= { acknak_sreg[3:0], w_dat[0] };
  end

  initial	{ sync_ack, sync_nak } = 2'b00;
  always @(posedge i_clk)
  if(i_reset || i_data_en || i_cfg_ds || !OPT_CRCTOKEN)
  begin
    sync_ack <= 1'b0;
    sync_nak <= 1'b0;
  end else begin
    sync_ack <= (acknak_sreg == 5'b00101);
    sync_nak <= (acknak_sreg == 5'b01011);
  end
  // }}}


  always @(posedge i_clk)
  if (i_reset || i_cmd_en || r_last_cmd_enabled || i_cfg_dscmd)
    resp_started <= 1'b0;
  else if ((cmd_sample_ck != 0) && (cmd_sample_ck & w_cmd)==0)
    resp_started <= 1'b1;

  always @(posedge i_clk)
  if (i_reset || i_data_en || !i_rx_en || i_cfg_ds)
    io_started <= 2'b0;
  else if (sample_pck != 0
      && ((sample_pck & { w_dat[8], w_dat[0] }) == 0))
  begin
    io_started[0] <= 1'b1;
    if (!i_cfg_ddr)
      io_started[1] <= 1'b1;
    else if (sample_pck[1] && sample_ck[0])
      io_started[1] <= 1'b1;
  end else if (io_started == 2'b01 && sample_ck != 0)
    io_started <= 2'b11;

  // dat0_busy, wait_for_busy, busy_delay
  // {{{
  initial	busy_count = (OPT_CRCTOKEN) ? 3'h0 : 3'h4;
  always @(posedge i_clk)
  if (i_reset || i_cmd_en || i_data_en)
    // Clock periods to wait until busy is active
    busy_count <= BUSY_CLOCKS;
  else if (sample_pck != 0 && busy_count > 0)
    busy_count <= busy_count - 1;

  initial	busy_delay = -1;
  always @(posedge i_clk)
  if (i_reset || i_data_en)
    // System clock cycles to wait until busy can be read
    busy_delay <= -1;
  else if (busy_delay != 0)
    busy_delay <= busy_delay - 1;

  initial	{ dat0_busy, wait_for_busy } = 2'b01;
  always @(posedge i_clk)
  if (i_reset || i_data_en)
  begin
    dat0_busy <= 1'b0;
    wait_for_busy <= 1'b1;
  end else if (dat0_busy && !wait_for_busy &&
    ((sample_pck == 0)
    || (sample_pck & {w_dat[8],w_dat[0]})!=sample_pck))
  begin
    // Still busy ...
    dat0_busy <= 1'b1;
    wait_for_busy <= 1'b0;
  end else if (i_cmd_en)
  begin
    dat0_busy <= 1'b0;	// Should already be zero
    wait_for_busy <= 1'b1;
  end else if (wait_for_busy)
  begin
    dat0_busy <= 1'b1;
    wait_for_busy <= (busy_delay > 0) || (busy_count > 1);
  end else if ((sample_pck != 0)
      && (sample_pck & {w_dat[8],w_dat[0]})!=2'b0)
    dat0_busy <= 1'b0;

  assign	o_data_busy = dat0_busy;
  // }}}

  always @(posedge i_clk)
  begin

    // The command response
    // {{{
    if (i_reset || i_cmd_en || cmd_sample_ck == 0 || i_cfg_dscmd)
    begin
      r_cmd_strb <= 1'b0;
      // r_cmd_data <= r_cmd_data;
    end else if (resp_started)
    begin
      r_cmd_strb <= 1'b1;
      r_cmd_data <= |(cmd_sample_ck & w_cmd);
    end else if ((cmd_sample_ck[1] && !w_cmd[1])
        ||(cmd_sample_ck[0] && !w_cmd[0]))
    begin
      r_cmd_strb <= 1'b1;
      r_cmd_data <= 1'b0;
    end else
      r_cmd_strb <= 1'b0;
    // }}}

    // The data response
    // {{{
    if (i_data_en || sample_ck == 0 || i_cfg_ds)
      r_rx_strb <= 1'b0;
    else if (io_started[1])
      r_rx_strb <= 1'b1;
    else
      r_rx_strb <= 1'b0;
    // }}}

    if (sample_ck[1])
      r_rx_data <= w_dat[15:8];
    else
      r_rx_data <= w_dat[7:0];
  end

  assign	o_cmd_strb = { r_cmd_strb, 1'b0 };
  assign	o_cmd_data = { r_cmd_data, 1'b0 };
  assign	o_rx_strb  = { r_rx_strb, 1'b0 };
  assign	o_rx_data  = { r_rx_data, 8'h0 };

  reg	[7:0]	w_out;
  always @(*)
  begin
    w_out = 0;
    w_out[NUMIO-1:0] = w_dat[8 +: NUMIO]& w_dat[0 +: NUMIO];
  end

  initial	ackd = 0;
  always @(posedge i_clk)
  if (i_reset || i_data_en || !OPT_CRCTOKEN)
  begin
    ackd <= 0;
  end else if (sync_ack || sync_nak)
    ackd <= 1'b1;

  assign	o_crcack = OPT_CRCTOKEN && sync_ack && !ackd;
  assign	o_crcnak = OPT_CRCTOKEN && sync_nak && !ackd;

endmodule

`ifndef VERILATOR
`include "../sdspi/rtl/sdio.v"
`include "../sdspi/rtl/sdwb.v"
`include "../sdspi/rtl/sdckgen.v"
`include "../sdspi/rtl/sdcmd.v"
`include "../sdspi/rtl/sdtxframe.v"
`include "../sdspi/rtl/sdrxframe.v"
`endif
