package endeavour2

import spinal.core._
import spinal.lib._
import spinal.lib.com.jtag.Jtag
import spinal.lib.bus.tilelink
import spinal.lib.bus.tilelink.fabric.Node

import vexiiriscv.VexiiRiscv
import vexiiriscv.ParamSimple
import vexiiriscv.misc.EmbeddedRiscvJtag
import vexiiriscv.soc.TilelinkVexiiRiscvFiber

trait Core {
  val time = UInt(64 bits)

  val interrupts = new Bundle {
    val timer = Bool()
    val software = Bool()
    val external = Bool()
  }

  def connectBus(cbus: Node, iobus: Node)
  def core() : VexiiRiscv
}

class VexiiCore(resetVector: Long, bootMemClear: Boolean = false, jtag: Option[Jtag] = None) extends Core {
  val param = new ParamSimple()
  param.resetVector = resetVector
  param.xlen = 32
  param.hartCount = 1
  param.bootMemClear = bootMemClear
  param.withCaches
  param.lsuL1Coherency = true

  param.fetchMemDataWidthMin = 64
  param.lsuMemDataWidthMin = 64

  param.withRvc = true
  param.withAlignerBuffer = true
  param.withDispatcherBuffer = true

  param.withMul = true
  param.withDiv = true
  param.withRva = true
  param.withRvZb = false

  param.withMmu = true
  param.withMmuSyncRead

  param.privParam.withSupervisor = true
  param.privParam.withUser = true
  param.privParam.withRdTime = true

  /*param.withRvf = true
  param.fpuFmaFullAccuracy = false
  param.fpuIgnoreSubnormal = true*/

  // Performance optimization
  param.decoders = 2
  param.lanes = 2

  param.fetchL1Ways = 4
  param.lsuL1Ways = 4

  param.fetchL1RefillCount = 3
  param.lsuL1RefillCount = 4
  param.lsuL1WritebackCount = 4

  param.withBtb = true
  param.withGShare = true
  param.withRas = true

  param.fetchL1Prefetch = "nl"
  //param.lsuSoftwarePrefetch = true
  param.lsuHardwarePrefetch = "rpt"
  param.lsuStoreBufferSlots = 4
  param.lsuStoreBufferOps = 32

  // fMax optimization
  param.relaxedBranch = true
  param.relaxedShift = true
  param.relaxedSrc = true
  param.relaxedBtb = true
  param.relaxedDiv = true
  param.relaxedMulInputs = true

  param.withLateAlu = true
  param.allowBypassFrom = 0
  param.storeRs2Late = true

  if (jtag.isDefined) {
    param.embeddedJtagTap = true
    param.embeddedJtagCd = ClockDomain.current
    param.privParam.withDebug = true
  }

  val plugins = param.plugins()
  val tlcore = new TilelinkVexiiRiscvFiber(plugins)
  tlcore.priv match {
    case Some(priv) => new Area {
      val mti, msi, mei, sei = misc.InterruptNode.master()
      mti.flag := interrupts.timer
      msi.flag := interrupts.software
      mei.flag := interrupts.external
      sei.flag := interrupts.external
      priv.mti << mti
      priv.msi << msi
      priv.mei << mei
      priv.sei << sei
      priv.rdtime := time
    }
    case None =>
  }

  if (jtag.isDefined) {
    plugins.foreach{
      case p : EmbeddedRiscvJtag => { fiber.Handle { jtag.get <> p.logic.jtag } }
      case _ =>
    }
  }

  tlcore.iBus.setDownConnection { (down, up) =>
    down.a << up.a.halfPipe().halfPipe()
    up.d << down.d.m2sPipe()
  }
  tlcore.lsuL1Bus.setDownConnection(a = StreamPipe.HALF, b = StreamPipe.HALF_KEEP, c = StreamPipe.FULL, d = StreamPipe.M2S_KEEP, e = StreamPipe.HALF)
  tlcore.dBus.setDownConnection(a = StreamPipe.HALF, d = StreamPipe.M2S_KEEP)

  override def core() : VexiiRiscv = tlcore.logic.core
  override def connectBus(cbus: Node, iobus: Node) = {
    cbus << tlcore.iBus
    cbus << tlcore.lsuL1Bus
    iobus << tlcore.dBus
  }
}

class VexiiSmallCore(resetVector: Long, withCaches: Boolean = true, bootMemClear: Boolean = false) extends Core {
  val param = new ParamSimple()
  param.resetVector = resetVector
  param.xlen = 32
  param.hartCount = 1
  param.withMul = true
  param.withDiv = true
  param.privParam.withRdTime = true
  param.bootMemClear = bootMemClear
  if (withCaches) {
    param.withCaches
    param.lsuL1Coherency = true
  }

  val plugins = param.plugins()
  val tlcore = new TilelinkVexiiRiscvFiber(plugins)
  tlcore.priv match {
    case Some(priv) => new Area {
      val mti, msi, mei = misc.InterruptNode.master()
      mti.flag := interrupts.timer
      msi.flag := interrupts.software
      mei.flag := interrupts.external
      priv.mti << mti
      priv.msi << msi
      priv.mei << mei
      priv.rdtime := time
    }
    case None =>
  }

  override def core() : VexiiRiscv = tlcore.logic.core
  override def connectBus(cbus: Node, iobus: Node) = {
    cbus << tlcore.iBus
    if (withCaches) {
      cbus << tlcore.lsuL1Bus
      iobus << tlcore.dBus
    } else {
      assert(cbus == iobus, "VexiiSmallCore(withCaches = false) requires cbus==iobus")
      cbus << tlcore.dBus
    }
  }
}
