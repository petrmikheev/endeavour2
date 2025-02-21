module UartController (
  input reset, input clk,

  input         uart_rx,
  output reg    uart_tx,

  input   [3:0] apb_PADDR,
  input         apb_PSEL,
  input         apb_PENABLE,
  output        apb_PREADY,
  input         apb_PWRITE,
  input  [31:0] apb_PWDATA,
  output [31:0] apb_PRDATA,

  output        interrupt
);

  parameter FIFO_SIZE = 1024;
  localparam FBITS = $clog2(FIFO_SIZE);

  reg [8:0] fifo_rx [FIFO_SIZE-1:0];
  reg [7:0] fifo_tx [FIFO_SIZE-1:0];
  reg [FBITS-1:0] rx_ina, rx_outa, tx_ina, tx_outa;
  reg [8:0] rx_outv;
  reg [7:0] tx_outv;
  reg rx_empty, tx_empty;
  reg tx_full;
  reg rx_err;
  assign interrupt = ~rx_empty;

  always @(posedge clk) begin
    rx_outv <= fifo_rx[rx_outa];
    tx_outv <= fifo_tx[tx_outa];
    rx_empty <= rx_ina == rx_outa;
    tx_empty <= tx_ina == tx_outa;
    tx_full <= tx_ina + 1'b1 == tx_outa;
  end

  reg selbuf_conf;
  reg selbuf_receiver;
  reg selbuf_transmitter;
  reg pwrite_buf;

  reg [15:0] divisor;
  reg use_parity;
  reg parity_odd;
  reg cstopb;

  reg uart_rx_buf = 0;
  always @(posedge clk) uart_rx_buf <= uart_rx;

  reg [3:0] read_bit_num = 0;
  reg [15:0] read_state = 0;
  reg [8:0] read_data = 0;
  reg [3:0] write_bit_num = 0;
  reg [15:0] write_state = 0;
  reg [8:0] write_data = 0;
  assign apb_PREADY = ~selbuf_conf | (tx_empty && write_bit_num == 0) | ~pwrite_buf;
  assign apb_PRDATA = selbuf_conf     ? {13'b0, cstopb, parity_odd, use_parity, divisor} :
                      selbuf_receiver ? {rx_empty, 21'b0, rx_err, rx_outv} :
                                        {tx_full, 31'b0};

  wire rx_err_clear = selbuf_receiver & apb_PENABLE & pwrite_buf;

  always @(posedge clk) begin
    pwrite_buf <= apb_PWRITE;
    if (reset) begin
      divisor <= 16'd415;
      use_parity <= 0;
      cstopb <= 0;
      selbuf_conf <= 0;
      selbuf_receiver <= 0;
      selbuf_transmitter <= 0;

      rx_ina <= 0;
      rx_outa <= 0;
      rx_err <= 0;
      tx_ina <= 0;
      tx_outa <= 0;

      read_bit_num <= 0;
      read_state <= 0;
      write_bit_num <= 0;
      write_state <= 0;

      uart_tx <= 1;
    end else begin

      // apb interface
      selbuf_receiver <= apb_PSEL && apb_PADDR[3:2] == 2'd0;
      selbuf_transmitter <= apb_PSEL && apb_PADDR[3:2] == 2'd1;
      selbuf_conf <= apb_PSEL && apb_PADDR[3:2] == 2'd2;
      if (selbuf_conf) begin
        if (apb_PWRITE & apb_PENABLE & tx_empty & (write_bit_num == 0)) begin
          divisor <= apb_PWDATA[15:0];
          {cstopb, parity_odd, use_parity} <= apb_PWDATA[18:16];
        end
      end else if (selbuf_receiver) begin
        if (apb_PENABLE & ~rx_empty & ~apb_PWRITE) rx_outa <= rx_outa + 1'b1;
      end else if (selbuf_transmitter) begin
        if (apb_PWRITE & apb_PENABLE) begin
          fifo_tx[tx_ina] <= apb_PWDATA[7:0];
          tx_ina <= tx_ina + 1'b1;
`ifdef IVERILOG
          $write("%c", apb_PWDATA[7:0]);
`endif
        end
      end

      // receive
      if (rx_err_clear) rx_err <= 0;
      if (read_bit_num != 0) begin
        if (read_state != 0)
          read_state <= read_state - 1'b1;
        else begin
          read_bit_num <= read_bit_num - 1'b1;
          read_data <= {uart_rx_buf, read_data[8:1]};
          if (read_bit_num == 4'b1) begin
            if (uart_rx_buf) begin
              fifo_rx[rx_ina] <= use_parity ? {^{read_data, parity_odd}, read_data[7:0]} : {1'b0, read_data[8:1]};
              rx_ina <= rx_ina + 1'b1;
            end else
              if (~rx_err_clear) rx_err <= 1;
          end else read_state <= divisor;
        end
      end else begin
        if (read_state == 16'b0 && ~uart_rx_buf) begin
          read_state[14:0] <= {divisor[15:3], 2'b01};
        end
        if (read_state == 16'b1) begin
          read_state <= divisor;
          read_bit_num <= 4'd9 + use_parity;
          if (~rx_err_clear & uart_rx_buf) rx_err <= 1;
        end
        if (|read_state[14:1]) begin
          read_state <= read_state - 1'b1;
        end
      end

      // transmit
      if (write_state != 0) write_state <= write_state - 1'b1;
      else begin
        if (write_bit_num != 0) begin
          write_bit_num <= write_bit_num - 1'b1;
          {write_data, uart_tx} <= {1'b1, write_data};
          write_state <= divisor;
        end else if (~tx_empty) begin
          write_data <= {use_parity ? ^tx_outv : 1'b1, tx_outv};
          tx_outa <= tx_outa + 1'b1;
          uart_tx <= 0;
          write_state <= divisor;
          write_bit_num <= 4'd9 + use_parity + cstopb;
        end else uart_tx <= 1;
      end

    end
  end

endmodule