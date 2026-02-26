package endeavour2

import spinal.core._
import spinal.core.sim._
import spinal.core.fiber._
import spinal.lib._
import spinal.lib.bus.amba3.apb.{Apb3}

class DmaControllerTest extends Component {
  val ram = new RamFiber(1<<17)
  val c = new DmaControllerFiber()
  ram.up << c.dma
  val io = new Bundle {
    val apb = slave(Apb3(c.apb.config))
    val interrupt = out Bool()
  }
  io.apb <> c.apb
  io.interrupt := c.interrupt
  Fiber patch { ram.thread.logic.mem.simPublic }
}

object DmaControllerTest extends App {
  val sim = SimConfig.withTimeSpec(1 ns, 1 ps).withWave

  sim.compile(new DmaControllerTest()).doSimUntilVoid("test", seed = 42){dut =>
    SimTimeout(6000000000L)
    dut.clockDomain.forkStimulus(5000)

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

    def cmd(opcode: Int, b_from: Int, b_to: Int, arg: Long): BigInt = {
      (BigInt(opcode) << 58) | (BigInt(b_from) << 45) | (BigInt(b_to) << 32) | arg
    }

    val mem = dut.ram.thread.logic.mem
    /*for (i <- 0 until 8192) {
      mem.setBigInt(i, BigInt("1234000056780000", 16) + (BigInt(i)<<32) + i)
    }*/

    for (i <- 0 until 4096) {
      mem.setBigInt(i, BigInt("FF00FF", 16) + (BigInt(i)<<32))
    }
    for (i <- 4096 until 8192) {
      mem.setBigInt(i, BigInt("FF00FF00000000", 16) + i)
    }

    mem.setBigInt(8192 + 0, cmd(DmaOpcode.READ, 128, 256, 0))
    mem.setBigInt(8192 + 1, cmd(DmaOpcode.READ_SYNC, 256, 384, 4096*8))
    mem.setBigInt(8192 + 2, cmd(DmaOpcode.COPY, 384, 512, 128))
    mem.setBigInt(8192 + 3, cmd(DmaOpcode.MIXRGB, 0, 128, (384 << 13) | 256))
    mem.setBigInt(8192 + 4, cmd(DmaOpcode.WRITE, 0, 128, 0))
    /*mem.setBigInt(16, cmd(DmaOpcode.SET, 0, 4096, 0x444))
    for (i <- 0 until 16) {
      mem.setBigInt(16 + i + 1, cmd(DmaOpcode.WRITE, 0, 4096, 0x80000000L + 65536 + i * 4096))
    }*/

    /*mem.setBigInt(16, cmd(DmaOpcode.READ, 128, 256, 1024))
    mem.setBigInt(17, cmd(DmaOpcode.READ_SYNC, 1024, 2048, 0))
    mem.setBigInt(18, cmd(DmaOpcode.COPY, 1030, 1024+128-1, 128))
    mem.setBigInt(19, cmd(DmaOpcode.SET, 1024+256, 1024+256+63, 0x87654321L))
    for (i <- 4 to 38) {
      mem.setBigInt(16 + i, cmd(DmaOpcode.SET, 1400+i, 1400+i+1, 0x01010101L * i))
    }
    mem.setBigInt(16 + 39, cmd(DmaOpcode.WRITE_SYNC, 1024, 2048, 1024))*/

    dut.clockDomain.waitSampling(5)
    apbWrite(0, 8192 * 8) // command addr
    apbWrite(4, 5 /*40*/)    // command count; start
    apbWrite(8, 1)    // enable interrupt

    waitUntil(dut.io.interrupt.toBoolean)

    /*for (i <- (128-8) until (128+64)) {
      println(f"${i*8} ${mem.getBigInt(i).toString(16)}")
    }*/

    for (i <- 0 until 16) {
      println(f"${i*8} ${mem.getBigInt(i).toString(16)}")
    }

    apbWrite(8, 0)

    dut.clockDomain.waitSampling(50)

    simSuccess()
  }
}
