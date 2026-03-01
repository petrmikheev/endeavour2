package endeavour2

import spinal.core._
import spinal.core.internals.{MemTopology, PhaseContext, PhaseNetlist}

object MemBlackboxing {
  // Note: Ram_2wrs is a workaround for a bug in Efinity.
  // With default SpinalHDL instantiation of true-dual-port RAM Efinity for some reason ignores write mask.
  //
  // Limitations: Both ports mask have same settings. readUnderWrite and duringWrite settings are ignored.
  val ram2wrs= """module Ram_2wrs #(
                 |        parameter integer wordCount = 0,
                 |        parameter integer wordWidth = 0,
                 |        parameter clockCrossing = 1'b0,
                 |        parameter technology = "auto",
                 |        parameter portA_readUnderWrite = "dontCare",
                 |        parameter portA_duringWrite = "dontCare",
                 |        parameter integer portA_addressWidth = 0,
                 |        parameter integer portA_dataWidth = 0,
                 |        parameter integer portA_maskWidth = 0,
                 |        parameter portA_maskEnable = 1'b0,
                 |        parameter portB_readUnderWrite = "dontCare",
                 |        parameter portB_duringWrite = "dontCare",
                 |        parameter integer portB_addressWidth = 0,
                 |        parameter integer portB_dataWidth = 0,
                 |        parameter integer portB_maskWidth = 0,
                 |        parameter portB_maskEnable = 1'b0
                 |    )(
                 |        input portA_clk,
                 |        input portA_en,
                 |        input portA_wr,
                 |        input [portA_maskWidth-1:0] portA_mask,
                 |        input [portA_addressWidth-1:0] portA_addr,
                 |        input [portA_dataWidth-1:0] portA_wrData,
                 |        output [portA_dataWidth-1:0] portA_rdData,
                 |        input portB_clk,
                 |        input portB_en,
                 |        input portB_wr,
                 |        input [portB_maskWidth-1:0] portB_mask,
                 |        input [portB_addressWidth-1:0] portB_addr,
                 |        input [portB_dataWidth-1:0] portB_wrData,
                 |        output [portB_dataWidth-1:0] portB_rdData
                 |    );
                 |
                 |    reg [portA_dataWidth-1:0] ram_block [(2**portA_addressWidth)-1:0];
                 |    integer i;
                 |
                 |    localparam COL_WIDTH = portA_dataWidth/portA_maskWidth;
                 |    reg [portA_dataWidth-1:0] a_rd_data;
                 |    reg [portA_dataWidth-1:0] b_rd_data;
                 |    always @ (posedge portA_clk) begin
                 |        a_rd_data <= ram_block[portA_addr];
                 |        if(portA_en & portA_wr) begin
                 |            for(i=0;i<portA_maskWidth;i=i+1) begin
                 |                if(portA_mask[i]) begin // byte-enable
                 |                    ram_block[portA_addr][i*COL_WIDTH +: COL_WIDTH] <= portA_wrData[i*COL_WIDTH +:COL_WIDTH];
                 |                end
                 |            end
                 |        end
                 |    end
                 |    always @ (posedge portB_clk) begin
                 |        b_rd_data <= ram_block[portB_addr];
                 |        if(portB_en & portB_wr) begin
                 |            for(i=0;i<portB_maskWidth;i=i+1) begin
                 |                if(portB_mask[i]) begin // byte-enable
                 |                    ram_block[portB_addr][i*COL_WIDTH +: COL_WIDTH] <= portB_wrData[i*COL_WIDTH +:COL_WIDTH];
                 |                end
                 |            end
                 |        end
                 |    end
                 |    assign portA_rdData = a_rd_data;
                 |    assign portB_rdData = b_rd_data;
                 |endmodule""".stripMargin

  object blackboxPolicy extends MemBlackboxingPolicy{
    override def translationInterest(topology: MemTopology): Boolean = {
      // if(topology.readWriteSync.size > 1) return false  // this line disables Ram_2wrs blackboxing
      if(topology.writes.exists(e => e.mask != null && e.getSymbolWidth == 8) && topology.mem.initialContent == null) return true
      if (topology.readWriteSync.exists(e => e.mask != null && e.getSymbolWidth == 8) && topology.mem.initialContent == null) return true
      if (topology.readsAsync.size != 0 && topology.mem.initialContent == null) return true
      false
    }

    override def onUnblackboxable(topology: MemTopology, who: Any, message: String): Unit = generateUnblackboxableError(topology, who, message)
  }

  def addMemBlackboxing(sc: SpinalConfig) = {
    sc.addStandardMemBlackboxing(blackboxPolicy)
    sc.memBlackBoxers += new PhaseNetlist {
      override def impl(pc: PhaseContext) = {
        pc.walkComponents{
          case bb: Ram_1w_1rs => bb.setInlineVerilog(Ram_1w_1rs.efinix)
          case bb: Ram_1w_1ra => bb.setInlineVerilog(Ram_1w_1ra.efinix)
          case bb: Ram_2wrs => bb.setInlineVerilog(ram2wrs)
          case _ =>
        }
      }
    }
  }
}
