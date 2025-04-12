module VideoController #(parameter ADDRESS_WIDTH) (
  input clk,
  input reset,
  input pixel_clk,

  output reg [7:0] data_red,
  output reg [7:0] data_green,
  output reg [7:0] data_blue,
  output           data_enable,
  output reg hSync,
  output reg vSync,

  input   [5:0] apb_PADDR,
  input         apb_PSEL,
  input         apb_PENABLE,
  output        apb_PREADY,
  input         apb_PWRITE,
  input  [31:0] apb_PWDATA,
  output [31:0] apb_PRDATA,

  output reg        tl_bus_a_valid,
  input             tl_bus_a_ready,
  output     [2:0]  tl_bus_a_payload_opcode,
  output     [2:0]  tl_bus_a_payload_param,
  output reg [1:0]  tl_bus_a_payload_source,
  output reg [ADDRESS_WIDTH-1:0] tl_bus_a_payload_address,
  output     [2:0]  tl_bus_a_payload_size,
  input             tl_bus_d_valid,
  output            tl_bus_d_ready,
  input      [2:0]  tl_bus_d_payload_opcode,
  input      [2:0]  tl_bus_d_payload_param,
  input      [1:0]  tl_bus_d_payload_source,
  input      [2:0]  tl_bus_d_payload_size,
  input             tl_bus_d_payload_denied,
  input      [63:0] tl_bus_d_payload_data,
  input             tl_bus_d_payload_corrupt
);

  reg [5:0] hDrawStartO;
  reg [11:0] hDrawEnd, hDrawEndO;
  reg [11:0] hSyncStart, hSyncStartO;
  reg [11:0] hSyncEnd, hSyncEndO;
  reg [11:0] hCharInit;
  reg [11:0] hLast;
  reg hOddMode;

  reg [10:0] vDrawEnd;
  reg [10:0] vSyncStart;
  reg [10:0] vSyncEnd;
  reg [10:0] vLast;

  reg show_text;
  reg show_graphic;
  reg use_graphic_alpha;
  reg hSyncInv, vSyncInv;
  reg [4:0] hOffset, hOffsetNext;
  reg [3:0] font_height;
  localparam font_width = 3'd7; // 8 pixels
  reg [31:6] text_addr, text_addr_next;
  reg [31:6] graphic_addr, graphic_addr_next;
  reg [3:0] vTextOffset;
  reg [7:0] hTextOffset;
  reg [31:0] frame_number;
  reg odd_frame, odd_frame_buf;

  reg [10:0] charmap_index;

  // CHARMAP_SIZE = 512: symbols with codes 8-127 (ASCII)
  // CHARMAP_SIZE = 1024: symbols with codes 8-255
  // CHARMAP_SIZE = 2048: symbols with codes 8-255, two fonts
  localparam CHARMAP_SIZE = 2048;

  reg [31:0] charmap [CHARMAP_SIZE-1:0];  // 8 x 16x512 blocks
  reg [63:0] text_line [255:0];           // 4 x 16x512 blocks
  reg [63:0] graphic_line [511:0];        // 4 x 16x512 blocks

  localparam PIXEL_DELAY = 2'd3;

  // *** APB interface

  reg [3:0] apb_reg;

  always @(posedge clk) begin
    apb_reg <= apb_PADDR[5:2];
    if (reset) begin
      show_text <= 0;
      show_graphic <= 0;
      use_graphic_alpha <= 0;
      vTextOffset <= 0;
      hTextOffset <= 0;
      frame_number <= 0;
    end else begin
      odd_frame_buf <= odd_frame;
      if (frame_number[0] != odd_frame) frame_number <= frame_number + 1'b1;
      if (apb_PSEL & apb_PENABLE & apb_PWRITE) begin
        case (apb_reg)
          4'h0: {hSyncInv, vSyncInv} <= apb_PWDATA[31:30];
          4'h1: begin hDrawEnd   <= apb_PWDATA[11:0]; hOddMode <= |apb_PWDATA[4:0]; end
          4'h2: hSyncStart <= apb_PWDATA[11:0];
          4'h3: hSyncEnd   <= apb_PWDATA[11:0];
          4'h4: hLast      <= apb_PWDATA[11:0] - 1'd1;
          4'h5: vDrawEnd   <= apb_PWDATA[10:0];
          4'h6: vSyncStart <= apb_PWDATA[10:0];
          4'h7: vSyncEnd   <= apb_PWDATA[10:0];
          4'h8: vLast      <= apb_PWDATA[10:0] - 1'd1;
          4'h9: {font_height, use_graphic_alpha, show_graphic, show_text} <= {apb_PWDATA[7:4], apb_PWDATA[2:0]};
          4'hA: charmap_index <= apb_PWDATA[10:0];
          4'hB: charmap[charmap_index] <= apb_PWDATA;
          4'hC: text_addr_next <= apb_PWDATA[31:6];
          4'hD: {graphic_addr_next, hOffsetNext} <= apb_PWDATA[31:1];
          4'hE: {vTextOffset, hTextOffset} <= apb_PWDATA[11:0];
          default:;
        endcase
      end
    end
  end

  assign apb_PREADY = 1'b1;
  assign apb_PRDATA = apb_reg == 4'h1 ? {20'b0, hDrawEnd} :
                      apb_reg == 4'h5 ? {21'b0, vDrawEnd} :
                      apb_reg == 4'h9 ? {24'b0, font_height, 1'b0, use_graphic_alpha, show_graphic, show_text} :
                      apb_reg == 4'hC ? {text_addr_next, 6'd0} :
                      apb_reg == 4'hD ? {graphic_addr_next, hOffsetNext, 1'd0} :
                      apb_reg == 4'hE ? {20'b0, vTextOffset, hTextOffset} :
                      apb_reg == 4'hF ? frame_number :
                                        32'b0;

  // *** Counters

  reg [11:0] hCounter = 0;
  reg [10:0] vCounter = 0;
  reg  [7:0] hCharCounter;
  reg  [7:0] vCharCounter = 0;
  reg  [2:0] char_px;
  reg  [3:0] char_py = 0;
  reg  [3:0] text_read_steps = 1'd1;

  reg hDraw = 0;
  reg vDraw = 0;
  wire DrawArea = hDraw & vDraw;
  assign data_enable = DrawArea;

  reg [2:0] pixel_group_request_counter = 0;
  reg [2:0] pixel_group_done_counter = 0;
  reg pixel_new_line_parity = 0;
  reg pixel_new_line_done_parity = 0;
  reg text_line_request_parity = 0;
  reg text_line_done_parity = 0;
  reg text_load = 1;

  always @(posedge pixel_clk) begin
    if (reset) begin
      odd_frame <= 0;
    end else begin
      if (hCounter == hLast) begin
        hCounter <= 0;
        if (vCounter >= vLast) begin
          vCounter <= 0;
          odd_frame <= ~odd_frame;
          graphic_addr <= graphic_addr_next;
          text_addr <= text_addr_next;
          hOffset <= hOffsetNext;
          hDrawStartO <= hOffsetNext + PIXEL_DELAY;
          hDrawEndO <= hDrawEnd + hOffsetNext + PIXEL_DELAY;
          hSyncStartO <= hSyncStart + hOffsetNext + PIXEL_DELAY;
          hSyncEndO <= hSyncEnd + hOffsetNext + PIXEL_DELAY;
        end else begin
          vCounter <= vCounter + 1'd1;
          if (vCounter == 0) vDraw <= 1;
          if (vCounter == vDrawEnd) begin vDraw <= 0; text_load <= 0; end
          if (vCounter == vSyncStart) vSync <= ~vSyncInv;
          if (vCounter == vSyncEnd) vSync <= vSyncInv;
        end
      end else begin
        hCounter <= hCounter + 1'd1;
        if (show_graphic && hDraw && &hCounter[6:0] && vCounter < vDrawEnd) pixel_group_request_counter <= pixel_group_request_counter + 1'b1;
        if (show_graphic && vDraw && hCounter == 1'd1 && (|hOffset || hOddMode)) pixel_new_line_parity <= ~pixel_new_line_parity;
        if (hCounter == hDrawStartO) hDraw <= 1;
        if (hCounter == hDrawEndO) begin
          hDraw <= 0;
          text_read_steps <= ((hCharCounter[7:2] - 1'd1)>>3) + 1'd1;
        end
        if (hCounter == hSyncStartO) hSync <= ~hSyncInv;
        if (hCounter == hSyncEndO) begin
          hSync <= hSyncInv;
          if (vCounter == vLast - vTextOffset - 3'd4) begin
            text_load <= 1;
            vCharCounter <= 0;
            char_py <= font_height - 3'd4;
            hCharInit <= hOffsetNext >= font_width + hTextOffset + 2'd2 ? hOffsetNext - (font_width + hTextOffset + 2'd2) : hLast + hOffsetNext - font_width - hTextOffset - 1'b1;
          end
        end
      end
      if (hCounter == hCharInit && hCounter != hSyncEndO) begin
        hCharCounter <= 0;
        char_px <= 0;
        if (char_py == font_height) begin
          char_py <= 0;
          vCharCounter <= vCharCounter + 1'b1;
        end else
          char_py <= char_py + 1'b1;
      end else begin
        if (char_px == font_width) begin
          char_px <= 0;
          hCharCounter <= hCharCounter + 1'b1;
          if (show_text && text_load && hCharCounter == 1'd1) text_line_request_parity <= ~text_line_request_parity;
        end else
          char_px <= char_px + 1'b1;
      end
    end
  end

  // *** RAM interface

  assign tl_bus_a_payload_opcode = 3'd4; // GET
  assign tl_bus_a_payload_param = 3'd0;  // 0
  assign tl_bus_a_payload_size = 3'd6;   // 64 bytes
  assign tl_bus_d_ready = 1'b1;

  reg [2:0] tl_request_count = 0;
  reg [5:0] tl_beat_count = 0;

  reg text_line_sel = 0;
  reg [2:0] text_line_index = 0;
  reg [8:0] graphic_line_index = 0;
  reg text_line_request_parity_buf = 0;
  reg pixel_new_line_parity_buf = 0;
  reg [2:0] pixel_group_request_counter_buf = 0;
  reg [10:0] pixel_load_y = 11'd2047;
  reg [14:0] pixel_group_addr, next_pixel_group_addr;
  reg [17:6] next_text_addr_part;
  reg pixel_loading = 0;
  reg text_loading = 0;
  wire [3:0] char_npy = font_height - char_py;
  reg pixel_new_line;

  always @(posedge clk) begin
    pixel_group_request_counter_buf <= pixel_group_request_counter;
    text_line_request_parity_buf <= text_line_request_parity;
    pixel_new_line_parity_buf <= pixel_new_line_parity;
    pixel_new_line <= pixel_load_y != vCounter;
    next_pixel_group_addr <= pixel_new_line ? {11'(graphic_addr[22:12] + vCounter), graphic_addr[11:8]} : 15'(pixel_group_addr + 1'd1);
    next_text_addr_part <= {8'(text_addr[17:10] + vCharCounter), 4'(text_addr[9:6] + {char_npy[2:0], 1'b0})};
    if (reset) begin
      pixel_loading <= 0;
      text_loading <= 0;
      tl_bus_a_valid <= 0;
      tl_request_count <= 0;
      tl_beat_count <= 0;
    end if (pixel_loading | text_loading) begin
      if (tl_bus_a_valid & tl_bus_a_ready) begin
        tl_request_count <= tl_request_count - 1'b1;
        tl_bus_a_payload_address <= {(ADDRESS_WIDTH-1)'(tl_bus_a_payload_address[ADDRESS_WIDTH-1:0] + 32'd64)};
        tl_bus_a_payload_source <= tl_bus_a_payload_source + 1'b1;
        if (tl_request_count == 1'b1) tl_bus_a_valid <= 1'b0;
      end
      if (tl_bus_d_valid) begin
        tl_beat_count <= tl_beat_count - 1'b1;
        if (tl_beat_count == 1'b1) begin
          text_loading <= 1'b0;
          pixel_loading <= 1'b0;
        end
      end
      if (tl_bus_d_valid & text_loading) begin
        text_line_index <= text_line_index + 1'b1;
        text_line[{text_line_sel, char_npy[2:0], tl_bus_d_payload_source[0], text_line_index}] <= tl_bus_d_payload_data;
      end
      if (tl_bus_d_valid & pixel_loading) begin
        graphic_line_index <= graphic_line_index + 1'b1;
        graphic_line[{graphic_line_index[8:5], tl_bus_d_payload_source[1:0], graphic_line_index[2:0]}] <= tl_bus_d_payload_data;
      end
    end else if (pixel_new_line_parity_buf != pixel_new_line_done_parity) begin
      pixel_new_line_done_parity <= ~pixel_new_line_done_parity;
      pixel_loading <= 1;
      tl_bus_a_valid <= 1'b1;
      tl_request_count <= hOddMode ? 3'd2 : 3'd1;
      tl_beat_count <= hOddMode ? 6'd16 : 6'd8;
      tl_bus_a_payload_source <= 1'b0;
      tl_bus_a_payload_address <= {graphic_addr[ADDRESS_WIDTH-1:23], 15'(pixel_group_addr + 1'd1), graphic_addr[7:6], 6'd0};
    end else if (pixel_group_request_counter_buf != pixel_group_done_counter) begin
      pixel_group_done_counter <= pixel_group_done_counter + 1'b1;
      pixel_loading <= 1;
      tl_bus_a_valid <= 1'b1;
      tl_request_count <= 3'd4; // 4 requests, 64 byte each
      tl_beat_count <= 6'd32; // 32*8 = 256 bytes
      tl_bus_a_payload_source <= 1'b0;
      tl_bus_a_payload_address <= {graphic_addr[ADDRESS_WIDTH-1:23], next_pixel_group_addr, graphic_addr[7:6], 6'd0};
      pixel_group_addr <= next_pixel_group_addr;
      if (pixel_new_line) begin
        pixel_load_y <= vCounter;
        graphic_line_index <= 0;
      end
    end else if (text_line_request_parity_buf != text_line_done_parity) begin
      text_line_done_parity <= ~text_line_done_parity;
      if (char_npy < text_read_steps) begin
        text_loading <= 1;
        tl_bus_a_payload_address <= {text_addr[ADDRESS_WIDTH-1:18], next_text_addr_part, 6'd0};
        tl_bus_a_payload_source <= 1'b0;
        tl_request_count <= 3'd2;
        tl_beat_count <= 6'd16;
        tl_bus_a_valid <= 1'b1;
        text_line_sel <= ~vCharCounter[0];
        text_line_index <= 3'd0;
      end
    end
  end

  // *** Color calculation

  reg [63:0] gword, tword;
  wire [31:0] tword32 = hCharCounter[0] ? tword[63:32] : tword[31:0];
  wire [8:0] tchar = tword32[8:0];
  wire [6:0] tfg_index = tword32[22:16];
  wire [6:0] tbg_index = tword32[30:24];
  reg t_4color_mode;
  reg [10:0] charmap_rindex;
  reg [31:0] charmap_rdata;
  reg [31:0] tfg, tbg, tfg2, tbg2, charmap_word;
  reg [7:0] char_shift, char_shift2;
  reg [31:0] char_fg, char_bg, char_fg2, char_bg2;
  reg char_4color_mode;
  wire [31:0] tcolor_2c = char_shift[7] ? char_fg : char_bg;
  wire [31:0] tcolor_4c = char_shift2[7] ? (char_shift[7] ? char_fg2 : char_bg2) : tcolor_2c;
  wire [31:0] tcolor = char_4color_mode ? tcolor_4c : tcolor_2c;
  wire [15:0] gcolor16 = hCounter[1:0] == 2'b01 ? gword[15:0]  :
                         hCounter[1:0] == 2'b10 ? gword[31:16] :
                         hCounter[1:0] == 2'b11 ? gword[47:32] :
                                                  gword[63:48];
  wire        galpha = use_graphic_alpha ? gcolor16[5] : 1'b0;
  wire [23:0] gcolor24 = {
      /*R*/ gcolor16[15:11], gcolor16[15:13],
      /*G*/ gcolor16[10:6], (use_graphic_alpha ? gcolor16[10:8] : {gcolor16[5], gcolor16[10:9]}),
      /*B*/ gcolor16[4:0], gcolor16[4:2]};
  reg [7:0] gred1, ggreen1, gblue1;
  reg [7:0] gred2, ggreen2, gblue2;
  reg [9:0] diff_r, diff_g, diff_b;
  reg [13:0] mul_r, mul_g, mul_b;
  reg [6:0] alpha1, alpha2;

  always @(posedge pixel_clk) begin
    if (show_graphic) begin
      if (hCounter[1:0] == 2'b00) gword <= graphic_line[hCounter[10:2]];
    end else
      gword <= 0;
    if (show_text) begin
      charmap_rdata <= charmap[charmap_rindex[$clog2(CHARMAP_SIZE)-1:0]];
      if (char_px == 3'd1)
        charmap_rindex <= {4'd0, tfg_index};
      else if (char_px == 3'd2) begin
        charmap_rindex <= {4'd0, tbg_index};
        t_4color_mode <= tword32[31];
      end else if (char_px == 3'd3) begin
        tfg <= charmap_rdata;
        if (t_4color_mode)
          charmap_rindex <= {1'b1, char_py[3] ? tword32[15:8] : tword32[7:0], char_py[2:1]};
        else
          charmap_rindex <= {tchar, char_py[3:2]};
      end else if (char_px == 3'd4) begin
        tbg <= charmap_rdata;
        charmap_rindex <= {4'd0, tfg_index[6:1], 1'b1};
      end else if (char_px == 3'd5) begin
        charmap_word <= charmap_rdata;
        charmap_rindex <= {4'd0, tbg_index[6:1], 1'b1};
      end else if (char_px == 3'd6) begin
        tfg2 <= charmap_rdata;
      end else if (char_px == 3'd7) begin
        tbg2 <= charmap_rdata;
      end
      if (char_px == 0) begin
        tword <= text_line[{vCharCounter[0], hCharCounter[7:1]}];
        if (t_4color_mode) begin
          {char_shift2, char_shift} <= char_py[0] ? charmap_word[31:16] : charmap_word[15:0];
        end else begin
          case (char_py[1:0])
            2'd0: char_shift <= charmap_word[7:0];
            2'd1: char_shift <= charmap_word[15:8];
            2'd2: char_shift <= charmap_word[23:16];
            2'd3: char_shift <= charmap_word[31:24];
          endcase
        end
        char_fg <= tfg;
        char_bg <= tbg;
        char_fg2 <= tfg2;
        char_bg2 <= tbg2;
        char_4color_mode <= t_4color_mode;
      end else begin
        char_shift[7:1] <= char_shift[6:0];
        char_shift2[7:1] <= char_shift2[6:0];
      end
    end else begin
      char_fg <= 0;
      char_bg <= 0;
      char_fg2 <= 0;
      char_bg2 <= 0;
    end

    {gred1, ggreen1, gblue1} <= gcolor24;
    {gred2, ggreen2, gblue2} <= {gred1, ggreen1, gblue1};
    diff_r <= {2'b10, tcolor[31:24]} - gcolor24[23:16];
    diff_g <= {2'b10, tcolor[23:16]} - gcolor24[15:8];
    diff_b <= {2'b10, tcolor[15:8]}  - gcolor24[7:0];
    alpha1 <= show_graphic ? (galpha ? 7'd0 : tcolor[6:0]) : 7'd64;
    alpha2 <= alpha1;
    mul_r <= $unsigned({4'b0, diff_r}) * $unsigned(alpha1);
    mul_g <= $unsigned({4'b0, diff_g}) * $unsigned(alpha1);
    mul_b <= $unsigned({4'b0, diff_b}) * $unsigned(alpha1);
    data_red   <= gred2   + mul_r[13:6] - {alpha2[4:0], 3'b0};
    data_green <= ggreen2 + mul_g[13:6] - {alpha2[4:0], 3'b0};
    data_blue  <= gblue2  + mul_b[13:6] - {alpha2[4:0], 3'b0};
  end

endmodule

