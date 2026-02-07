package endeavour2

import spinal.core._
import spinal.lib._
import spinal.lib.fsm._
import spinal.lib.bus.amba3.apb.{Apb3, Apb3Config, Apb3SlaveFactory}

// Used for spi flash
class SpiController extends Component {
  val io = new Bundle {
    val spi_ncs = out Bool()
    val spi_ncs_en = out Bool()
    val spi_clk = out Bool()
    val spi_mosi = out Bool()
    val spi_miso = in Bool()
    val apb = slave(Apb3(Apb3Config(
      addressWidth  = 4,
      dataWidth     = 32,
      useSlaveError = false
    )))
  }

  val apb = Apb3SlaveFactory(io.apb)

  val writeCnt = RegInit(U(0, 16 bits))
  val readCnt = RegInit(U(0, 16 bits))
  val data = Reg(Bits(32 bits))
  val writeHasData = RegInit(False)
  val readHasData = RegInit(False)

  apb.readAndWrite(writeCnt, address = 0, bitOffset = 16)
  apb.readAndWrite(readCnt, address = 0, bitOffset = 0)
  apb.read(writeHasData, address = 4)
  apb.read(readHasData, address = 8)
  apb.readAndWrite(data, address = 12)
  apb.onWrite(12)( writeHasData := True )
  apb.onRead(12)( readHasData := False )

  val fsm = new StateMachine {
    val data_shift = Reg(Bits(32 bits))
    val counter = RegInit(U(0, 4 bits))
    counter := counter + 1

    io.spi_ncs_en := True
    io.spi_ncs := False
    io.spi_clk := counter(0)
    io.spi_mosi := data_shift(31)

    val Idle : State = new State with EntryPoint {
      whenIsActive {
        io.spi_ncs_en := False
        io.spi_clk := True
        when (writeCnt =/= 0 && counter === 15) { goto(Write) }
      }
    }
    val Write : State = new State {
      onEntry {
        data_shift := data
        writeHasData := False
      }
      whenIsActive {
        when (counter(0)) { data_shift := data_shift(30 downto 0) ## io.spi_miso }
        when (counter === 15) {
          writeCnt := writeCnt - 1
          when (writeCnt(1 downto 0) === 1) {
            data_shift := data
            writeHasData := False
          }
          when (writeCnt === 1 && readCnt === 0) { goto(Stop) }
          when (writeCnt === 1 && readCnt =/= 0) { goto(Read) }
        }
      }
    }
    val Read : State = new State {
      whenIsActive {
        when (!counter(0)) { data_shift := data_shift(30 downto 0) ## io.spi_miso }
        when (counter === 15) {
          readCnt := readCnt - 1
          when (readCnt(1 downto 0) === 1) {
            data := data_shift
            readHasData := True
          }
          when (readCnt === 1) { goto(Stop) }
        }
      }
    }
    val Stop : State = new State {
      whenIsActive {
        io.spi_ncs := counter(3)
        io.spi_clk := True
        when (counter === 15) { goto(Idle) }
      }
    }
  }
}

// Used for communication with ESP32
class SpiFifoController extends Component {
  val io = new Bundle {
    val spi_ncs = out Bool()
    val spi_clk = out Bool()
    val spi_mosi = out Bool()
    val spi_miso = in Bool()
    val apb = slave(Apb3(Apb3Config(
      addressWidth  = 4,
      dataWidth     = 32,
      useSlaveError = false
    )))
    val interrupt = out Bool()
  }

  val apb = Apb3SlaveFactory(io.apb)

  val interrupt_en = RegInit(False)
  val CPOL = RegInit(True)
  val CPHA = RegInit(False)
  val divisor = RegInit(U(0, 8 bits))
  val divisorCounter = RegInit(U(0, 8 bits))
  val byteCounter = RegInit(U(0, 11 bits))
  val nselect = RegInit(True)
  val bitCounter = RegInit(U(0, 5 bits))
  val dataIn = Reg(Bits(16 bits))
  val dataOut = Reg(Bits(16 bits))

  val mem = Mem(Bits(16 bits), wordCount = 1024)
  val intAddr = Reg(UInt(10 bits))
  val extAddr = Reg(UInt(10 bits))
  val intWrite = False
  val extWrite = False
  val dataOutBuf = Reg(Bits(16 bits))

  val dataOutNext = mem.readWriteSync(intAddr, Mux(RegNext(bitCounter(4)), dataIn(15 downto 8), dataIn(7 downto 0)) ## dataIn(7 downto 0), True, RegNext(intWrite))
  val dataRead = mem.readWriteSync(extAddr, io.apb.PWDATA(7 downto 0) ## io.apb.PWDATA(15 downto 8), True, extWrite)

  io.interrupt := interrupt_en && byteCounter === 0
  io.spi_ncs := nselect
  io.spi_clk := CPOL ^ bitCounter(0)
  io.spi_mosi := dataOut(15)

  apb.readAndWrite(interrupt_en, address = 0, bitOffset = 31)
  apb.readAndWrite(CPOL, address = 0, bitOffset = 30)
  apb.readAndWrite(CPHA, address = 0, bitOffset = 29)
  apb.readAndWrite(divisor, address = 0, bitOffset = 0)

  apb.read(dataRead(7 downto 0) ## dataRead(15 downto 8), address = 4)
  apb.onRead(4)( extAddr := extAddr + 1 )
  apb.onWrite(4)({
    extAddr := extAddr + 1
    extWrite := True
  })

  apb.readAndWrite(byteCounter, address = 8)

  apb.readAndWrite(nselect, address = 12)
  apb.onWrite(12) ({
    extAddr := io.apb.PWDATA(0).asUInt.resized
    intAddr := 0
  })

  when (byteCounter === 0) {
    dataOut := dataOutNext
    dataOutBuf := dataOutNext
  } elsewhen (divisorCounter === 0) {
    divisorCounter := divisor
    when (bitCounter(0) === CPHA) {
      dataIn := dataIn(14 downto 0) ## io.spi_miso
      intWrite := bitCounter(3 downto 1) === 7
    } otherwise {
      when (bitCounter(4 downto 1).asBits === ~(CPHA #* 4)) {
        dataOut := dataOutBuf
      } otherwise {
        dataOut(15 downto 1) := dataOut(14 downto 0)
      }
    }
    when (bitCounter === 4) { intAddr := intAddr + 1 }
    when (bitCounter === 12) { dataOutBuf := dataOutNext }
    when (bitCounter(3 downto 0) === 15) {
      byteCounter := byteCounter - 1
    }
    bitCounter := bitCounter + 1
  } otherwise {
    divisorCounter := divisorCounter - 1
  }
}

case class EpdInterface() extends Bundle {
  val busy_n = in Bool()
  val rst_n = out Bool()
  val dc = out Bool()
  val csb = out Bool()
  val scl = out Bool()
  val sda_IN = in Bool()
  val sda_OUT = out Bool()
  val sda_OE = out Bool()
}

class EpdSpiController extends Component {
  val io = new Bundle {
    val epd = EpdInterface()
    val apb = slave(Apb3(Apb3Config(
      addressWidth  = 4,
      dataWidth     = 32,
      useSlaveError = false
    )))
  }
  val apb = Apb3SlaveFactory(io.apb)

  val rst_n = RegInit(False)
  val dc = Reg(Bool())
  val csb = RegInit(True)
  val scl = RegInit(False)
  val write = RegInit(False)

  val divisor = Reg(UInt(6 bits))
  val divisor_counter = Reg(UInt(6 bits))
  val counter = RegInit(U(0, 5 bits))
  val data = Reg(Bits(8 bits))
  val spi_busy = counter.orR

  io.epd.rst_n := rst_n
  io.epd.dc := dc
  io.epd.csb := csb
  io.epd.scl := scl
  io.epd.sda_OE := write
  io.epd.sda_OUT := data(7)

  apb.readAndWrite(rst_n, 0, bitOffset=0)
  apb.read(~RegNext(io.epd.busy_n) ## spi_busy, 0, bitOffset=1)
  apb.readAndWrite(data, 4)

  apb.onWrite(8)({
    when (~spi_busy) {
      divisor := io.apb.PWDATA(5 downto 0).asUInt
      divisor_counter := io.apb.PWDATA(5 downto 0).asUInt
      counter := 19
      dc := io.apb.PWDATA(8)
      write := io.apb.PWDATA(9)
    }
  })

  when (spi_busy) {
    when (divisor_counter === 0) {
      divisor_counter := divisor
      when (counter === 19) {
        csb := False
      } elsewhen (counter === 1) {
        csb := True
        write := False
      } elsewhen (counter =/= 18) {
        scl := counter(0)
        when (~counter(0)) { data := data(6 downto 0) ## io.epd.sda_IN }
      }
      counter := counter - 1
    } otherwise {
      divisor_counter := divisor_counter - 1
    }
  }
}