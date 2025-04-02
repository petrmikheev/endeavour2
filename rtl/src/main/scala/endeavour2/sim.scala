package endeavour2

import rvls.spinal.RvlsBackend
import spinal.core._
import spinal.core.sim._
import spinal.core.fiber._
import spinal.lib.misc.Elf
import vexiiriscv.test.VexiiRiscvProbe

import java.io.{File,FileInputStream}
import java.nio.file.{Files, Paths}

object FileUtil {
  def loadToMem(mem: Mem[Bits], addr: Long, path: String) = {
    print("Loading %s ".format(path))
    val stream = new FileInputStream(path)
    val size = stream.available()
    val data = new Array[Byte](size)
    stream.read(data)
    stream.close()
    //println("mem.width = %d".format(mem.width))
    val wordSize = mem.width / 8
    for (i <- 0 until (size+wordSize-1) / wordSize) {
      var v: BigInt = 0
      for (j <- 0 until wordSize) { v = v + (BigInt(data(i*wordSize+j)&0xff) << (j*8)) }
      //println("%4x: %08x".format(i*wordSize, v))
      mem.setBigInt(addr / wordSize + i, v)
    }
    println("DONE")
  }
}

class EndeavourSocSim extends EndeavourSoc(
        coreParam=Core.small(withCaches=true),
        internalRam=true, ramSize=65536,
        internalRamContent=Some(Files.readAllBytes(Paths.get("../software/bios/bios.bin"))),
        //bootRomContent=Some(Files.readAllBytes(Paths.get("../software/bios/microloader.bin"))),
        sim=true
        ) {
  Fiber patch{
    //ram.thread.logic.mem.simPublic()
    rst_area.reset.simPublic()
  }

  val uapb = area60mhz.uart_ctrl.io.apb
  val uart_write_fire = uapb.PSEL(0) && uapb.PENABLE && uapb.PREADY && uapb.PWRITE && uapb.PADDR === 0x4
  val uart_write_value = uapb.PWDATA(7 downto 0)
  uart_write_fire.simPublic
  uart_write_value.simPublic

  def listenUart() = {
    val cd = ClockDomain(io.clk60)
    while (true) {
      cd.waitSampling()
      if (uart_write_fire.toBoolean) {
        val c = uart_write_value.toInt
        if (c == 10 || (c>=32 && c<=126)) print(c.toChar)
      }
    }
  }
}

object EndeavourSocSim extends App {
  val sim = SimConfig.withTimeSpec(1 ns, 1 ps).withWave

  sim.compile(new EndeavourSocSim(/*withCaches=false, internalRam=true*/)).doSimUntilVoid("test", seed = 42){dut =>
    val probe = new VexiiRiscvProbe(
      cpu = dut.cpu0.vexii(),
      kb = None // Option(new vexiiriscv.test.konata.Backend(new File(currentTestPath, "konata.log")).spinalSimFlusher(hzToLong(1000 Hz)))
    )
    probe.autoRegions()

    //val mem = dut.ram.thread.logic.mem
    //FileUtil.loadToMem(mem, 0, "../software/bios/bios.bin")
    //FileUtil.loadToMem(mem, 0x400000l, "/home/petya/endeavour-tools/linux_kernel/arch/riscv/boot/Image")
    //FileUtil.loadToMem(mem, 0x5000000l, "/home/petya/endeavour-tools/minfs.initrd")

    SimTimeout(6000000000L)
    dut.io.pll_core_LOCKED #= true
    dut.io.pll_ddr_LOCKED #= true
    dut.io.key #= 2
    dut.io.i2c_sda_IN #= true
    dut.io.sd.ndetect #= false
    dut.io.sd.cmd_IN #= true
    ClockDomain(dut.io.clk25).forkStimulus(40000)
    ClockDomain(dut.io.clk60).forkStimulus(17000)
    ClockDomain(dut.io.clk100).forkStimulus(10000)
    ClockDomain(dut.io.clk_cpu).forkStimulus(5000)
    //dut.io.reset #= true
    //delayed(100 ns)(dut.io.reset #= false)

    dut.listenUart()
  }
}
