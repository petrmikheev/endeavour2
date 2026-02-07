module AudioController (
  input reset, input clk,

  output        i2c_scl,
  output        i2c_sda,
  input         i2c_sda_IN,
  output reg    shdn,

  input   [2:0] apb_PADDR,
  input         apb_PSEL,
  input         apb_PENABLE,
  output        apb_PREADY,
  input         apb_PWRITE,
  input  [31:0] apb_PWDATA,
  output [31:0] apb_PRDATA
);

  parameter I2C_ADDR = 7'b1100000;
  parameter FIFO_SIZE = 1024;
  localparam FIFO_BITS = $clog2(FIFO_SIZE);

  reg [15:0] divisor;
  reg [15:0] counter;
  reg [3:0] current_volume;
  reg [3:0] target_volume;

  reg [23:0] fifo [0:FIFO_SIZE-1];
  reg [FIFO_BITS-1:0] ina, outa, remaining;
  reg sel_cfg, sel_stream;
  reg flush;
  reg no_sleep;
  reg volume_initialized;
  reg volume_up;
  reg [3:0] volume_delay;

  assign apb_PREADY = volume_initialized;
  assign apb_PRDATA = sel_cfg ? {ina == outa, 10'b0, no_sleep, target_volume, divisor} : remaining;

  localparam STATE_HALT = 5'd0;
  localparam STATE_IDLE = 5'd1;
  localparam STATE_SL0  = 5'd2;
  localparam STATE_SL1  = 5'd3;
  localparam STATE_SL2  = 5'd4;
  localparam STATE_SR0  = 5'd5;
  localparam STATE_SR1  = 5'd6;
  localparam STATE_SR2  = 5'd7;
  localparam STATE_VV0  = 5'd8;
  localparam STATE_VV1  = 5'd9;
  localparam STATE_VV2  = 5'd10;
  localparam STATE_VU0  = 5'd11;
  localparam STATE_VU1  = 5'd12;
  localparam STATE_VU2  = 5'd13;
  localparam STATE_VD0  = 5'd14;
  localparam STATE_VD1  = 5'd15;
  localparam STATE_VD2  = 5'd16;
  localparam STATE_HALTING = 5'd17;

  reg [4:0] state = STATE_HALT;
  reg [23:0] value;

  assign shdn = (state == STATE_HALT) & ~no_sleep;

  wire i2c_data_valid = state > STATE_IDLE;
  wire i2c_data_ready;
  reg [7:0] i2c_data;

  always @(*) begin
    case(state)
      STATE_SL0: i2c_data = 8'b01000010; // channel B
      STATE_SL1: i2c_data = {4'b0, value[11:8]};
      STATE_SL2: i2c_data = value[7:0];
      STATE_SR0: i2c_data = 8'b01000100; // channel C
      STATE_SR1: i2c_data = {4'b0, value[23:20]};
      STATE_SR2: i2c_data = value[19:12];
      STATE_VV0: i2c_data = 8'b01000000; // channel A
      STATE_VV1: i2c_data = volume_up ? 8'h0f : 8'h00;
      STATE_VV2: i2c_data = volume_up ? 8'hff : 8'h00;
      STATE_VU0: i2c_data = 8'b01000110; // channel D
      STATE_VU1: i2c_data = 8'h0f;
      STATE_VU2: i2c_data = 8'hff;
      STATE_VD0: i2c_data = 8'b01000110; // channel D
      STATE_VD1: i2c_data = 8'h0;
      STATE_VD2: i2c_data = 8'h0;
      default: i2c_data = 8'h0;
    endcase
  end

  I2C i2c(
    .clk(clk),

    .cmd_high_speed(1'b1),
    .cmd_addr(I2C_ADDR),
    .cmd_read(1'b0),
    .cmd_active(state != STATE_HALT),
    .read_nack(1'b0),
    .addr_err(),

    .data_valid(i2c_data_valid),
    .data_ready(i2c_data_ready),
    .data_in(i2c_data),
    .data_err(),
    .data_out(),

    .i2c_scl(i2c_scl),
    .i2c_sda(i2c_sda),
    .i2c_sda_IN(i2c_sda_IN)
  );

  always @(posedge clk) begin
    if (reset) begin
      divisor <= 0;
      counter <= 0;
      ina <= 0;
      outa <= 0;
      remaining <= FIFO_BITS'(FIFO_SIZE - 1);
      state <= STATE_HALT;
      current_volume <= 4'd15;
      target_volume <= 4'd0;
      volume_delay <= 0;
      volume_initialized <= 0;
      no_sleep <= 0;
    end else begin
      remaining <= FIFO_BITS'(outa - ina - 1'b1);
      sel_cfg <= apb_PSEL & ~apb_PADDR[2];
      sel_stream <= apb_PSEL & apb_PADDR[2];
      if (apb_PENABLE & apb_PWRITE & sel_cfg & volume_initialized) begin
        divisor <= apb_PWDATA[15:0];
        target_volume <= apb_PWDATA[19:16];
        no_sleep <= apb_PWDATA[20];
        flush <= apb_PWDATA[31];
      end else
        flush <= 0;
      if (apb_PENABLE & apb_PWRITE & sel_stream & volume_initialized) begin
        fifo[ina] <= {apb_PWDATA[27:16], apb_PWDATA[11:0]};
        ina <= ina + 1'b1;
      end else if (flush)
        ina <= outa;

      if (counter != 0)
        counter <= counter - 1'b1;

      if (state == STATE_HALT || state == STATE_HALTING) begin
        if (ina != outa || current_volume != target_volume) state <= STATE_IDLE;
        else if (counter == 0 && state == STATE_HALTING) state <= STATE_HALT;
      end else if (state == STATE_IDLE) begin
        if (current_volume != target_volume && (ina == outa || volume_delay == 0)) begin
          volume_up <= target_volume > current_volume;
          state <= STATE_VV0;
          volume_delay <= 4'd15;
        end else if (counter == 0 && ina != outa) begin
          counter <= divisor;
          outa <= outa + 1'b1;
          value <= fifo[outa];
          state <= STATE_SL0;
          volume_delay <= volume_delay - 1'd1;
        end
        if (counter == 0 && ina == outa && current_volume == target_volume) begin
          state <= STATE_HALTING;
          counter <= 16'd1000;
          volume_initialized <= 1'b1;
        end
      end else if (i2c_data_ready) begin
        if (state == STATE_VD2) current_volume <= volume_up ? (current_volume + 1'b1) : (current_volume - 1'b1);
        state <= (state == STATE_SR2 || state == STATE_VD2) ? STATE_IDLE : state + 1'b1;
      end

    end
  end

endmodule
