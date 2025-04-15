package endeavour2

import spinal.core._
import spinal.lib._
import spinal.lib.fsm._
import spinal.lib.bus.amba3.apb.{Apb3, Apb3Config, Apb3SlaveFactory}
import spinal.lib.bus.amba4.axi._
import spinal.lib.bus.tilelink

class SpiController extends Component {
  val io = new Bundle {
    val spi_ncs = out Bool()
    val spi_ncs_en = out Bool()
    val spi_clk = out Bool()
    val spi_d0 = out Bool()
    val spi_d1 = in Bool()
    val apb = slave(Apb3(Apb3Config(
      addressWidth  = 4,
      dataWidth     = 32,
      useSlaveError = false
    )))
  }

  val apb = Apb3SlaveFactory(io.apb)

  val writeCnt = RegInit(U(0, 16 bits))
  val readCnt = RegInit(U(0, 16 bits))
  val data = Reg(Bits(32 bits))
  val writeHasData = RegInit(False)
  val readHasData = RegInit(False)

  apb.readAndWrite(writeCnt, address = 0, bitOffset = 16)
  apb.readAndWrite(readCnt, address = 0, bitOffset = 0)
  apb.read(writeHasData, address = 4)
  apb.read(readHasData, address = 8)
  apb.readAndWrite(data, address = 12)
  apb.onWrite(12)( writeHasData := True )
  apb.onRead(12)( readHasData := False )

  val fsm = new StateMachine {
    val data_shift = Reg(Bits(32 bits))
    val counter = RegInit(U(0, 4 bits))
    counter := counter + 1

    io.spi_ncs_en := True
    io.spi_ncs := False
    io.spi_clk := counter(0)
    io.spi_d0 := data_shift(31)

    val Idle : State = new State with EntryPoint {
      whenIsActive {
        io.spi_ncs_en := False
        io.spi_clk := True
        when (writeCnt =/= 0 && counter === 15) { goto(Write) }
      }
    }
    val Write : State = new State {
      onEntry {
        data_shift := data
        writeHasData := False
      }
      whenIsActive {
        when (counter(0)) { data_shift := data_shift(30 downto 0) ## io.spi_d1 }
        when (counter === 15) {
          writeCnt := writeCnt - 1
          when (writeCnt(1 downto 0) === 1) {
            data_shift := data
            writeHasData := False
          }
          when (writeCnt === 1 && readCnt === 0) { goto(Stop) }
          when (writeCnt === 1 && readCnt =/= 0) { goto(Read) }
        }
      }
    }
    val Read : State = new State {
      whenIsActive {
        when (!counter(0)) { data_shift := data_shift(30 downto 0) ## io.spi_d1 }
        when (counter === 15) {
          readCnt := readCnt - 1
          when (readCnt(1 downto 0) === 1) {
            data := data_shift
            readHasData := True
          }
          when (readCnt === 1) { goto(Stop) }
        }
      }
    }
    val Stop : State = new State {
      whenIsActive {
        io.spi_ncs := counter(3)
        io.spi_clk := True
        when (counter === 15) { goto(Idle) }
      }
    }
  }
}

class ApbClockBridge(awidth: Int) extends BlackBox {
  addGeneric("AWIDTH", awidth)
  val apb_conf = Apb3Config(
    addressWidth  = awidth,
    dataWidth     = 32,
    useSlaveError = true
  )
  val io = new Bundle {
    val clk_input = in Bool()
    val clk_output = in Bool()
    val input = slave(Apb3(apb_conf))
    val output = master(Apb3(apb_conf))
  }
  noIoPrefix()
  mapClockDomain(clock=io.clk_input)
  addRTLPath("./verilog/apb_clock_bridge.v")
}

case class UART() extends Bundle {
  val rx = in Bool()
  val tx = out Bool()
}

class UartController extends BlackBox {
  val io = new Bundle {
    val clk = in Bool()
    val reset = in Bool()
    val uart = UART()
    val interrupt = out Bool()
    val apb = slave(Apb3(Apb3Config(
      addressWidth  = 4,
      dataWidth     = 32,
      useSlaveError = false
    )))
  }
  noIoPrefix()
  mapClockDomain(clock=io.clk, reset=io.reset)
  addRTLPath("./verilog/uart_controller.v")
}

class AudioController extends BlackBox {
  val io = new Bundle {
    val clk = in Bool()
    val reset = in Bool()
    val shdn = out Bool()
    val i2c_scl = out Bool()
    val i2c_sda = out Bool()
    val i2c_sda_IN = in Bool()
    val apb = slave(Apb3(Apb3Config(
      addressWidth  = 3,
      dataWidth     = 32,
      useSlaveError = false
    )))
  }
  noIoPrefix()
  mapClockDomain(clock=io.clk, reset=io.reset)
  addRTLPath("./verilog/audio_controller.v")
}

class I2cController extends BlackBox {
  val io = new Bundle {
    val clk = in Bool()
    val reset = in Bool()
    val i2c_scl = out Bool()
    val i2c_sda = out Bool()
    val i2c_sda_IN = in Bool()
    val apb = slave(Apb3(Apb3Config(
      addressWidth  = 4,
      dataWidth     = 32,
      useSlaveError = false
    )))
  }
  noIoPrefix()
  mapClockDomain(clock=io.clk, reset=io.reset)
  addRTLPath("./verilog/i2c.v")
}

case class SdcardPhy() extends Bundle {
  val clk_LO = out Bool()
  val clk_HI = out Bool()

  val cmd_IN_LO = in Bool()
  val cmd_IN_HI = in Bool()
  val cmd_OUT = out Bool()
  val cmd_OE = out Bool()

  val data_IN_LO = in Bits(4 bits)
  val data_IN_HI = in Bits(4 bits)
  val data_OUT = out Bits(4 bits)
  val data_OE = out Bits(4 bits)

  val ndetect = in Bool()
  val vdd_sel_3v3 = out Bool()
}

class SdcardController extends BlackBox {
  val io = new Bundle {
    val clk = in Bool()
    val reset = in Bool()
    val sdcard = SdcardPhy()

    val apb = slave(Apb3(Apb3Config(
      addressWidth  = 5,
      dataWidth     = 32,
      useSlaveError = true
    )))

    val interrupt = out Bool()
  }
  noIoPrefix()
  addRTLPath("./verilog/sdcard_controller.v")
  addRTLPath("./sdspi/rtl/sdio_top.v")
  addRTLPath("./sdspi/rtl/sdio.v")
  addRTLPath("./sdspi/rtl/sdwb.v")
  addRTLPath("./sdspi/rtl/sdckgen.v")
  addRTLPath("./sdspi/rtl/sdcmd.v")
  addRTLPath("./sdspi/rtl/sdtxframe.v")
  addRTLPath("./sdspi/rtl/sdrxframe.v")
  addRTLPath("./sdspi/rtl/sdfrontend.v")
}

class VideoController extends BlackBox {
  addGeneric("ADDRESS_WIDTH", 30)
  val io = new Bundle {
    val clk = in Bool()
    val reset = in Bool()
    val pixel_clk = in Bool()
    val data_red = out Bits(8 bits)
    val data_green = out Bits(8 bits)
    val data_blue = out Bits(8 bits)
    val data_enable = out Bool()
    val hSync = out Bool()
    val vSync = out Bool()
    val apb = slave(Apb3(Apb3Config(
      addressWidth  = 6,
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
      withDataA    = false,
      withDataB    = false,
      withDataC    = false,
      withDataD    = true,
      node         = null
    )))
  }
  noIoPrefix()
  mapClockDomain(clock=io.clk, reset=io.reset)
  addRTLPath("./verilog/video_controller.v")
}
