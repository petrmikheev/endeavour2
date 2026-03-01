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

    val cmdPos = 8192
    def runDma(cmdCount: Int) = {
      apbWrite(0, cmdPos * 8) // command addr
      apbWrite(4, cmdCount)    // command count; start
      apbWrite(8, 1)    // enable interrupt
      waitUntil(dut.io.interrupt.toBoolean)
      apbWrite(8, 0)    // disable interrupt
    }

    val mem = dut.ram.thread.logic.mem
    dut.clockDomain.waitSampling(5)

    {  // Aligned MIXRGB
      for (i <- 0 until 4096) {
        mem.setBigInt(i, BigInt("FF00FF", 16) + (BigInt(i)<<32))
      }
      for (i <- 4096 until 8192) {
        mem.setBigInt(i, BigInt("FF00FF00000000", 16) + i)
      }
      mem.setBigInt(cmdPos + 0, cmd(DmaOpcode.READ, 128, 256, 0))
      mem.setBigInt(cmdPos + 1, cmd(DmaOpcode.READ_SYNC, 256, 384, 4096*8))
      mem.setBigInt(cmdPos + 2, cmd(DmaOpcode.COPY, 384, 512, 128))
      mem.setBigInt(cmdPos + 3, cmd(DmaOpcode.MIXRGB, 0, 128, (384 << 13) | 256))
      mem.setBigInt(cmdPos + 4, cmd(DmaOpcode.WRITE, 0, 128, 0))
      runDma(5)
      for (i <- 0 until 16) {
        val expected : BigInt = 0x6f006f006f086fL + (i + 1L) / 2 + (((i + 1L) / 2) << 32)
        val v = mem.getBigInt(i)
        assert(v == expected, f"wrong result in Aligned MIXRGB at ${i*8}: ${v.toString(16)} != ${expected.toString(16)}")
        // println(f"${i*8} ${v.toString(16)}")
      }
    }

    {  // Shifted MIXRGB
      for (i <- 0 until 160) {
        mem.setBigInt(i, (BigInt(i*4+2)<<48) + (BigInt(i*4+2)<<32) + (BigInt(i*4)<<16) + (BigInt(i*4)<<0))
      }
      mem.setBigInt(cmdPos + 0, cmd(DmaOpcode.READ_SYNC, 0, 640*2, 0))
      mem.setBigInt(cmdPos + 1, cmd(DmaOpcode.MIXRGB, 2, 640*2-2, (2 << 13) | 4))
      mem.setBigInt(cmdPos + 2, cmd(DmaOpcode.WRITE_SYNC, 0, 640*2, 0))
      runDma(3)
      for (i <- 0 until 16) {
        val v = mem.getBigInt(i)
        println(f"${i*8} ${v.toString(16)}")
        // TODO assert v == expected
      }
    }

    {  // Set
      mem.setBigInt(cmdPos + 0, cmd(DmaOpcode.SET, 0, 128, 0xaaaaaaaaL))
      mem.setBigInt(cmdPos + 1, cmd(DmaOpcode.SET, 64, 64+2, 0xffffffffL))
      mem.setBigInt(cmdPos + 2, cmd(DmaOpcode.SET, 3, 5, 0xccccccccL))
      mem.setBigInt(cmdPos + 3, cmd(DmaOpcode.SET, 7, 9, 0xddddddddL))
      mem.setBigInt(cmdPos + 4, cmd(DmaOpcode.SET, 33, 39, 0x88888888L))
      mem.setBigInt(cmdPos + 5, cmd(DmaOpcode.WRITE, 0, 128, 0))
      runDma(6)
      for (i <- 0 until 16) {
        val v = mem.getBigInt(i)
        println(f"${i*8} ${v.toString(16)}")
        // TODO assert v == expected
      }
    }

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

    /*for (i <- (128-8) until (128+64)) {
      println(f"${i*8} ${mem.getBigInt(i).toString(16)}")
    }*/

    /*for (i <- 0 until 16) {
      println(f"${i*8} ${mem.getBigInt(i).toString(16)}")
    }*/

    dut.clockDomain.waitSampling(50)

    simSuccess()
  }
}
