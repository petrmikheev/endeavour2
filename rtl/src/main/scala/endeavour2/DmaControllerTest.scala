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
      def mixrgb(x: Int, y: Int) : BigInt = {
        val b = ((x & 0x1f) + (y & 0x1f)) / 2
        val g = (((x>>5) & 0x3f) + ((y>>5) & 0x3f)) / 2
        val r = (((x>>11) & 0x1f) + ((y>>11) & 0x1f)) / 2
        BigInt((r<<11) | (g<<5) | b)
      }
      for (i <- 0 until 159) {
        val expected = (mixrgb(i*4+2,i*4+4)<<48) + (BigInt(i*4+2)<<32) + (mixrgb(i*4+2,i*4)<<16) + (BigInt(i*4)<<0)
        val v = mem.getBigInt(i)
        assert(v == expected, f"wrong result in Shifted MIXRGB at ${i*8}: ${v.toString(16)} != ${expected.toString(16)}")
        //println(f"${i*8} ${v.toString(16)}")
      }
      val expected = (BigInt(159*4+2)<<48) + (BigInt(159*4+2)<<32) + (mixrgb(159*4+2,159*4)<<16) + (BigInt(159*4)<<0)
      val v = mem.getBigInt(159)
      assert(v == expected, f"wrong result in Shifted MIXRGB at ${159*8}: ${v.toString(16)} != ${expected.toString(16)}")
    }

    {  // Set
      mem.setBigInt(cmdPos + 0, cmd(DmaOpcode.SET, 0, 128, 0xaaaaaaaaL))
      mem.setBigInt(cmdPos + 1, cmd(DmaOpcode.SET, 64, 64+2, 0xffffffffL))
      mem.setBigInt(cmdPos + 2, cmd(DmaOpcode.SET, 3, 5, 0xccccccccL))
      mem.setBigInt(cmdPos + 3, cmd(DmaOpcode.SET, 7, 9, 0xddddddddL))
      mem.setBigInt(cmdPos + 4, cmd(DmaOpcode.SET, 33, 39, 0x88888888L))
      mem.setBigInt(cmdPos + 5, cmd(DmaOpcode.WRITE, 0, 128, 0))
      runDma(6)
      assert(mem.getBigInt(0) == BigInt("ddaaaaccccaaaaaa", 16), "wrong result in SET")
      assert(mem.getBigInt(1) == BigInt("aaaaaaaaaaaaaadd", 16), "wrong result in SET")
      assert(mem.getBigInt(2) == BigInt("aaaaaaaaaaaaaaaa", 16), "wrong result in SET")
      assert(mem.getBigInt(4) == BigInt("aa888888888888aa", 16), "wrong result in SET")
      assert(mem.getBigInt(8) == BigInt("aaaaaaaaaaaaffff", 16), "wrong result in SET")
      //for (i <- 0 until 16) {
      //  val v = mem.getBigInt(i)
      //  println(f"${i*8} ${v.toString(16)}")
      //}
    }

    {  // MAP
      for (i <- 0 until 128) {
        mem.setBigInt(i, BigInt(f"aa00${i*2+1}%02xbbaa00${i*2}%02xbb", 16))
      }
      mem.setBigInt(128, BigInt("0706050403020100", 16))
      mem.setBigInt(129, BigInt("0f0e0d0c0b0a0908", 16))
      mem.setBigInt(130, BigInt("1716151413121110", 16))
      mem.setBigInt(131, BigInt("1f1e1d1c1b1a1918", 16))
      mem.setBigInt(132, BigInt("2726252423222120", 16))
      mem.setBigInt(133, BigInt("2f2e2d2c2b2a2928", 16))
      mem.setBigInt(134, BigInt("3736353433323130", 16))
      mem.setBigInt(135, BigInt("3f3e3d3c3b3a3938", 16))
      mem.setBigInt(cmdPos + 0, cmd(DmaOpcode.READ_SYNC, 1024, 2048, 0))
      mem.setBigInt(cmdPos + 1, cmd(DmaOpcode.READ_SYNC, 64, 128, 1024))
      mem.setBigInt(cmdPos + 2, cmd(DmaOpcode.LOADMAP, 3072, 4096, 1024))
      mem.setBigInt(cmdPos + 3, cmd(DmaOpcode.MAP1R4, 1024, 1024+256, (3072<<13) | 64))
      mem.setBigInt(cmdPos + 4, cmd(DmaOpcode.MAP1R2, 2048, 2048+128, (3072<<13) | 64))
      mem.setBigInt(cmdPos + 5, cmd(DmaOpcode.WRITE, 1024, 1024+256, 0))
      mem.setBigInt(cmdPos + 6, cmd(DmaOpcode.WRITE, 2048, 2048+128, 256))
      runDma(7)
      for (i <- 0 until 32) {
        val expected = BigInt(f"aa00${i*2+1}%02xbbaa00${i*2}%02xbb", 16)
        val v = mem.getBigInt(i)
        assert(v == expected, "wrong result in MAP1R4")
        //println(f"${i*8} ${v.toString(16)}")
      }
      for (i <- 0 until 16) {
        val expected = BigInt(f"${i*4+3}%02xbb${i*4+2}%02xbb${i*4+1}%02xbb${i*4}%02xbb", 16)
        val v = mem.getBigInt(32+i)
        assert(v == expected, "wrong result in MAP1R2")
        //println(f"${(i+32)*8} ${v.toString(16)}")
      }
    }

    dut.clockDomain.waitSampling(50)

    simSuccess()
  }
}
