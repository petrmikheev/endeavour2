module ApbClockBridge #(parameter AWIDTH = 32)(
  input clk_input, input clk_output,

  input  [AWIDTH-1:0] input_PADDR,
  input               input_PSEL,
  input               input_PENABLE,
  output reg          input_PREADY,
  input               input_PWRITE,
  input        [31:0] input_PWDATA,
  output reg   [31:0] input_PRDATA,
  output reg          input_PSLVERROR,

  output reg [AWIDTH-1:0] output_PADDR,
  output reg              output_PSEL,
  output reg              output_PENABLE,
  input                   output_PREADY,
  output reg              output_PWRITE,
  output reg       [31:0] output_PWDATA,
  input            [31:0] output_PRDATA,
  input                   output_PSLVERROR
);

  reg enable_buf;
  reg ready1 = 0, ready2 = 0;

  always @(posedge clk_output) begin
    output_PADDR <= input_PADDR;
    output_PSEL <= input_PSEL;
    output_PWRITE <= input_PWRITE;
    output_PWDATA <= input_PWDATA;

    enable_buf <= input_PENABLE;
    output_PENABLE <= output_PSEL & enable_buf & ~ready1 & ~(output_PENABLE & output_PREADY);
    if (output_PSEL & output_PENABLE & output_PREADY) begin
      ready1 <= 1;
      input_PRDATA <= output_PRDATA;
      input_PSLVERROR <= output_PSLVERROR;
    end else if (~(output_PSEL & enable_buf))
      ready1 <= 0;
  end

  always @(posedge clk_input) begin
    input_PREADY <= ready1 & ~ready2 & ~input_PREADY;
    if (~ready1)
      ready2 <= 0;
    else if (input_PREADY)
      ready2 <= 1;
  end

endmodule
