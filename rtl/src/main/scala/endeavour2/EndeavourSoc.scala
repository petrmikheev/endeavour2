package endeavour2

import java.nio.file.{Files, Paths}

import spinal.core._
import spinal.lib._
import spinal.lib.bus.amba3.apb._
import spinal.lib.bus.amba4.axi._
import spinal.lib.bus.tilelink
import spinal.lib.bus.misc.SizeMapping
import spinal.lib.misc.plic._

import vexiiriscv.ParamSimple

class FrequencyCounter extends Component {
  val io = new Bundle {
    val test_clk = in Bool()
    val time = in UInt(16 bits)
    val freq = out UInt(32 bits)
  }
  val count_window = RegNext(io.time(11 downto 0) <= 2442 && io.time(11 downto 0) =/= 0)
  val read_window = io.time(11 downto 9) === 6
  val restart_window = RegNext(io.time(11 downto 9) === 7)
  val c = new ClockingArea(ClockDomain(clock = io.test_clk, reset = ClockDomain.current.reset)) {
    val count_en = RegNext(count_window) addTag(crossClockDomain)
    val restart_en = RegNext(restart_window) addTag(crossClockDomain)
    val counter = Reg(UInt(20 bits)) init(0)
    when (count_en)   { counter := counter + 1 }
    when (restart_en) { counter := 0 }
  }
  val res = RegNextWhen(c.counter, read_window) addTag(crossClockDomain)
  io.freq := res @@ U(0, 12 bits)
}

class EndeavourSoc(coreParam: ParamSimple,
                   bootRomContent : Option[Array[Byte]] = None,
                   internalRam : Boolean = false,
                   internalRamContent : Option[Array[Byte]] = None,
                   ramSize : Long = 1L<<30,
                   sim : Boolean = false) extends Component {
  val romBaseAddr = 0x40000000L
  val ramBaseAddr = 0x80000000L
  val resetVector = if (bootRomContent.isDefined) romBaseAddr else ramBaseAddr

  val io = new Bundle {
    val clk25 = in Bool()
    val clk60 = in Bool()
    val clk100 = in Bool()
    val clk_cpu = in Bool()
    val dyn_clk0 = in Bool()

    val pll_core_LOCKED = in Bool()
    val pll_ddr_LOCKED = in Bool()

    val uart = UART()
    val ddr = Ddr3_Phy()
    val key = in Bits(3 bits)
    val led = out Bits(4 bits)

    val snd_scl_OUT = out Bool()
    val snd_scl_OE = out Bool()
    val snd_sda_OUT = out Bool()
    val snd_sda_OE = out Bool()
    val snd_sda_IN = in Bool()
    val snd_shdn = out Bool()

    val i2c_scl_OUT = out Bool()
    val i2c_scl_OE = out Bool()
    val i2c_sda_OUT = out Bool()
    val i2c_sda_OE = out Bool()
    val i2c_sda_IN = in Bool()

    val esp32_en = out Bool()
  }

  io.esp32_en := False  // disable for now

  io.i2c_scl_OUT := False
  io.i2c_sda_OUT := False
  io.i2c_scl_OE := False // ~val
  io.i2c_sda_OE := False // ~val

  val rst_area = new ClockingArea(ClockDomain(
    clock = io.clk25,
    config = ClockDomainConfig(resetKind = BOOT))
  ) {
    val counter = Reg(UInt((if (sim) 4 else 15) bits)) init(0)
    val reset = RegInit(True)
    val softResetRequest = Bool() addTag(crossClockDomain)
    when (io.pll_core_LOCKED & io.pll_ddr_LOCKED) { counter := counter + 1 }
    when (counter.andR & reset) { reset := False }
    when (softResetRequest) {
      reset := True
      counter := 0
    }
  }

  ClockDomainStack.set(ClockDomain(clock=io.clk_cpu, reset=rst_area.reset))

  val cpu_freq_counter = new FrequencyCounter()
  val dvi_freq_counter = new FrequencyCounter()
  cpu_freq_counter.io.test_clk := io.clk_cpu
  dvi_freq_counter.io.test_clk := io.dyn_clk0

  val cd60mhz = ClockDomain(
      clock = io.clk60, reset = rst_area.reset,
      frequency = FixedFrequency(60 MHz))

  val area60mhz = new ClockingArea(cd60mhz) {

    val uart_ctrl = new UartController()
    uart_ctrl.io.uart <> io.uart

    val audio_ctrl = new AudioController()
    io.snd_shdn := audio_ctrl.io.shdn
    io.snd_scl_OUT := False
    io.snd_sda_OUT := False
    io.snd_scl_OE := ~audio_ctrl.io.i2c_scl  // 0->0, 1->Z (has external pull-up)
    io.snd_sda_OE := ~audio_ctrl.io.i2c_sda
    audio_ctrl.io.i2c_sda_IN := io.snd_sda_IN

    val apb = Apb3(Apb3Config(
      addressWidth  = 10,
      dataWidth     = 32,
      useSlaveError = true
    ))
    val apbDecoder = Apb3Decoder(
      master = apb,
      slaves = List(
        uart_ctrl.io.apb     -> (0x100, 16),
        audio_ctrl.io.apb    -> (0x200, 8)
        //i2c.io.apb   -> (0x300, 32)
      )
    )
  }
  val apb_60mhz_bridge = new ApbClockBridge(10)
  apb_60mhz_bridge.io.clk_output := io.clk60
  apb_60mhz_bridge.io.output <> area60mhz.apb

  val dbus = tilelink.fabric.Node()
  val cbus = tilelink.fabric.Node()
  cbus at (ramBaseAddr, ramSize) of dbus

  val miscApb = Apb3(Apb3Config(
    addressWidth  = 6,
    dataWidth     = 32,
    useSlaveError = true
  ))
  val miscCtrl = Apb3SlaveFactory(miscApb)

  val softResetRequested = RegInit(False)
  miscCtrl.onWrite(0)(softResetRequested := True)
  rst_area.softResetRequest := softResetRequested

  miscCtrl.read(U(1, 32 bits), address = 0x4)  // hart count
  miscCtrl.read(cpu_freq_counter.io.freq, address = 0x8)
  miscCtrl.read(dvi_freq_counter.io.freq, address = 0xC)

  val keyReg = RegNext(io.key)
  val ledReg = Reg(Bits(4 bits)) init(0)
  val ramStat = Bits(12 bits)
  io.led := ledReg
  miscCtrl.driveAndRead(ledReg, address = 0x10)
  miscCtrl.read(keyReg, address = 0x14)
  miscCtrl.read(ramStat, address = 0x18)
  miscCtrl.read(B(32 bits,
      0 -> coreParam.withRvZb,            // zba
      1 -> coreParam.withRvZb,            // zbb
      2 -> coreParam.withRvZb,            // zbc
      3 -> coreParam.withRvZb,            // zbs
      4 -> coreParam.lsuSoftwarePrefetch, // zicbop
      5 -> coreParam.withRvcbm,           // zicbom
      default -> False), address = 0x1C)
  miscCtrl.read(U(ramSize, 32 bits), address = 0x20)

  val plicSize = 0x4000000
  val plicPriorityWidth = 1
  val plic_gateways = List(
    PlicGatewayActiveHigh(source = area60mhz.uart_ctrl.io.interrupt, id = 1, priorityWidth = plicPriorityWidth)
    //PlicGatewayActiveHigh(source = peripheral.sdcard_ctrl.io.interrupt, id = 2, priorityWidth = plicPriorityWidth),
    //PlicGatewayActiveHigh(source = usb_ctrl.interrupt, id = 3, priorityWidth = plicPriorityWidth)
  )
  val plic_target = PlicTarget(id = 0, gateways = plic_gateways, priorityWidth = plicPriorityWidth)

  val plicApb = Apb3(Apb3Config(
    addressWidth  = log2Up(plicSize),
    dataWidth     = 32,
    useSlaveError = true
  ))
  val plic = PlicMapper(Apb3SlaveFactory(plicApb), PlicMapping.sifive)(
    gateways = plic_gateways,
    targets = List(plic_target)
  )

  val time_area = new ClockingArea(ClockDomain(clock=io.clk100, reset=rst_area.reset)) {
    val time = Reg(UInt(64 bits)) init(0)
    val counter = Reg(UInt(4 bits)) init(0)
    when (counter === 9) {
      counter := 0
      time := time + 1
    } otherwise {
      counter := counter + 1
    }
  }

  val time = Reg(UInt(64 bits)) init(0) addTag(crossClockDomain)
  when (time_area.counter(2)) { time := time_area.time }  // time step: 100ns
  cpu_freq_counter.io.time := time(15 downto 0)
  dvi_freq_counter.io.time := time(15 downto 0)

  val clintApb = Apb3(Apb3Config(
    addressWidth  = 16,
    dataWidth     = 32,
    useSlaveError = true
  ))
  val clint = new Area {
    val clintCtrl = Apb3SlaveFactory(clintApb)

    val IPI_ADDR = 0x0000
    val CMP_ADDR = 0x4000
    val TIME_ADDR = 0xBFF8

    clintCtrl.readMultiWord(time, TIME_ADDR)

    def connectCore(core: Core, hartId: Int) = {
      val timecmp = Reg(UInt(64 bits)) init(0xffffffffL)
      val softInterrupt = RegInit(False)
      clintCtrl.readAndWrite(timecmp(31 downto 0), CMP_ADDR + 8 * hartId)
      clintCtrl.readAndWrite(timecmp(63 downto 32), CMP_ADDR + 8 * hartId + 4)
      clintCtrl.readAndWrite(softInterrupt, IPI_ADDR + 4 * hartId)
      core.time := time
      core.interrupts.timer := RegNext(time >= timecmp)
      core.interrupts.software := softInterrupt
      core.interrupts.external := plic_target.iep
    }
  }

  val ioSize = 0x8000000
  val toApb = new tilelink.fabric.Apb3BridgeFiber()
  toApb.down.addTag(new system.tag.MemoryEndpointTag(SizeMapping(0, ioSize)))
  toApb.up.forceDataWidth(32)
  fiber.Handle {
    val apbDecoder = Apb3Decoder(
      master = toApb.down.get,
      slaves = List(
        // uart     0x100
        // audio    0x200
        // i2c      0x300
        apb_60mhz_bridge.io.input -> (0x0, 0x400),
        // misc    0x1000
        // display 0x2000
        // sdcard  0x3000
        // usb     0x4000
        // esp32   0x5000
        // spi?
        miscApb              -> (0x1000, 1<<miscApb.config.addressWidth),
        clintApb             -> (0x10000, 0x10000),
        plicApb              -> (0x4000000, 0x4000000)
      )
    )
  }

  val cpu0 : Core = new VexiiCore(param=coreParam, resetVector=resetVector)

  clint.connectCore(cpu0, hartId=0)
  dbus << cpu0.iBus
  toApb.up at (0, ioSize) of cpu0.dBus

  if (cpu0.hasL1) {
    dbus.forceDataWidth(64)
    dbus << cpu0.lsuL1Bus
  } else {
    dbus.forceDataWidth(32)
    dbus << cpu0.dBus
  }

  val mbus = tilelink.fabric.Node()

  if (cpu0.hasL1) {
    val tl_cache = new tilelink.coherent.CacheFiber()
    tl_cache.parameter.downPendingMax = 8
    tl_cache.up << cbus
    mbus << tl_cache.down
  } else {
    mbus << cbus
  }

  val rom = bootRomContent match {
    case Some(content) => {
      val rom = new SlowRomFiber(content)
      println(s"ROM at ${romBaseAddr.toHexString} - ${(romBaseAddr + (1<<rom.addressWidth)).toHexString}")
      rom.up at (romBaseAddr, 1<<rom.addressWidth) of cpu0.iBus
    }
    case None =>
  }

  if (internalRam) {
    val ram = new RamFiber(ramSize, initialContent=internalRamContent)
    ram.up << mbus
    io.ddr.not_connected()
    ramStat := 3  // calibration_ok, calibration_done
  } else {
    assert(cpu0.hasL1);
    println(s"RAM at ${ramBaseAddr.toHexString} - ${(ramBaseAddr + ramSize).toHexString}")
    val ram = new Ddr3Fiber(ramSize, io.ddr)
    ram.up << mbus
    ramStat := ram.cal_stat
  }
}

object EndeavourSoc {
  def main(args: Array[String]): Unit = {
    val bootRomContent = Files.readAllBytes(Paths.get("../software/bios/microloader.bin"))
    SpinalConfig(mode=Verilog, targetDirectory="verilog").generate(new EndeavourSoc(
        //coreParam=Core.small(withCaches=false), internalRam=true, ramSize=65536,
        //coreParam=Core.small(withCaches=true),
        coreParam=Core.medium(),
        //coreParam=Core.full(),
        bootRomContent=Some(bootRomContent)
        ))
  }
}
