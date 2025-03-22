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

  def vexii() : VexiiRiscv

  def iBus(): Node
  def dBus(): Node

  def hasL1(): Boolean
  def lsuL1Bus(): Node
}

object Core {
  def minimal(withCaches: Boolean = false): ParamSimple = {
    val param = new ParamSimple()
    param.xlen = 32
    param.hartCount = 1
    param.withMul = true
    param.withDiv = true
    param.privParam.withRdTime = true
    if (withCaches) {
      param.withCaches
      param.lsuL1Coherency = true
      param.fetchMemDataWidthMin = 64
      param.lsuMemDataWidthMin = 64
    }
    param
  }

  def small(): ParamSimple = {
    val param = minimal(withCaches = true)
    param.withRva = true
    param.withRvc = true
    param.withAlignerBuffer = true
    param.privParam.withSupervisor = true
    param.privParam.withUser = true
    param
  }

  def full(): ParamSimple = {
    val param = new ParamSimple()
    param.xlen = 32
    param.hartCount = 1
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
    param
  }
}

class VexiiCore(val param: ParamSimple, resetVector: Long, jtag: Option[Jtag] = None) extends Core {
  param.resetVector = resetVector
  if (jtag.isDefined) {
    param.embeddedJtagTap = true
    param.embeddedJtagCd = ClockDomain.current
    param.privParam.withDebug = true
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
      if (param.privParam.withSupervisor) {
        val sei = misc.InterruptNode.master()
        sei.flag := interrupts.external
        priv.sei << sei
      }
      if (param.privParam.withRdTime) {
        priv.rdtime := time
      }
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
  /*tlcore.dBus.setDownConnection(a = StreamPipe.HALF, d = StreamPipe.M2S_KEEP)
  if (param.lsuL1Enable) {
    tlcore.lsuL1Bus.setDownConnection(a = StreamPipe.HALF, b = StreamPipe.HALF_KEEP, c = StreamPipe.FULL, d = StreamPipe.M2S_KEEP, e = StreamPipe.HALF)
  }*/

  override def vexii() : VexiiRiscv = tlcore.logic.core

  override def iBus(): Node = tlcore.iBus
  override def dBus(): Node = tlcore.dBus

  override def hasL1(): Boolean = param.lsuL1Enable
  override def lsuL1Bus(): Node = tlcore.lsuL1Bus
}
