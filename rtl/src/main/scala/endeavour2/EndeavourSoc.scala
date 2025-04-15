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

class EndeavourSoc(coresParams: List[ParamSimple],
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
    val clk_cpu = in Bool()
    val dyn_clk0 = in Bool()

    val pll_core_LOCKED = in Bool()
    val pll_ddr_LOCKED = in Bool()

    val ddr = Ddr3_Phy()
    val ddr_tac_shift = out Bits(3 bits)
    val ddr_tac_shift_ena = out Bool()
    val ddr_tac_shift_sel = out Bits(5 bits)

    val uart = UART()
    val usb1 = USB()
    val usb2 = USB()

    val cbsel = in Bits(2 bits)
    val boot_en = in Bool()
    val key = in Bits(2 bits)
    val led = out Bits(4 bits)

    val spi_flash_ncs_IN = in Bool()
    val spi_flash_ncs_OUT = out Bool()
    val spi_flash_ncs_OE = out Bool()
    val spi_flash_clk = out Bool()
    val spi_flash_data_IN = in Bits(4 bits)
    val spi_flash_data_OUT = out Bits(4 bits)
    val spi_flash_data_OE = out Bits(4 bits)

    val sd = SdcardPhy()
    val TL_MODE_SEL = out Bool()

    val dvi_data_HI = out Bits(12 bits)
    val dvi_data_LO = out Bits(12 bits)
    val dvi_de = out Bool()
    val dvi_hsync = out Bool()
    val dvi_vsync = out Bool()

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
    val esp32_spi_boot = out Bool()
    val esp32_rx = out Bool()
    val esp32_tx = in Bool()

    // ESP32 GPIO 4-7
    /*val esp32_io4_IN = in Bool()
    val esp32_io4_OUT = out Bool()
    val esp32_io4_OE = out Bool()
    val esp32_io5_IN = in Bool()
    val esp32_io5_OUT = out Bool()
    val esp32_io5_OE = out Bool()
    val esp32_io6_IN = in Bool()
    val esp32_io6_OUT = out Bool()
    val esp32_io6_OE = out Bool()
    val esp32_io7_IN = in Bool()
    val esp32_io7_OUT = out Bool()
    val esp32_io7_OE = out Bool()*/
  }

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

    val esp32_uart_ctrl = new UartController()
    esp32_uart_ctrl.io.uart.rx := io.esp32_tx
    io.esp32_rx := esp32_uart_ctrl.io.uart.tx

    val audio_ctrl = new AudioController()
    io.snd_shdn := audio_ctrl.io.shdn
    io.snd_scl_OUT := False
    io.snd_sda_OUT := False
    io.snd_scl_OE := ~audio_ctrl.io.i2c_scl  // 0->0, 1->Z (has external pull-up)
    io.snd_sda_OE := ~audio_ctrl.io.i2c_sda
    audio_ctrl.io.i2c_sda_IN := io.snd_sda_IN

    val i2c_ctrl = new I2cController()
    io.i2c_scl_OUT := False
    io.i2c_sda_OUT := False
    io.i2c_scl_OE := ~i2c_ctrl.io.i2c_scl  // 0->0, 1->Z (has external pull-up)
    io.i2c_sda_OE := ~i2c_ctrl.io.i2c_sda
    i2c_ctrl.io.i2c_sda_IN := io.i2c_sda_IN

    val spi_flash_ctrl = new SpiController()
    io.spi_flash_ncs_OUT := spi_flash_ctrl.io.spi_ncs
    io.spi_flash_ncs_OE := spi_flash_ctrl.io.spi_ncs_en
    io.spi_flash_clk := spi_flash_ctrl.io.spi_clk
    spi_flash_ctrl.io.spi_d1 := io.spi_flash_data_IN(1)
    io.spi_flash_data_OUT := (B"110", spi_flash_ctrl.io.spi_d0).asBits
    io.spi_flash_data_OE := Mux(spi_flash_ctrl.io.spi_ncs_en, B"1101", B"0000")

    val apb = Apb3(Apb3Config(
      addressWidth  = 11,
      dataWidth     = 32,
      useSlaveError = true
    ))
    val apbDecoder = Apb3Decoder(
      master = apb,
      slaves = List(
        uart_ctrl.io.apb       -> (0x100, 16),
        audio_ctrl.io.apb      -> (0x200, 8),
        i2c_ctrl.io.apb        -> (0x300, 16),
        esp32_uart_ctrl.io.apb -> (0x400, 16),
        spi_flash_ctrl.io.apb  -> (0x500, 16)
      )
    )
  }
  val apb_60mhz_bridge = new ApbClockBridge(11)
  apb_60mhz_bridge.io.clk_output := io.clk60
  apb_60mhz_bridge.io.output <> area60mhz.apb

  val sdcard_ctrl = new SdcardController()
  sdcard_ctrl.io.clk := io.ddr.core_clk
  sdcard_ctrl.io.reset := rst_area.reset
  sdcard_ctrl.io.sdcard <> io.sd
  io.TL_MODE_SEL := ~io.sd.vdd_sel_3v3

  val apb_sdcard_bridge = new ApbClockBridge(5)
  apb_sdcard_bridge.io.clk_output := sdcard_ctrl.io.clk
  apb_sdcard_bridge.io.output <> sdcard_ctrl.io.apb

  val dbus = tilelink.fabric.Node()
  val cbus = tilelink.fabric.Node()
  cbus at (ramBaseAddr, ramSize) of dbus

  val usb_ctrl = new EndeavourUSB(cd60mhz, io.usb1, io.usb2, sim=sim)
  dbus << usb_ctrl.dma

  val video = (ramSize >= (1<<23)) generate new Area {
    val ctrl = new VideoController()
    ctrl.io.pixel_clk <> io.dyn_clk0
    io.dvi_data_LO(11 downto 4) := ctrl.io.data_red
    (io.dvi_data_LO(3 downto 0), io.dvi_data_HI(11 downto 8)) := ctrl.io.data_green
    io.dvi_data_HI(7 downto 0) := ctrl.io.data_blue
    io.dvi_de := ctrl.io.data_enable
    io.dvi_hsync := ctrl.io.hSync
    io.dvi_vsync := ctrl.io.vSync

    val bus = tilelink.fabric.Node.down()
    val bus_fiber = fiber.Fiber build new Area {
      bus.m2s forceParameters tilelink.M2sParameters(
        addressWidth = 30,
        dataWidth = 64,
        masters = List(
          tilelink.M2sAgent(
            name = ctrl,
            mapping = List(
              tilelink.M2sSource(
                id = SizeMapping(0, 4),
                emits = tilelink.M2sTransfers(get = tilelink.SizeRange(64, 64))
              )
            )
          )
        )
      )
      bus.s2m.supported load tilelink.S2mSupport.none()
      bus.bus << ctrl.io.tl_bus
    }
    bus.setDownConnection(a = StreamPipe.HALF, d = StreamPipe.M2S_KEEP)
    cbus << bus
  }
  if (video == null) {
    io.dvi_data_LO := 0
    io.dvi_data_HI := 0
    io.dvi_de := False
    io.dvi_hsync := False
    io.dvi_vsync := False
  }

  val miscApb = Apb3(Apb3Config(
    addressWidth  = 6,
    dataWidth     = 32,
    useSlaveError = true
  ))
  val miscCtrl = Apb3SlaveFactory(miscApb)

  val softResetRequested = RegInit(False)
  rst_area.softResetRequest := softResetRequested

  miscCtrl.onWrite(0)(softResetRequested := True)
  miscCtrl.read(cpu_freq_counter.io.freq, address = 0x4)
  miscCtrl.read(dvi_freq_counter.io.freq, address = 0x8)
  miscCtrl.read(U(coresParams.length, 32 bits), address = 0xC)  // hart count

  def cpuFeatures(p: ParamSimple): Bits = B(32 bits,
      0 -> p.withRvZb,            // zba
      1 -> p.withRvZb,            // zbb
      2 -> p.withRvZb,            // zbc
      3 -> p.withRvZb,            // zbs
      4 -> p.lsuSoftwarePrefetch, // zicbop
      5 -> p.withRvcbm,           // zicbom
      default -> False)

  assert(coresParams.length <= 4)
  for (i <- 0 to coresParams.length - 1) {
    miscCtrl.read(cpuFeatures(coresParams(i)), address = 0x10 + i * 4)
  }

  val ramTacShiftOverridden = RegInit(False)
  val ramTacShiftOverrideRequest = RegInit(False)
  val ramTacShiftOverride = Reg(Bits(3 bits))
  val ramStat = Bits(15 bits)
  when (ramTacShiftOverrideRequest) { ramTacShiftOverrideRequest := False }

  val cbKeyReg = RegNext((io.cbsel, io.boot_en, io.spi_flash_ncs_IN).asBits)
  val keyReg = RegNext(io.key)
  val ledReg = Reg(Bits(4 bits)) init(0)
  val esp32CfgReg = Reg(Bits(2 bits)) init(0)
  io.led := ledReg
  io.esp32_en := esp32CfgReg(0)
  io.esp32_spi_boot := esp32CfgReg(1)
  miscCtrl.readAndWrite(ledReg, address = 0x20)
  miscCtrl.read(keyReg, address = 0x24)
  miscCtrl.read(cbKeyReg, address = 0x24, bitOffset = 28)
  miscCtrl.read((Mux(ramTacShiftOverridden, ramTacShiftOverride, ramStat(14 downto 12)), False, ramStat).asBits, address = 0x28)
  miscCtrl.onWrite(0x28)({
    ramTacShiftOverrideRequest := True
    ramTacShiftOverridden := True
  })
  miscCtrl.write(ramTacShiftOverride, address = 0x28, bitOffset = 16)
  miscCtrl.read(U(ramSize, 32 bits), address = 0x2C)
  miscCtrl.readAndWrite(esp32CfgReg, address = 0x30)

  val plicSize = 0x4000000
  val plicPriorityWidth = 1
  val plic_gateways = List(
    PlicGatewayActiveHigh(source = area60mhz.uart_ctrl.io.interrupt, id = 1, priorityWidth = plicPriorityWidth),
    PlicGatewayActiveHigh(source = sdcard_ctrl.io.interrupt, id = 2, priorityWidth = plicPriorityWidth),
    PlicGatewayActiveHigh(source = usb_ctrl.interrupt, id = 3, priorityWidth = plicPriorityWidth)
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

  val time_area = new ClockingArea(cd60mhz) {
    val time = Reg(UInt(64 bits)) init(0)
    val counter = Reg(UInt(3 bits)) init(0)
    when (counter === 5) {
      counter := 0
      time := time + 1
    } otherwise {
      counter := counter + 1
    }
  }

  val time = Reg(UInt(64 bits)) init(0) addTag(crossClockDomain)
  when (time_area.counter(1)) { time := time_area.time }  // time step: 100ns
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
    var apbSlaves = List[(Apb3, SizeMapping)](
      apb_60mhz_bridge.io.input  -> (0x0, 0x800),
      miscApb                    -> (0x1000, 1<<miscApb.config.addressWidth),
      apb_sdcard_bridge.io.input -> (0x3000, 32),
      usb_ctrl.apb_ctrl          -> (0x4000, 0x1000),
      clintApb                   -> (0x10000, 0x10000),
      plicApb                    -> (0x4000000, 0x4000000)
    )
    if (video != null) {
      apbSlaves ++= List[(Apb3, SizeMapping)](
        video.ctrl.io.apb        -> (0x2000, 64)
      )
    }
    val apbDecoder = Apb3Decoder(
      master = toApb.down.get,
      slaves = apbSlaves
    )
  }

  val ibus = tilelink.fabric.Node()
  dbus << ibus

  var hasL1 = false
  var hartId = 0

  val cpus = coresParams.map(p => {
    val cpu : Core = new VexiiCore(param=p, resetVector=resetVector, hartId=hartId)
    if (cpu.hasL1) {
      hasL1 = true
      dbus << cpu.lsuL1Bus
    } else {
      dbus << cpu.dBus
    }
    clint.connectCore(cpu, hartId=hartId)
    ibus << cpu.iBus
    toApb.up at (0, ioSize) of cpu.dBus
    hartId += 1
    cpu
  })

  dbus.forceDataWidth(if (hasL1) 64 else 32)

  val mbus = tilelink.fabric.Node()

  if (hasL1 && internalRam) {
    val tl_hub = new tilelink.coherent.HubFiber()
    tl_hub.parameter.downPendingMax = 8
    tl_hub.up << cbus
    mbus << tl_hub.down
  } else if (hasL1) {
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
      rom.up at (romBaseAddr, 1<<rom.addressWidth) of ibus
    }
    case None =>
  }

  if (internalRam) {
    val ram = new RamFiber(ramSize, initialContent=internalRamContent)
    ram.up << mbus
    io.ddr.not_connected()
    io.ddr_tac_shift := 0
    io.ddr_tac_shift_ena := False
    ramStat := 3  // calibration_ok, calibration_done
  } else {
    assert(hasL1);
    println(s"RAM at ${ramBaseAddr.toHexString} - ${(ramBaseAddr + ramSize).toHexString}")
    val ram = new Ddr3Fiber(ramSize, io.ddr)
    ram.up << mbus
    ramStat := ram.cal_stat
    io.ddr_tac_shift_ena := ram.ddr_ctrl.io.tac_shift_ena | ramTacShiftOverrideRequest
    io.ddr_tac_shift := Mux(ramTacShiftOverridden, ramTacShiftOverride, ram.ddr_ctrl.io.tac_shift)
  }
  io.ddr_tac_shift_sel := 4
}

object EndeavourSoc {
  def main(args: Array[String]): Unit = {
    val bootRomContent = Files.readAllBytes(Paths.get("../software/bios/microloader.bin"))
    SpinalConfig(mode=Verilog, targetDirectory="verilog").generate(new EndeavourSoc(
        //coresParams=List(Core.small(withCaches=false)), internalRam=true, ramSize=65536,
        //coresParams=List(Core.small(withCaches=true)),
        coresParams=List(Core.medium()),
        //coresParams=List(Core.medium(), Core.small(withCaches=true)),
        //coresParams=List(Core.medium(withFpu=false, withBiggerCache=false), Core.medium(withFpu=false, withBiggerCache=false)),
        //coresParams=List(Core.full()),
        bootRomContent=Some(bootRomContent)
        ))
  }
}
