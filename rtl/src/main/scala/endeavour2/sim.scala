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
      for (j <- 0 until wordSize) {
        if (i*wordSize+j < size) {
          v = v + (BigInt(data(i*wordSize+j)&0xff) << (j*8))
        }
      }
      //println("%4x: %08x".format(i*wordSize, v))
      mem.setBigInt(addr / wordSize + i, v)
    }
    println("DONE")
  }
}

class EndeavourSocSim(ramSize : Long) extends EndeavourSoc(
        //coresParams=List(Core.small(withCaches=true)/*, Core.small(withCaches=true)*/),
        coresParams=List(Core.medium()),
        internalRam=true, ramSize=ramSize,
        internalRamContent=Some(Files.readAllBytes(Paths.get("../software/bios/bios.bin"))),
        //bootRomContent=Some(Files.readAllBytes(Paths.get("../software/bios/microloader.bin"))),
        sim=true
        ) {
  Fiber patch{
    iram.thread.logic.mem.simPublic()
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

  val ramSize = 8L << 20
  //val ramSize = 64L << 20
  sim.compile(new EndeavourSocSim(ramSize)).doSimUntilVoid("test", seed = 42){dut =>
    val probe = new VexiiRiscvProbe(
      cpu = dut.cpus(0).vexii(),
      kb = None // Option(new vexiiriscv.test.konata.Backend(new File(currentTestPath, "konata.log")).spinalSimFlusher(hzToLong(1000 Hz)))
    )
    probe.autoRegions()

    val mem = dut.iram.thread.logic.mem
    FileUtil.loadToMem(mem, 0x8000l, "../software/raw_examples/sim_dma.bin")
    //FileUtil.loadToMem(mem, 0x8000l, "/tmp/endeavour2.dtb")
    //FileUtil.loadToMem(mem, 0x2000000l, "../../endeavour2-ext/linux-kernel/arch/riscv/boot/Image")

    //disableSimWave()
    //delayed(6000000000L)(enableSimWave())
    SimTimeout(6300000000L)
    dut.io.pll_core_LOCKED #= true
    dut.io.pll_ddr_LOCKED #= true
    dut.io.key #= 7
    dut.io.i2c_sda_IN #= true
    dut.io.sd.ndetect #= true
    dut.io.uart.rx #= true
    ClockDomain(dut.io.clk25).forkStimulus(40000)
    ClockDomain(dut.io.clk60).forkStimulus(17000)
    ClockDomain(dut.io.clk_cpu).forkStimulus(5000)
    //dut.io.reset #= true
    //delayed(100 ns)(dut.io.reset #= false)

    val sendUart = fork {
      val data = "run 80008000\n";
      //val data = "device_tree 80008000\nboot 82000000\n";

      val step = 17000 * 2;
      sleep(step * 100)
      data.getBytes("UTF-8").foreach { c =>
        dut.io.uart.rx #= false
        sleep(step)
        for (i <- 0 until 8) {
          dut.io.uart.rx #= ((c >> i) & 1) != 0
          sleep(step)
        }
        dut.io.uart.rx #= true
        sleep(step * 3)
      }
    }

    dut.listenUart()
  }
}
