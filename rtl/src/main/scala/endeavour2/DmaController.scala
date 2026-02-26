package endeavour2

import spinal.core._
import spinal.lib._
import spinal.lib.fsm._
import spinal.lib.misc.pipeline._
import spinal.lib.bus.amba3.apb.{Apb3, Apb3Config, Apb3SlaveFactory}
import spinal.lib.bus.tilelink
import spinal.lib.bus.misc.SizeMapping

object DmaOpcode {
  val READ = 0
  val WRITE = 1
  val READ_SYNC = 2
  val WRITE_SYNC = 3
  val SET = 4
  val COPY = 5
  val MIXRGB = 32
}

class DmaController extends Component {
  val io = new Bundle {
    val apb = slave(Apb3(Apb3Config(
      addressWidth  = 4,
      dataWidth     = 32,
      useSlaveError = false
    )))
    val tl_bus = master(tilelink.Bus(tilelink.BusParameter(
      addressWidth = 30,
      dataWidth    = 64,
      sizeBytes    = 64,
      sourceWidth  = 2,
      sinkWidth    = 0,
      withBCE      = false,
      withDataA    = true,
      withDataB    = false,
      withDataC    = false,
      withDataD    = true,
      node         = null
    )))
    val interrupt = out Bool()
  }

  // *** Buffer

  val buffer = Mem(Bits(64 bits), wordCount = 1024)

  val argAddrS = Stream(UInt(10 bits))
  val argDataS = Flow(Bits(128 bits))
  val arg2Buf = Reg(Bits(64 bits))
  val writeResS = Flow(new Bundle {
    val addr = UInt(10 bits)
    val data = Bits(64 bits)
    val mask = Bits(8 bits)
  })
  val fetchAddr = UInt(10 bits)
  val fetchS = Stream(Bits(64 bits))

  val arg1Addr, arg2Addr, resAddr = Reg(UInt(10 bits))
  val arg2Turn = Reg(Bool())
  val has_arg2 = Bool()
  when (argAddrS.fire) {
    when (arg2Turn) {
      arg2Addr := arg2Addr + 1
      arg2Turn := False
    } otherwise {
      arg1Addr := arg1Addr + 1
      arg2Turn := has_arg2
    }
  }
  when (writeResS.fire) { resAddr := resAddr + 1 }
  argAddrS.payload := Mux(arg2Turn, arg2Addr, arg1Addr)
  writeResS.payload.addr := resAddr

  val bufPort1 = new Area {
    val addr = RegNext(Mux(writeResS.valid, writeResS.payload.addr, Mux(argAddrS.valid, argAddrS.payload, fetchAddr)))
    val wdata = RegNext(writeResS.payload.data)
    val en = RegNext(writeResS.valid | argAddrS.valid | fetchS.ready)
    val writeEn = RegNext(writeResS.valid)
    val mask = RegNext(writeResS.payload.mask)
    val rdata = buffer.readWriteSync(addr, wdata, en, writeEn, mask)
    val rdataFire = RegNext(RegNext(argAddrS.fire))
    val rdataArg2 = RegNext(RegNext(arg2Turn))
    argAddrS.ready := ~writeResS.valid
    when (rdataFire) { arg2Buf := rdata }
    argDataS.payload := arg2Buf ## rdata
    argDataS.valid := rdataFire & ~rdataArg2
    fetchS.valid := RegNext(RegNext(~writeResS.valid & ~argAddrS.valid & fetchS.ready))
    fetchS.payload := rdata
  }

  val tlDataOutS = Stream(Bits(64 bits))
  val tlDataInS = Flow(new Bundle {
    val addr = UInt(10 bits)
    val data = Bits(64 bits)
  })

  val tlDataOutAddr = Reg(UInt(10 bits))
  val tlDataOutValid = Reg(Bits(2 bits))

  val bufPort2 = new Area {
    val out_addr = tlDataOutAddr + tlDataOutValid(0).asUInt + tlDataOutValid(1).asUInt
    val readNext = tlDataOutS.ready & ~tlDataInS.valid
    tlDataOutValid := readNext ## (tlDataOutValid(1) & (~tlDataOutValid(0) | tlDataOutS.ready))
    val addr = RegNext(Mux(tlDataInS.valid, tlDataInS.payload.addr, out_addr))
    val wdata = RegNext(tlDataInS.payload.data)
    val en = RegNext(tlDataInS.valid | tlDataOutS.ready)
    val writeEn = RegNext(tlDataInS.valid)
    val rdata = buffer.readWriteSync(addr, wdata, en, writeEn)
    tlDataOutS.payload := rdata
    tlDataOutS.valid := tlDataOutValid(0)
    when (tlDataOutS.fire) { tlDataOutAddr := tlDataOutAddr + 1 }
  }

  // *** TileLink

  val tl_id_queue = Reg(Vec.fill(4)(UInt(2 bits)))
  for (i <- 0 to 3) { tl_id_queue(i) init(i) }
  val tl_id_head = RegInit(U(0, 2 bits))
  val tl_id_tail = RegInit(U(0, 2 bits))
  val tl_id_busy = RegInit(B(0, 4 bits))
  val tl_id_prev_busy = RegInit(B(0, 4 bits))

  val tl_d_AccessAckData = io.tl_bus.d.payload.opcode.asBits(0)
  val d_beat_counter = RegInit(U(0, 3 bits))
  when (io.tl_bus.d.fire & tl_d_AccessAckData) { d_beat_counter := d_beat_counter + 1 }
  val d_last = io.tl_bus.d.fire & (d_beat_counter === 7 | ~tl_d_AccessAckData)
  when (d_last) {
    tl_id_queue(tl_id_tail) := io.tl_bus.d.payload.source
    tl_id_tail := tl_id_tail + 1
    tl_id_busy(io.tl_bus.d.payload.source) := False
    tl_id_prev_busy(io.tl_bus.d.payload.source) := False
  }

  val mem_addr = Reg(UInt(24 bits))
  val mem_buf_base = Reg(UInt(7 bits))
  val mem_counter = RegInit(U(0, 7 bits))
  val mem_write = Reg(Bool())
  val mem_buf_addr = Reg(Vec.fill(4)(UInt(7 bits)))

  val tl_a_source = Reg(UInt(2 bits))
  io.tl_bus.a.payload.source := tl_a_source
  io.tl_bus.a.payload.opcode := Mux(mem_write, tilelink.Opcode.A.PUT_FULL_DATA, tilelink.Opcode.A.GET)
  io.tl_bus.a.payload.address := (mem_addr ## B(0, 6 bits)).asUInt
  io.tl_bus.a.payload.param := 2 // non-standart feature of tilelink.coherent.Cache - on miss propagate transaction down without allocating cache line
  io.tl_bus.a.payload.size := 6 // 2**6 = 64 bytes
  io.tl_bus.a.valid := False

  io.tl_bus.d.ready := True
  tlDataInS.valid := io.tl_bus.d.valid & tl_d_AccessAckData
  tlDataInS.payload.data := io.tl_bus.d.payload.data
  tlDataInS.payload.addr := (mem_buf_addr(io.tl_bus.d.payload.source) ## d_beat_counter).asUInt

  tlDataOutS.ready := False
  io.tl_bus.a.payload.data := tlDataOutS.payload
  io.tl_bus.a.payload.mask := 0xff

  val a_beat_counter = RegInit(U(0, 3 bits))

  val tla_fsm = new StateMachine {
    val Idle : State = new State with EntryPoint {
      whenIsActive {
        when (mem_counter.orR & ~tl_id_busy.andR) {
          tl_a_source := tl_id_queue(tl_id_head)
          tl_id_head := tl_id_head + 1
          when (mem_write) { goto(Write) } otherwise { goto(Read) }
        }
      }
    }
    val Read : State = new State {  // Get 4 -> AccessAckData 1
      whenIsActive {
        io.tl_bus.a.valid := True
        tl_id_busy(tl_a_source) := True
        mem_buf_addr(tl_a_source) := mem_buf_base
        when (io.tl_bus.a.fire) {
          mem_addr := mem_addr + 1
          mem_buf_base := mem_buf_base + 1
          mem_counter := mem_counter - 1
          goto(Idle)
        }
      }
    }
    val Write : State = new State { // PutFullData 0 -> AccessAck 0
      whenIsActive {
        io.tl_bus.a.valid := tlDataOutS.valid
        tlDataOutS.ready := io.tl_bus.a.ready
        tl_id_busy(tl_a_source) := True
        when (io.tl_bus.a.fire) {
          when (a_beat_counter === 7) {
            mem_addr := mem_addr + 1
            mem_counter := mem_counter - 1
            when (mem_counter === 1 || tl_id_busy.andR) {
              goto(Idle)
            } otherwise {
              tl_a_source := tl_id_queue(tl_id_head)
              tl_id_head := tl_id_head + 1
            }
          }
          a_beat_counter := a_beat_counter + 1
        }
      }
    }
  }

  val I = new Area {
    val instr = Reg(Bits(64 bits))
    val opcode = instr(63 downto 58)
    val b_from = instr(57 downto 45).asUInt
    val b_to = instr(44 downto 32).asUInt
    val b_arg = instr(12 downto 0).asUInt - b_from(2 downto 0)
    val b_arg2 = instr(25 downto 13).asUInt
    has_arg2 := opcode(5)
    val d32 = instr(31 downto 0)

    val shift = Reg(UInt(3 bits))
    val shift_mask = Reg(Bits(8 bits))
    val wmask_first = Reg(Bits(8 bits))
    val wmask_last = Reg(Bits(8 bits))
    val cmdSet = Reg(Bool())
    val cmdMixrgb = Reg(Bool())
  }

  // *** Pipeline

  val runPipeline = False
  val work_counter = Reg(UInt(10 bits))
  val first = Reg(Bool())

  val bldr = new NodesBuilder()

  val FIRST = Payload(Bool())
  val LAST = Payload(Bool())
  val DUMMY = Payload(Bool())
  val RAW_ARG = Payload(Bits(64 bits))
  val ARG1 = Payload(Bits(64 bits))
  val ARG2 = Payload(Bits(64 bits))
  val SHIFTED = Payload(Bits(64 bits))
  val RES = Payload(Bits(64 bits))

  argAddrS.valid := runPipeline
  val N0 = new bldr.Node {
    valid := (argDataS.valid | I.cmdSet) & runPipeline
    RAW_ARG := Mux(I.cmdSet, I.d32 #* 2, argDataS.payload(63 downto 0))
    ARG2 := argDataS.payload(127 downto 64)
    FIRST := first
    when (isFiring) { first := False }
  }

  val Ns = new bldr.Node {
    SHIFTED := RAW_ARG.rotateRight(I.shift<<3)
  }

  val Nbuf = new bldr.Node {
    LAST := work_counter === 1
    DUMMY := work_counter === 0
    when (isFiring & ~DUMMY) {
      work_counter := work_counter - 1
    }
  }

  val Narg = new bldr.Node {
    for (i <- 0 to 7) {
      ARG1(i*8 + 7 downto i*8) := Mux(I.shift_mask(i), SHIFTED(i*8 + 7 downto i*8), Nbuf(SHIFTED)(i*8 + 7 downto i*8))
    }
  }

  val Nr = new bldr.Node {
    when (I.cmdMixrgb) {
      for (i <- 0 to 3) {
        val b = i * 16
        RES((b+ 4) downto (b+ 0)) := ((ARG1((b+ 4) downto (b+ 0)).asUInt +^ ARG2((b+ 4) downto (b+ 0)).asUInt) >> 1).asBits;
        RES((b+10) downto (b+ 5)) := ((ARG1((b+10) downto (b+ 5)).asUInt +^ ARG2((b+10) downto (b+ 5)).asUInt) >> 1).asBits;
        RES((b+15) downto (b+11)) := ((ARG1((b+15) downto (b+11)).asUInt +^ ARG2((b+15) downto (b+11)).asUInt) >> 1).asBits;
      }
    } otherwise {
      RES := ARG1
    }
    ready := Narg.valid
    writeResS.data := RES
    writeResS.mask := Mux(FIRST, I.wmask_first, B(0xff)) & Mux(LAST, I.wmask_last, B(0xff))
    writeResS.valid := isFiring & ~DUMMY
  }

  bldr.genStagedPipeline()

  // *** Command FSM

  val instr_addr = Reg(UInt(24 bits))
  val instr_counter = Reg(UInt(16 bits))
  val interrupt_en = RegInit(False)
  val instr_baddr = Reg(UInt(4 bits))
  val instr_next_read = RegInit(False)

  fetchAddr := (B"111111" ## instr_baddr).asUInt
  fetchS.ready := False

  val startProcessing = False
  val isIdle = False

  val apb = Apb3SlaveFactory(io.apb)
  apb.readAndWrite(instr_addr, address = 0, bitOffset = 6)
  apb.readAndWrite(instr_counter, address = 4)
  apb.write(interrupt_en, address = 8)
  apb.read(isIdle, address = 8)
  apb.onWrite(4)( startProcessing := True )

  val fsm = new StateMachine {
    val Idle : State = new State with EntryPoint {
      whenIsActive {
        isIdle := True
        when (startProcessing) {
          I.instr(63 downto 58) := DmaOpcode.READ_SYNC
          I.instr(57 downto 45) := B(8192 - 128, 13 bits)
          I.instr(44 downto 32) := Mux(io.apb.PWDATA(15 downto 3).orR, B(0, 13 bits), B(8192 - 64, 13 bits))
          I.instr(29 downto 6) := instr_addr.asBits
          instr_addr := instr_addr + 2
          instr_baddr := 0
          instr_next_read := False
          goto(Parse)
        }
      }
    }
    val Fetch : State = new State {
      whenIsActive {
        fetchS.ready := True
        when (instr_counter === 0) {
          when (mem_counter === 0 && tl_id_busy === 0) { goto(Idle) }
        } elsewhen (instr_baddr(2 downto 0) === 0 && instr_next_read) {
          I.instr(63 downto 58) := DmaOpcode.READ
          I.instr(57 downto 45) := Mux(instr_baddr(3), B(8192 - 128, 13 bits), B(8192 - 64, 13 bits))
          I.instr(44 downto 32) := Mux(instr_baddr(3), B(8192 - 64, 13 bits), B(0, 13 bits))
          I.instr(29 downto 6) := instr_addr.asBits
          instr_addr := instr_addr + 1
          instr_next_read := False
          goto(Parse)
        } elsewhen (fetchS.valid) {
          when (instr_baddr(2 downto 0).andR) { instr_next_read := True }
          I.instr := fetchS.payload
          instr_baddr := instr_baddr + 1
          instr_counter := instr_counter - 1
          goto(Parse)
        }
      }
    }
    val Parse : State = new State {
      whenIsActive {
        I.shift := Mux(I.opcode === DmaOpcode.SET, U(0, 3 bits), I.b_arg(2 downto 0))
        I.shift_mask := B(0xff) |>> I.b_arg(2 downto 0)
        I.wmask_first := B(0xff)|<< I.b_from(2 downto 0)
        I.wmask_last := B(0xff) |>> (U(0, 3 bits) - I.b_to(2 downto 0))
        I.cmdSet := I.opcode === DmaOpcode.SET
        I.cmdMixrgb := I.opcode === DmaOpcode.MIXRGB
        resAddr := I.b_from(12 downto 3)
        arg1Addr := I.b_arg(12 downto 3)
        arg2Addr := I.b_arg2(12 downto 3)
        arg2Turn := has_arg2
        work_counter := (I.b_to + 7)(12 downto 3) - I.b_from(12 downto 3)
        when (I.opcode === M"0000--") {
          when (mem_counter === 0 & ~d_last) { goto(MemOp) }
        } otherwise {
          goto(DoWork)
        }
      }
    }
    val DoWork : State = new State {
      onEntry { first := True }
      whenIsActive {
        runPipeline := True
        when (Nr.isFiring & Nr(LAST)) { goto(Fetch) }
      }
    }
    val MemOp : State = new State {
      onEntry {
        mem_addr := I.d32(29 downto 6).asUInt
        mem_buf_base := I.b_from(12 downto 6)
        mem_counter := I.b_to(12 downto 6) - I.b_from(12 downto 6)
        mem_write := I.opcode(0)
        tl_id_prev_busy := tl_id_busy
        tlDataOutValid := B(0, 2 bits)
        tlDataOutAddr := I.b_from(12 downto 3)
      }
      whenIsActive {
        when (I.opcode(1)) {
          when (mem_counter === 0 && tl_id_busy === 0) { goto(Fetch) }
        } otherwise {
          when (tl_id_prev_busy === 0) { goto(Fetch) }
        }
      }
    }
  }

  io.interrupt := interrupt_en & isIdle
}

class DmaControllerFiber extends Area {
  val core = new DmaController()
  val dma = tilelink.fabric.Node.down()
  val apb = core.io.apb
  val interrupt = core.io.interrupt

  dma.setDownConnection { (down, up) =>
    down.a << up.a.s2mPipe().m2sPipe().m2sPipe()
    up.d << down.d.m2sPipe()
  }

  val dma_fiber = fiber.Fiber build new Area {
    dma.m2s forceParameters tilelink.M2sParameters(
      addressWidth = 30,
      dataWidth = 64,
      masters = List(
        tilelink.M2sAgent(
          name = core,
          mapping = List(
            tilelink.M2sSource(
              id = SizeMapping(0, 4),
              emits = tilelink.M2sTransfers(get = tilelink.SizeRange(64, 64), putFull = tilelink.SizeRange(64, 64))
            )
          )
        )
      )
    )
    dma.s2m.supported load tilelink.S2mSupport.none()
    dma.bus << core.io.tl_bus
  }
}
