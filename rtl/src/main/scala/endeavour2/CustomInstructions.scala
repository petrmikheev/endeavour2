package endeavour2

import spinal.core._
import spinal.lib._
import spinal.lib.pipeline.Stageable
import vexiiriscv.Generate.args
import vexiiriscv.{Global, ParamSimple, VexiiRiscv}
import vexiiriscv.compat.MultiPortWritesSymplifier
import vexiiriscv.riscv.{IntRegFile, RS1, RS2, Riscv}
import vexiiriscv.execute._

object CustomInstructionsPlugin{
  val MIXRGB = IntRegFile.TypeR(M"0000000----------000-----0001011")
}

class CustomInstructionsPlugin(var layer: LaneLayer, var aluAt : Int = 0, var formatAt : Int = 0) extends ExecutionUnitElementSimple(layer) {

  val logic = during setup new Logic {
    awaitBuild()
    assert(Riscv.XLEN.get == 32)

    val wb = newWriteback(ifp, formatAt)

    val mixrgb = add(CustomInstructionsPlugin.MIXRGB).spec
    mixrgb.addRsSpec(RS1, executeAt = 0)
    mixrgb.addRsSpec(RS2, executeAt = 0)

    uopRetainer.release()

    val process = new el.Execute(aluAt) {
      val rs1 = el(IntRegFile, RS1).asUInt
      val rs2 = el(IntRegFile, RS2).asUInt

      val rd = UInt(32 bits)
      rd( 4 downto  0) := (rs1( 4 downto  0) +^ rs2( 4 downto  0)) >> 1;
      rd(10 downto  5) := (rs1(10 downto  5) +^ rs2(10 downto  5)) >> 1;
      rd(15 downto 11) := (rs1(15 downto 11) +^ rs2(15 downto 11)) >> 1;
      rd(20 downto 16) := (rs1(20 downto 16) +^ rs2(20 downto 16)) >> 1;
      rd(26 downto 21) := (rs1(26 downto 21) +^ rs2(26 downto 21)) >> 1;
      rd(31 downto 27) := (rs1(31 downto 27) +^ rs2(31 downto 27)) >> 1;

      wb.valid := SEL
      wb.payload := rd.asBits
    }
  }
}
