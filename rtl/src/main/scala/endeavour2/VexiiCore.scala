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
  def small(withCaches: Boolean = true): ParamSimple = {  // 0.734 DMIPS/MHz
    val param = new ParamSimple()
    param.xlen = 32
    param.hartCount = 1
    param.withMul = true
    param.withDiv = true
    param.privParam.withRdTime = true
    param.withRvc = true
    param.withAlignerBuffer = true
    if (withCaches) {
      param.withCaches
      param.lsuL1Coherency = true
      param.fetchMemDataWidthMin = 64
      param.lsuMemDataWidthMin = 64
    }
    param
  }

  def medium(withFpu: Boolean = true, withRvd: Boolean = true, withBiggerCache: Boolean = true): ParamSimple = {  // 1.622 DMIPS/MHz
    val param = small(withCaches = true)

    // needed to run linux
    param.withRva = true
    param.withMmu = true
    param.withMmuSyncRead
    param.privParam.withSupervisor = true
    param.privParam.withUser = true

    // cache performance
    param.fetchL1Prefetch = "nl"
    param.fetchL1RefillCount = 3
    param.lsuSoftwarePrefetch = true
    param.lsuStoreBufferSlots = 4
    param.lsuStoreBufferOps = 32
    param.lsuL1RefillCount = 8
    param.lsuL1WritebackCount = 8
    param.lsuHardwarePrefetch = "rpt"
    if (withBiggerCache) {
      param.fetchL1Ways = 4
      param.lsuL1Ways = 4
    }

    // branch prediction
    param.withBtb = true
    param.withGShare = true
    param.withRas = true

    // core performance
    param.withDispatcherBuffer = true
    param.allowBypassFrom = 0
    param.withLsuBypass = true
    param.withLateAlu = true
    param.storeRs2Late = true
    param.divRadix = 4

    // fMax
    param.relaxedBranch = true

    // FPU
    if (withFpu) {
      param.withRvf = true
      param.withRvd = withRvd
      param.fpuMulParam.fmaFullAccuracy = false
      param.fpuIgnoreSubnormal = true
    }

    param
  }

  def full(): ParamSimple = {  // 2.459 DMIPS/MHz
    val param = medium()

    // performance
    param.decoders = 2
    param.lanes = 2

    // features
    //param.withRvZb = true   // reduces fMax 220 MHz -> 136 MHz

    // fMax
    param.relaxedBtb = true
    /*param.relaxedBranch = true
    param.relaxedShift = true
    param.relaxedSrc = true
    param.relaxedBtb = true
    param.relaxedDiv = true
    param.relaxedMulInputs = true*/

    param
  }
}

class VexiiCore(val param: ParamSimple, resetVector: Long, hartId: Int = 0, jtag: Option[Jtag] = None) extends Core {
  param.resetVector = resetVector
  if (jtag.isDefined) {
    param.embeddedJtagTap = true
    param.embeddedJtagCd = ClockDomain.current
    param.privParam.withDebug = true
  }
  val plugins = param.plugins(hartId)
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
  if (param.lsuL1Enable) {
    tlcore.lsuL1Bus.setDownConnection(a = StreamPipe.HALF, b = StreamPipe.HALF_KEEP, c = StreamPipe.FULL, d = StreamPipe.M2S_KEEP, e = StreamPipe.HALF)
  }
  //tlcore.dBus.setDownConnection(a = StreamPipe.HALF, d = StreamPipe.M2S_KEEP)

  override def vexii() : VexiiRiscv = tlcore.logic.core

  override def iBus(): Node = tlcore.iBus
  override def dBus(): Node = tlcore.dBus

  override def hasL1(): Boolean = param.lsuL1Enable
  override def lsuL1Bus(): Node = tlcore.lsuL1Bus
}
