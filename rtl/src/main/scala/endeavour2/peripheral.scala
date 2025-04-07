package endeavour2

import spinal.core._
import spinal.lib._
import spinal.lib.bus.amba3.apb.{Apb3, Apb3Config}
import spinal.lib.bus.amba4.axi._
import spinal.lib.bus.tilelink

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
  val clk = out Bool()

  val cmd_IN = in Bool()
  val cmd_OUT = out Bool()
  val cmd_OE = out Bool()

  val data_IN = in Bits(4 bits)
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
