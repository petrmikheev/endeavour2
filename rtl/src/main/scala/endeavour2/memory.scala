package endeavour2

import spinal.core._
import spinal.lib._
import spinal.lib.bus.tilelink
import spinal.lib.bus.misc.SizeMapping
import spinal.lib.system.tag.PMA
import spinal.lib.bus.tilelink.coherent.OrderingCmd
import spinal.lib.pipeline._

case class Ddr3_Phy() extends Bundle {
  val tdqss_clk = in Bool()
  val core_clk = in Bool()
  val tac_clk = in Bool()
  val twd_clk = in Bool()

  val tac_shift = out Bits(3 bits)
  val tac_shift_sel = out Bits(5 bits)
  val tac_shift_ena = out Bool()

  val reset = out Bool()
  val cs = out Bool()
  val ras = out Bool()
  val cas = out Bool()
  val we = out Bool()
  val cke = out Bool()
  val addr = out Bits(16 bits)
  val ba = out Bits(3 bits)
  val odt = out Bool()
  val dm_HI = out Bits(2 bits)
  val dm_LO = out Bits(2 bits)
  val dqs_IN_HI = in Bits(2 bits)
  val dqs_IN_LO = in Bits(2 bits)
  val dqs_OUT_HI = out Bits(2 bits)
  val dqs_OUT_LO = out Bits(2 bits)
  val dqs_OE = out Bits(2 bits)
  val dqs_OEN = out Bits(2 bits)
  val dq_IN_HI = in Bits(16 bits)
  val dq_IN_LO = in Bits(16 bits)
  val dq_OUT_HI = out Bits(16 bits)
  val dq_OUT_LO = out Bits(16 bits)
  val dq_OE = out Bits(16 bits)

  def not_connected() = {
    cs := True
    List(reset, cke, ras, cas, we, odt, tac_shift_ena).foreach(v => v := False)
    List(addr, ba, dm_HI, dm_LO, dqs_OUT_HI, dqs_OUT_LO, dqs_OE, dqs_OEN,
         dq_OUT_HI, dq_OUT_LO, dq_OE, tac_shift, tac_shift_sel).foreach(v => v := 0)
  }
}

case class Ddr3_Native() extends Bundle with IMasterSlave {
  val wr_en = in Bool()
  val wr_addr_en = in Bool()
  val wr_addr = in UInt(32 bit)
  val wr_datamask = in Bits(16 bits)
  val wr_data = in Bits(128 bits)
  val wr_busy = out Bool()
  val wr_ack = out Bool()

  val rd_addr_en = in Bool()
  val rd_addr = in UInt(32 bit)
  val rd_ack = out Bool()
  val rd_busy = out Bool()
  val rd_en = in Bool()
  val rd_valid = out Bool()
  val rd_data = out Bits(128 bit)

  override def asMaster(): Unit = {
    out(wr_en, wr_addr_en, wr_addr, wr_data, wr_datamask)
    in(wr_busy, wr_ack)
    out(rd_addr_en, rd_addr, rd_en)
    in(rd_ack, rd_busy, rd_valid, rd_data)
  }
}

class Ddr3Controller extends BlackBox {
  val io = new Bundle {
    val clk = in Bool()
    val reset_n = in Bool()

    val phy = Ddr3_Phy()
    val native = slave(Ddr3_Native())

    val cal_ena = in Bool()
    val cal_done = out Bool()
    val cal_pass = out Bool()
    val cal_fail_log = out Bits(8 bits)
  }
  noIoPrefix()
  addRTLPath("./verilog/ddr3_controller_sim.v")

  private def renameIO(): Unit = {
    io.flatten.foreach(bt => {
      bt.setName(bt.getName()
          .replace("phy_tac_shift", "shift")
          .replace("phy_", "")
          .replace("dm_HI", "o_dm_hi").replace("dm_LO", "o_dm_lo")
          .replace("dqs_IN_HI", "i_dqs_hi").replace("dqs_IN_LO", "i_dqs_lo")
          .replace("dqs_OUT_HI", "o_dqs_hi").replace("dqs_OUT_LO", "o_dqs_lo")
          .replace("dqs_OEN", "o_dqs_n_oe").replace("dqs_OE", "o_dqs_oe")
          .replace("dq_IN_HI", "i_dq_hi").replace("dq_IN_LO", "i_dq_lo")
          .replace("dq_OUT_HI", "o_dq_hi").replace("dq_OUT_LO", "o_dq_lo")
          .replace("dq_OE", "o_dq_oe")
          .replace("native_", "")
      )
    })
  }

  addPrePopTask(() => renameIO())
}

class Ddr3Adapter(p: tilelink.BusParameter) extends BlackBox {
  addGeneric("ADDRESS_WIDTH", p.addressWidth)
  addGeneric("SOURCE_WIDTH", p.sourceWidth)
  val io = new Bundle {
    val clk = in Bool()
    val reset = in Bool()
    val calibration_done = in Bool()
    val native = master(Ddr3_Native())
    val tl = slave(tilelink.Bus(tilelink.BusParameter(
      addressWidth = p.addressWidth,
      dataWidth    = 64,
      sizeBytes    = 64,
      sourceWidth  = p.sourceWidth,
      sinkWidth    = 0,
      withBCE      = false,
      withDataA    = true,
      withDataB    = false,
      withDataC    = false,
      withDataD    = true,
      node         = null
    )))
  }
  noIoPrefix()
  mapClockDomain(clock=io.clk, reset=io.reset)
  addRTLPath("./verilog/ddr3_adapter.v")

  private def renameIO(): Unit = {
    io.flatten.foreach(bt => {
      bt.setName(bt.getName().replace("payload_", ""))
    })
  }

  addPrePopTask(() => renameIO())
}

class Ddr3Fiber(val bytes : BigInt, phy : Ddr3_Phy) extends Area {
  val up = tilelink.fabric.Node.up()
  up.addTag(PMA.MAIN)
  up.addTag(PMA.EXECUTABLE)
  //up.addTag(new system.tag.MemoryEndpointTag(SizeMapping(0, bytes)))
  up.forceDataWidth(64)

  val ddr_ctrl = new Ddr3Controller()

  ddr_ctrl.io.clk := ClockDomain.current.clock
  ddr_ctrl.io.reset_n := ~ClockDomain.current.reset
  ddr_ctrl.io.phy <> phy
  ddr_ctrl.io.cal_ena := True

  def cal_stat = (ddr_ctrl.io.cal_fail_log, B(0, 2 bits), ddr_ctrl.io.cal_pass, ddr_ctrl.io.cal_done).asBits

  fiber.Handle {
    val transfers = tilelink.M2sTransfers(
      get        = tilelink.SizeRange(64, 64),
      putFull    = tilelink.SizeRange(64, 64)
    )
    up.m2s.supported load up.m2s.proposed.intersect(transfers).copy(addressWidth = log2Up(bytes))
    up.s2m.none()

    val ddr_adapter = new Ddr3Adapter(up.bus.p)
    ddr_adapter.io.native <> ddr_ctrl.io.native
    ddr_adapter.io.tl <> up.bus
    ddr_adapter.io.calibration_done := ddr_ctrl.io.cal_done
  }
}

// Copied from spinal.lib.bus.tilelink.Ram with a modification to ignore writes.
// Small Rom uses less memory blocks than Ram since it doesn't need write port.
class Rom(p : tilelink.NodeParameters, bytes : Int) extends Component {
  val io = new Bundle{
    val up = slave port tilelink.Bus(p)
  }

  val mem = Mem.fill(bytes/p.m.dataBytes)(Bits(p.m.dataWidth bits))
  val port_address = cloneOf(mem.addressType)
  val port_enable = Bool()
  val port_rdata = mem.readSync(port_address, port_enable)

  val pipeline = new Pipeline{
    val cmd = new Stage{
      val IS_GET = insert(tilelink.Opcode.A.isGet(io.up.a.opcode))
      val SIZE = insert(io.up.a.size)
      val SOURCE = insert(io.up.a.source)
      val LAST = insert(True)

      valid := io.up.a.valid
      io.up.a.ready := isReady

      val addressShifted = (io.up.a.address >> log2Up(p.m.dataBytes))
      port_enable := isFireing

      val withFsm = io.up.p.beatMax != 1
      if (!withFsm) port_address := addressShifted
      val fsm = withFsm generate new Area {
        val counter = Reg(io.up.p.beat) init (0)
        val address = Reg(mem.addressType)
        val size = Reg(io.up.p.size)
        val source = Reg(io.up.p.source)
        val isGet = Reg(Bool())
        val busy = counter =/= 0
        when(busy && isGet) {
          io.up.a.ready := False
          valid := True
        }

        when(io.up.a.fire && !busy){
          size := io.up.a.size
          source := io.up.a.source
          isGet := tilelink.Opcode.A.isGet(io.up.a.opcode)
          address := addressShifted
        }

        LAST clearWhen(counter =/= tilelink.sizeToBeatMinusOne(io.up.p,SIZE))
        when(busy){
          SIZE := size
          SOURCE := source
          IS_GET := isGet
        }
        when(isFireing) {
          counter := counter + 1
          when(LAST) {
            counter := 0
          }
        }
        port_address := busy.mux(address, addressShifted) | counter.resized
      }

    }

    val rsp = new Stage(Connection.M2S()){
      val takeIt = cmd.LAST || cmd.IS_GET
      haltWhen(!io.up.d.ready && takeIt)
      io.up.d.valid := valid && takeIt
      io.up.d.opcode := cmd.IS_GET.mux(tilelink.Opcode.D.ACCESS_ACK_DATA, tilelink.Opcode.D.ACCESS_ACK)
      io.up.d.param := 0
      io.up.d.source := cmd.SOURCE
      io.up.d.size := cmd.SIZE
      io.up.d.denied := False
      io.up.d.corrupt := False
      io.up.d.data := port_rdata
    }
    build()
  }

  val ordering = Flow(OrderingCmd(p.sizeBytes))
  ordering.valid := io.up.a.fire && io.up.a.isLast()
  ordering.debugId := io.up.a.debugId
  ordering.bytes := (U(1) << io.up.a.size).resized
  Component.current.addTag(new tilelink.OrderingTag(ordering.stage()))
}

class RomFiber(val content : Array[Byte]) extends Area {
  val up = tilelink.fabric.Node.up()
  //up.forceDataWidth(16)
  up.addTag(PMA.MAIN)
  up.addTag(PMA.EXECUTABLE)
  val addressWidth = log2Up(content.length)

  val thread = fiber.Fiber build new Area{
    //up.m2s.supported load up.m2s.proposed.intersect(tilelink.M2sTransfers.allGetPut).copy(addressWidth = addressWidth)
    up.m2s.supported load up.m2s.proposed.intersect(tilelink.M2sTransfers(get=tilelink.SizeRange(4, 64))).copy(addressWidth = addressWidth)
    up.s2m.none()

    val logic = new Rom(up.bus.p.node, (1 << addressWidth) toInt)
    logic.io.up << up.bus

    val memWidth = logic.mem.getWidth
    val wordSize = memWidth / 8
    val array = new Array[Bits](logic.mem.wordCount)
    println(s"ROM[${array.length} * ${wordSize} bytes] <= content ${content.length} bytes")
    for (i <- 0 to logic.mem.wordCount - 1) {
      var v = BigInt(0)
      for (j <- (i+1)*wordSize-1 to i*wordSize by -1) {
        if (j < content.length) v = (v << 8) | (content(j).toInt & 0xff)
      }
      array(i) = v
    }
    logic.mem.init(array)
  }
}

class RamFiber(val bytes : BigInt, val initialContent : Option[Array[Byte]] = None) extends Area {
  val up = tilelink.fabric.Node.up()
  up.addTag(PMA.MAIN)
  up.addTag(PMA.EXECUTABLE)

  val thread = fiber.Fiber build new Area {
    up.m2s.supported load up.m2s.proposed.intersect(tilelink.M2sTransfers.allGetPut).copy(addressWidth = log2Up(bytes))
    up.s2m.none()

    val logic = new tilelink.Ram(up.bus.p.node, bytes toInt)
    logic.io.up << up.bus
    val memWidth = logic.mem.getWidth
    val wordSize = memWidth / 8

    initialContent match {
      case Some(content) => {
        val array = new Array[Bits](logic.mem.wordCount)
        println(s"RAM[${array.length} * ${wordSize} bytes] <= initialContent ${content.length} bytes")
        for (i <- 0 to logic.mem.wordCount - 1) {
          var v = BigInt(0)
          for (j <- (i+1)*wordSize-1 to i*wordSize by -1) {
            if (j < content.length) v = (v << 8) | (content(j).toInt & 0xff)
          }
          array(i) = v
        }
        logic.mem.init(array)
      }
      case None => println(s"RAM[${bytes/wordSize} * ${wordSize} bytes]")
    }
  }
}
