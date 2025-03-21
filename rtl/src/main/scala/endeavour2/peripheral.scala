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
  addRTLPath("./verilog/i2c.v")
}
