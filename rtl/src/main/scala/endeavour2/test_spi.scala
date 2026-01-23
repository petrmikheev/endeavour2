package endeavour2

import spinal.core._
import spinal.core.sim._


object SpiTest extends App {
  val sim = SimConfig.withTimeSpec(1 ns, 1 ps).withWave

  sim.compile(new SpiFifoController()).doSimUntilVoid("test", seed = 42){dut =>
    SimTimeout(60000000L)
    dut.clockDomain.forkStimulus(17000)

    def apbRead(addr: Int): Long = {
      dut.io.apb.PSEL #= 1
      dut.io.apb.PADDR #= addr
      dut.io.apb.PWRITE #= false
      dut.clockDomain.waitSampling()
      dut.io.apb.PENABLE #= true
      dut.clockDomain.waitSamplingWhere(dut.io.apb.PREADY.toBoolean)
      val res = dut.io.apb.PRDATA.toLong
      dut.io.apb.PSEL #= 0
      dut.io.apb.PENABLE #= false
      dut.clockDomain.waitSampling()
      return res
    }

    def apbWrite(addr: Int, v: Long) = {
      dut.io.apb.PSEL #= 1
      dut.io.apb.PADDR #= addr
      dut.io.apb.PWRITE #= true
      dut.clockDomain.waitSampling()
      dut.io.apb.PENABLE #= true
      dut.io.apb.PWDATA #= v
      dut.clockDomain.waitSamplingWhere(dut.io.apb.PREADY.toBoolean)
      dut.io.apb.PSEL #= 0
      dut.io.apb.PENABLE #= false
      dut.io.apb.PWRITE #= false
      dut.clockDomain.waitSampling()
    }

    def spiWrite(v: Int, cpol: Boolean, cpha: Boolean) = {
      val posEdge = cpol != cpha
      var shr = v
      for (i <- 0 to 7) {
        if (!cpha) dut.io.spi_miso #= (shr & 128) != 0
        waitUntil(!dut.io.spi_ncs.toBoolean && dut.io.spi_clk.toBoolean != posEdge);
        waitUntil(!dut.io.spi_ncs.toBoolean && dut.io.spi_clk.toBoolean == posEdge);
        if (cpha) dut.io.spi_miso #= (shr & 128) != 0
        shr = shr << 1
      }
    }

    dut.clockDomain.waitSampling()
    apbWrite(0, (1L<<31)|(1L<<30)|(1L<<29)|2)
    println(apbRead(0).toHexString)

    fork {
      for (i <- 0 to 100) {
        spiWrite(0x80 + i, true, true)
      }
    }

    apbWrite(12, 0)

    apbWrite(4, 0x0201)

    apbWrite(8, 6)

    apbWrite(4, 0x0403)
    apbWrite(4, 0x0605)

    waitUntil(dut.io.interrupt.toBoolean)

    apbWrite(12, 1)
    println("OUT")
    for (i <- 1 to 3) println(apbRead(4).toHexString)

    apbWrite(12, 0)

    apbWrite(4, 0xf2f1)

    apbWrite(8, 5)

    apbWrite(4, 0xf4f3)
    apbWrite(4, 0xf6f5)

    waitUntil(dut.io.interrupt.toBoolean)

    apbWrite(12, 1)
    println("OUT")
    for (i <- 1 to 3) println(apbRead(4).toHexString)

    dut.clockDomain.waitSampling(100)
    simSuccess()
  }
}
