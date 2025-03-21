module I2C (
  input clk,

  input       cmd_active,
  input       cmd_high_speed,
  input [6:0] cmd_addr,
  input       cmd_read,
  input       read_nack,
  output reg  addr_err,

  input            data_valid,
  output reg       data_ready,
  input      [7:0] data_in,
  output reg [7:0] data_out,
  output reg       data_err,

  output i2c_scl,
  output i2c_sda,
  input i2c_sda_IN
);

  localparam HALT          = 3'd0;
  localparam SWITCH_TO_HS  = 3'd1;
  localparam SEND_ADDR     = 3'd2;
  localparam DATA_IDLE     = 3'd3;
  localparam DATA_TRANSFER = 3'd4;

  reg [2:0] state = HALT;
  reg halt_required = 0;

  parameter CLK_FREQ = 60_000_000;
  localparam DIVISOR_400K = CLK_FREQ / (400_000 * 4) - 1 + 1;  // 36.5 + 1 -> 37 (394 kHz)
  localparam DIVISOR_HS = CLK_FREQ / (3_000_000 * 4) - 1;  // 4

  reg [5:0] clk_counter = 0;
  reg [1:0] q = 0;
  reg [3:0] bit_counter = 0;
  reg [7:0] data;
  reg hs_state = 0;
  initial data_ready = 0;
  initial data_err = 0;
  initial addr_err = 0;

  reg sda = 1;
  reg sda_in;
  assign i2c_scl = (state == HALT || q[1]) && (state != DATA_IDLE);
  assign i2c_sda = sda;

  always @(posedge clk) begin
    sda_in <= i2c_sda_IN;
    if (state == HALT)
      halt_required <= 0;
    else if (~cmd_active)
      halt_required <= 1;
    if (data_ready) data_ready <= 0;
    if (clk_counter != 0) begin
      clk_counter <= clk_counter - 1'b1;
    end else begin
      clk_counter <= hs_state ? DIVISOR_HS : DIVISOR_400K;
      q <= q + 1'b1;
      if (q == 2'd0 && state != HALT) begin
        if (|bit_counter) begin
          bit_counter <= bit_counter - 1'b1;
          if (~cmd_read || state != DATA_TRANSFER) {sda, data} <= {data, 1'b1};
          else sda <= bit_counter == 4'd1 ? read_nack : 1'b1;
        end else sda <= 0;
      end else if (q == 2'd2) begin
        case (state)
          HALT: begin
            if (~sda)
              sda <= 1;
            else if (cmd_active) begin
              sda <= 0;
              state <= cmd_high_speed ? SWITCH_TO_HS : SEND_ADDR;
              data  <= cmd_high_speed ? 8'h08 : {cmd_addr, cmd_read};
              bit_counter <= 4'd9;
            end
          end
          SWITCH_TO_HS:
            if (bit_counter == 4'd0) sda <= 0;
          SEND_ADDR:
            if (bit_counter == 4'd0) addr_err <= sda_in;
          DATA_TRANSFER: begin
            if (cmd_read) data <= {data[6:0], sda_in};
            if (bit_counter == 4'd0) begin
              if (~data_ready) data_ready <= 1;
              data_out <= data;
              if (~cmd_read) data_err <= sda_in;
            end
          end
          default: begin end
        endcase
      end else if (q == 2'd3) begin
        case (state)
          SWITCH_TO_HS:
            if (bit_counter == 4'd0) begin
              state <= SEND_ADDR;
              data <= {cmd_addr, cmd_read};
              bit_counter <= 4'd9;
              hs_state <= 1;
            end
          SEND_ADDR, DATA_TRANSFER:
            if (bit_counter == 4'd0) state <= DATA_IDLE;
          DATA_IDLE:
            if (halt_required) begin
              state <= HALT;
              hs_state <= 0;
            end else if (data_valid) begin
              state <= DATA_TRANSFER;
              data <= data_in;
              bit_counter <= 4'd9;
            end
          default: begin end
        endcase
      end
    end
  end

endmodule

// Not used in the SoC. Was added for debugging only.
/*module I2C_APB(
  input clk,
  input reset,

  input       [3:0] apb_PADDR,
  input             apb_PSEL,
  input             apb_PENABLE,
  output            apb_PREADY,
  input             apb_PWRITE,
  input      [31:0] apb_PWDATA,
  output reg [31:0] apb_PRDATA,

  output i2c_scl,
  inout i2c_sda
);

  reg [6:0] cmd_addr;
  reg cmd_active, cmd_high_speed, cmd_read, read_nack;
  reg has_data;
  wire addr_err, data_err, data_ready;
  reg [7:0] data_in;
  wire [7:0] data_out;

  I2C i2c(
    .clk(clk),

    .cmd_active(cmd_active),
    .cmd_high_speed(cmd_high_speed),
    .cmd_addr(cmd_addr),
    .cmd_read(cmd_read),
    .addr_err(addr_err),
    .read_nack(read_nack),

    .data_valid(has_data ^ cmd_read),
    .data_ready(data_ready),
    .data_in(data_in),
    .data_out(data_out),
    .data_err(data_err),

    .i2c_scl(i2c_scl),
    .i2c_sda(i2c_sda)
  );

  assign apb_PREADY = 1'b1;

  always @(posedge clk) begin
    if (reset) begin
      cmd_addr <= 7'd0;
      {read_nack, cmd_active, cmd_high_speed, cmd_read} <= 3'd0;
      has_data <= 0;
    end else begin
      if (cmd_read) begin
        if (~has_data & data_ready & cmd_active) has_data <= 1;
      end else begin
        if (has_data & data_ready) has_data <= 0;
      end
      if (apb_PSEL & ~apb_PWRITE) begin
        case (apb_PADDR)
          4'd0: apb_PRDATA <= {25'd0, cmd_addr};
          4'd4: apb_PRDATA <= {26'd0, has_data, addr_err, data_err, read_nack, cmd_active, cmd_high_speed, cmd_read};
          4'd8: begin apb_PRDATA <= {24'd0, data_out}; if (has_data) has_data <= 0; end
        endcase
      end else if (apb_PSEL & apb_PENABLE & apb_PWRITE) begin
        case (apb_PADDR)
          4'd0: cmd_addr <= apb_PWDATA[6:0];
          4'd4: begin {read_nack, cmd_active, cmd_high_speed, cmd_read} <= apb_PWDATA[3:0]; has_data <= 0; end
          4'd8: if (~has_data & ~cmd_read) begin has_data <= 1'd1; data_in <= apb_PWDATA[7:0]; end
        endcase
      end
    end
  end

endmodule*/
