package endeavour2

import java.nio.file.{Files, Paths}

import spinal.core._
import spinal.lib._
import spinal.lib.bus.amba3.apb._
import spinal.lib.bus.amba4.axi._
import spinal.lib.bus.tilelink
import spinal.lib.bus.misc.SizeMapping
import spinal.lib.misc.plic._

class FrequncyCounter extends Component {
  val io = new Bundle {
    val test_clk = in Bool()
  }
}

class EndeavourSoc(withMinimalCore : Boolean = false,
                   withSmallCore : Boolean = false,
                   romContent : Option[Array[Byte]] = None,
                   internalRam : Boolean = false,
                   internalRamContent : Option[Array[Byte]] = None,
                   ramSize : Long = 1024L * 1024L * 1024L,
                   sim : Boolean = false) extends Component {
  if (withMinimalCore) assert(internalRam && !withSmallCore, "minimal core requires internal RAM")

  val romBaseAddr = 0x40000000L
  val ramBaseAddr = 0x80000000L
  val resetVector = if (romContent.isDefined) romBaseAddr else ramBaseAddr

  val io = new Bundle {
    val clk25 = in Bool()
    val clk60 = in Bool()
    val clk100 = in Bool()
    val clk_cpu = in Bool()

    val pll_core_LOCKED = in Bool()
    val pll_ddr_LOCKED = in Bool()

    val uart = UART()
    val ddr = Ddr3_Phy()
    val key = in Bits(3 bits)
    val led = out Bits(4 bits)
  }

  val rst_area = new ClockingArea(ClockDomain(
    clock = io.clk25,
    config = ClockDomainConfig(resetKind = BOOT))
  ) {
    val counter = Reg(UInt(4 bits)) init(0)
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

  val cd60mhz = ClockDomain(
      clock = io.clk60, reset = rst_area.reset,
      frequency = FixedFrequency(60 MHz))

  val area60mhz = new ClockingArea(cd60mhz) {

    val uart_ctrl = new UartController()
    uart_ctrl.io.uart <> io.uart

    /*val audio_ctrl = new AudioController()
    audio_ctrl.io.shdn <> io.audio_shdn
    audio_ctrl.io.i2c <> io.audio_i2c*/

    val apb = Apb3(Apb3Config(
      addressWidth  = 10,
      dataWidth     = 32,
      useSlaveError = true
    ))
    val apbDecoder = Apb3Decoder(
      master = apb,
      slaves = List(
        uart_ctrl.io.apb     -> (0x100, 16)
        //audio_ctrl.io.apb    -> (0x200, 8)
        //i2c.io.apb   -> (0x300, 32)
      )
    )
  }
  val apb_60mhz_bridge = new ApbClockBridge(10)
  apb_60mhz_bridge.io.clk_output := io.clk60
  apb_60mhz_bridge.io.output <> area60mhz.apb

  val cbus = tilelink.fabric.Node()
  cbus.forceDataWidth(if (withMinimalCore) 32 else 64)

  val miscApb = Apb3(Apb3Config(
    addressWidth  = 5,
    dataWidth     = 32,
    useSlaveError = true
  ))
  val miscCtrl = Apb3SlaveFactory(miscApb)

  val softResetRequested = RegInit(False)
  miscCtrl.onWrite(0)(softResetRequested := True)
  rst_area.softResetRequest := softResetRequested

  miscCtrl.read(U(1, 32 bits), address = 0x4)  // hart count
  miscCtrl.read(U(0, 32 bits), address = 0x8)  // TODO cpu freq khz
  miscCtrl.read(U(0, 32 bits), address = 0xC)  // TODO dvi pixel freq khz

  val keyReg = RegNext(io.key)
  val ledReg = Reg(Bits(4 bits)) init(0)
  val ramStat = Bits(12 bits)
  io.led := ledReg
  miscCtrl.driveAndRead(ledReg, address = 0x10)
  miscCtrl.read(keyReg, address = 0x14)
  miscCtrl.read(ramStat, address = 0x18)

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
        miscApb              -> (0x1000, 32),
        clintApb             -> (0x10000, 0x10000),
        plicApb              -> (0x4000000, 0x4000000)
      )
    )
  }

  val mbus = tilelink.fabric.Node()
  if (withMinimalCore) {
    mbus << cbus
  } else {
    val tilelink_hub = new tilelink.coherent.HubFiber()
    tilelink_hub.up << cbus
    mbus << tilelink_hub.down
  }
  val iobus = if (withMinimalCore) cbus else tilelink.fabric.Node()
  toApb.up at (0, ioSize) of iobus

  val rom = romContent match {
    case Some(content) => {
      val rom = new RamFiber(8192, initialContent=Option(content))
      rom.up at (romBaseAddr, 8192) of mbus
      //val rom = new RomFiber(content)
      //rom.up at (romBaseAddr, 1<<rom.addressWidth) of mbus
    }
    case None =>
  }
  if (internalRam) {
    val ram = new RamFiber(ramSize, initialContent=internalRamContent)
    ram.up at (ramBaseAddr, ramSize) of mbus
    io.ddr.not_connected()
    ramStat := 0
  } else {
    val ram = new Ddr3Fiber(ramSize, io.ddr)
    ram.up at (ramBaseAddr, ramSize) of mbus
    ramStat := ram.cal_stat
  }

  val cpu0 : Core =
    if (withMinimalCore || withSmallCore) new VexiiSmallCore(resetVector=resetVector, bootMemClear=sim, withCaches=withSmallCore)
    else new VexiiCore(resetVector=resetVector, bootMemClear=sim)
  clint.connectCore(cpu0, hartId=0)
  cpu0.connectBus(cbus, iobus)
}

object EndeavourSoc {
  def main(args: Array[String]): Unit = {
    val romContent = Files.readAllBytes(Paths.get("../software/bios/microloader.bin"))
    SpinalConfig(mode=Verilog, targetDirectory="verilog").generate(new EndeavourSoc(
        //withMinimalCore=true,
        withSmallCore=true,
        //internalRam=true,
        //ramSize=32768,
        //internalRamContent=Some(romContent),
        romContent=Some(romContent)
        ))
    //SpinalConfig(mode=Verilog).generate(new EndeavourSoc(withMinimalCore=true))
  }
}
