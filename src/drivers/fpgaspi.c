/*
 * fpgaspi.c
 *
 *  Created on: Apr 11, 2011
 *      Author: tgorski
 */

#include "pwrmgr.h"

#include "pins.h"
#include "utils.h"
#include "CTP7_SPI_addrs.h"
#include "rtc.h"
#include "intc.h"
#include "timer_callback.h"
#include "fpgaspi.h"
#include "gpio.h"
#include "nonvolatile.h"

#define XFER_BLK_SIZE               (32)

#define FPGASPI_Clear_IRQ_Mask     {AVR32_SPI1.idr = 0xffffffff; }

volatile avr32_spi_csr0_t* pCSR;         // pointer to chip select register
unsigned char scanslv_detect_mask;
int scanslv_detect_ctr;
unsigned long spi_detect_fail2_ctr = 0;      // counter incremented every time the secondary SPI detect fails



int scanslv_detect_timer_callback(event eventID, void* arg) {
  scanslv_detect_ctr--;
  if (scanslv_detect_ctr)
    return 1;               // exit routine, stay resident in callback table
  return 0;
}


int fpgaspi_init(const unsigned char deviceID) {
  // This init function sets up the SPI1 peripheral for FPGA SPI I/O operation.
  // It should be called each time a FPGA SPI transfer is made, because of the possibility
  // that SPI1 is being shared with other non-FPGA devices with very different settings
  // Call returns 1 if the init is successful, 0 otherwise.
  
  gpio_spi1_mode();                   // set GPIO pins for SPI1 mode
  // disable SPI interface
  AVR32_SPI1.CR.spidis = 1;          // disable spi port

  // initialize mode register
  AVR32_SPI1.mr = 0;              // preset mode reg to all zeros
  AVR32_SPI1.MR.mstr = 1;         // master mode
  AVR32_SPI1.MR.ps = 0;           // fixed peripheral select
  AVR32_SPI1.MR.pcs = deviceID & 0x3;     // select correct chip select
  AVR32_SPI1.MR.dlybcs = 1;

  switch (deviceID) {
    case 0:
      AVR32_SPI1.csr0 = 0;
      pCSR = &(AVR32_SPI1.CSR0);
      break;

    case 1:
      AVR32_SPI1.csr1 = 0;
      pCSR = (volatile avr32_spi_csr0_t*) &(AVR32_SPI1.CSR1);
      break;

    case 2:
      AVR32_SPI1.csr2 = 0;
      pCSR = (volatile avr32_spi_csr0_t*) &(AVR32_SPI1.CSR2);
      break;

    case 3:
      AVR32_SPI1.csr3 = 0;
      pCSR = (volatile avr32_spi_csr0_t*) &(AVR32_SPI1.CSR3);
      break;

    default:
      return 0;
  }

  // initialize selected CSR;
  pCSR->cpol = 0;         // DEBUG:  try 1, original value = 0
  pCSR->ncpha = 1;        // DEBUG, try 0, original vaule = 1
  pCSR->csnaat = 0;       // chip select rise after every transfer
  pCSR->csaat = 1;        // chip select rises after after last transfer
  pCSR->bits = 0;         // 8 bit transfers
//  pCSR->scbr = 3;       // at 12MHz PBA clk, have 4 MHz SPCK
  pCSR->scbr = 12;   // ****SLOWER CLOCK FOR DEBUG
  pCSR->dlybs = 01;       // something reasonable
  pCSR->dlybct = 00;        // same here

  FPGASPI_Clear_IRQ_Mask;

  AVR32_SPI1.CR.spien = 1;          // enable spi port
  return 1;
}


unsigned char fpgaspi_slave_detect(void) {
  // routine to query fpga SPI interface to see if any FPGA-implemented SPI slave
  // devices are connected to the SPI1 peripheral.  First step is to put the pins
  // in GPIO mode with weak pullups.  Then the _FSIO_SCANSLV pin is asserted (set low)
  // and the routine waits for two ticks of the 200 usec event, at which point the
  // _SS0 thorugh _SS3 pins are sampled.  A logic 0 indicates that a FPGA-slave is
  // attached to that port.  The SPI1 interface is then returned to SPI mode and the
  // routine returns a mask indicating the detected FPGA slaves.
  unsigned char dbuf[PERIPH_VALIDATION_SPI_LEN];
  unsigned char pbuf[PERIPH_VALIDATION_SPI_LEN];
  int i1;
  gpio_scanslv_mode();
  scanslv_detect_ctr = 2;         // initialize isr counter
  scanslv_detect_mask = 0;        // clear detect mask
  register_timer_callback(scanslv_detect_timer_callback, TEVENT_200USEC);
  while (scanslv_detect_ctr) Service_Watchdog();  // wait for ISR to complete
  // read individual bits
  if (gpio_get_pin_value(_FSIO_SS0) == 0)
    scanslv_detect_mask |= FPGA0_SPI_DETECT_MASK;
  gpio_spi1_mode();

  if (!scanslv_detect_mask)
    // return zero detect mask, no slaves yet seen
    return scanslv_detect_mask;

  // New secondary check at 2.2b.  There are strange occurrences reported of CERN of cards that won't boot,
  // apparently as a result of the slave detect issuing a positive signal too quickly.  When cards get in this
  // configuration they tend to stay in it, even after short extraction cycles.  It is very difficult to reproduce,
  // but cards appear to recover and boot normally once the non-recoverable event thresholds for the ZYNQ-based
  // sensors are disabled.  
  // This secondary check will add an additional qualifier on the slave detect logic.  The MMC will examine the
  // top 4 byes of the memory and verify that they are zero.  Then it will write and verify an arbitrary pattern,
  // then write back zeros.  If both verifies are successful, then the slave is detected.
  // Note:  ZYNQ connected at Device ID 0

  do {
    if (!fpgaspi_data_read(0, &dbuf[0], PERIPH_VALIDATION_SPI_ADDR, PERIPH_VALIDATION_SPI_LEN))
      // read failed for some reason
      break;
    // validate all zeroes
    for (i1=0; i1<PERIPH_VALIDATION_SPI_LEN; i1++)
      if (dbuf[i1])
        break;         // nonzero value
    // generate and write pattern buffer with dynamic values
    pbuf[0] = AVR32_TC.channel[1].cv & 0xff;
    pbuf[1] = TIMESTATREC.uptime & 0xff;
    pbuf[2] = AVR32_TC.channel[0].cv & 0xff;
    pbuf[3] = (get_rtc_value() >> 8) & 0xff;
    for (i1=4; i1<PERIPH_VALIDATION_SPI_LEN; i1++)
      pbuf[i1] = 0xff-i1;
    // write pattern to test region of memory
    if (!fpgaspi_data_write(0, &pbuf[0], PERIPH_VALIDATION_SPI_ADDR, PERIPH_VALIDATION_SPI_LEN))
      // write failed for some reason
      break;
    // read pattern back
    if (!fpgaspi_data_read(0, &dbuf[0], PERIPH_VALIDATION_SPI_ADDR, PERIPH_VALIDATION_SPI_LEN))
      // read failed for some reason
      break;
    // verify dynamic pattern
    for (i1=0; i1<PERIPH_VALIDATION_SPI_LEN; i1++)
      if (dbuf[i1] != pbuf[i1])
        break;         // nonzero value
    // write back zeroes for next time
    for (i1=0; i1<PERIPH_VALIDATION_SPI_LEN; i1++)
      pbuf[i1] = 0;
    if (!fpgaspi_data_write(0, &pbuf[0], PERIPH_VALIDATION_SPI_ADDR, PERIPH_VALIDATION_SPI_LEN))
      // write failed for some reason
      break;

    return scanslv_detect_mask;

  } while (0);

  // fall here if loop fails via 'break' statement
  spi_detect_fail2_ctr++;
  return 0; 
 
}


int fpgaspi_data_write(const unsigned char deviceID, const unsigned char* pwbuf, unsigned short saddr, unsigned short len) {
  // subdivide write operation into 32-byte transfers to allow interrupts to periodically
  // execute if pending
  // returns 1 if write transfer occurs, 0 otherwise
  unsigned short cursaddr;
  unsigned short nextsaddr;
  unsigned short remlen;
  unsigned short wrblkremlen;
  const unsigned char* curwptr;
  unsigned long writehaddr;
  unsigned long writeladdr;

  if (!fpgaspi_init(deviceID))
    return 0;
  if ((len == 0) || (len > (MAX_FPGA_MEM_SIZE - saddr)))
    return 0;       // can't handle length
  cursaddr = saddr;
  remlen = len;
  curwptr = pwbuf;
  if (remlen >= XFER_BLK_SIZE)
    wrblkremlen = XFER_BLK_SIZE;
  else
    wrblkremlen = remlen;
  nextsaddr = cursaddr + wrblkremlen;   // next block starting address
  remlen -= wrblkremlen;    // remaining length after current block
  writehaddr = (cursaddr >> 8) & 0xff;
  writeladdr = cursaddr & 0xff;

  while (1) {
    // set write enable latch
    Disable_global_interrupt();
    // send write command and starting address
    AVR32_SPI1.tdr = WRITE_DATA_CMD;
    while (!AVR32_SPI1.SR.tdre);      // wait for byte to move
    AVR32_SPI1.tdr = writehaddr;      // upper address bits
    while (!AVR32_SPI1.SR.tdre);      // wait for byte to move
    AVR32_SPI1.tdr = writeladdr;      // lower bits
    while (!AVR32_SPI1.SR.tdre);      // wait for byte to move
    while (wrblkremlen > 1) {
      AVR32_SPI1.tdr = *curwptr++;
      wrblkremlen--;
      while (!AVR32_SPI1.SR.tdre);      // wait for byte to move
    }
    // perform last transfer
    AVR32_SPI1.tdr = *curwptr++;
    AVR32_SPI1.cr = AVR32_SPI_TDR_LASTXFER_MASK;    // signal last byte
    while (!AVR32_SPI1.SR.txempty);
    Enable_global_interrupt();
    if (!remlen)
      break;              // no more bytes to write
    // calculate size of next block write
    if (remlen < XFER_BLK_SIZE)
      wrblkremlen = remlen;
    else
      wrblkremlen = XFER_BLK_SIZE;
    // update control variables and header values for next block write
    cursaddr = nextsaddr;
    nextsaddr = cursaddr + wrblkremlen;   // next block starting address
    remlen -= wrblkremlen;    // remaining length after current block
    writehaddr = (cursaddr >> 8) & 0xff;
    writeladdr = cursaddr & 0xff;
  }
  return 1;
}


int fpgaspi_data_read(const unsigned char deviceID, unsigned char* prbuf, unsigned short saddr, unsigned short len) {
  // returns 1 if read transfer occurs, 0 otherwise
  // If the length is longer than 32 bytes, the read will be broken up into multiple
  // transfers, allowing interrupts to occur in between the 32-byte reads
  unsigned short cursaddr;
  unsigned short nextsaddr;
  unsigned short remlen;
  unsigned short rdblkremlen;
  unsigned char* currptr;
  unsigned long readhaddr;
  unsigned long readladdr;
  unsigned char rdata;
  unsigned char delayflag;

  if (!fpgaspi_init(deviceID))
    return 0;
  cursaddr = saddr;
  remlen = len;
  currptr = prbuf;
  if (remlen >= XFER_BLK_SIZE)
    rdblkremlen = XFER_BLK_SIZE;
  else
    rdblkremlen = remlen;
  nextsaddr = cursaddr + rdblkremlen;   // next block starting address
  remlen -= rdblkremlen;    // remaining length after current block
  readhaddr = (cursaddr >> 8) & 0xff;
  readladdr = cursaddr & 0xff;
  delayflag = 1;
  while (1) {
    Disable_global_interrupt();
    // send read command and starting address
    AVR32_SPI1.tdr = READ_DATA_CMD;
    while (!AVR32_SPI1.SR.tdre);      // wait for byte to move
    rdata = AVR32_SPI1.rdr;       // unload read data register
    AVR32_SPI1.tdr = readhaddr;     // upper address bits
    while (!AVR32_SPI1.SR.tdre);      // wait for byte to move
    rdata = AVR32_SPI1.rdr;       // unload read data register
    AVR32_SPI1.tdr = readladdr;     // lower bits
    while (!AVR32_SPI1.SR.tdre);    // wait for byte to move
    rdata = AVR32_SPI1.rdr;       // unload read data register
    while (rdblkremlen > 1) {
      AVR32_SPI1.tdr = 0;       // dummy write value
      rdblkremlen--;
      while (!AVR32_SPI1.SR.tdre);      // wait for next byte to be available
      if (delayflag)
        delayflag = 0;
      else {
        rdata = AVR32_SPI1.rdr;
        *currptr++ = rdata;
      }
    }
    // perform last transfer
    AVR32_SPI1.tdr = 0;
    AVR32_SPI1.cr = AVR32_SPI_TDR_LASTXFER_MASK;    // signal last byte
    while (!AVR32_SPI1.SR.tdre);
    if (!delayflag) {
      rdata = AVR32_SPI1.rdr;
      *currptr++ = rdata;
    }
    while (!AVR32_SPI1.SR.txempty);
    rdata = AVR32_SPI1.rdr;
    *currptr++ = rdata;
    Enable_global_interrupt();
    if (!remlen)
      break;              // no more bytes to write
    // calculate size of next block read
    if (remlen < XFER_BLK_SIZE)
      rdblkremlen = remlen;
    else
      rdblkremlen = XFER_BLK_SIZE;
    // update control variables and header values for next block write
    cursaddr = nextsaddr;
    nextsaddr = cursaddr + rdblkremlen;   // next block starting address
    remlen -= rdblkremlen;    // remaining length after current block
    readhaddr = (cursaddr >> 8) & 0xff;
    readladdr = cursaddr & 0xff;
    delayflag = 1;
  }
  return 1;
}


unsigned char fpgaspi_status_read(const unsigned char deviceID) {
  // performs a read of the FPGA SPI interface status register
  unsigned long rdrreg;
  if (!fpgaspi_init(deviceID))
    return 0xff;                      // invalid deviceID
  Disable_global_interrupt();
  AVR32_SPI1.tdr = READ_STATUS_CMD;
  while (!AVR32_SPI1.SR.tdre);      // wait for byte to move
  AVR32_SPI1.tdr = 0;         // dummy value
  AVR32_SPI1.cr = AVR32_SPI_TDR_LASTXFER_MASK;    // signal last byte
  while (!AVR32_SPI1.SR.txempty);   // wait for transmitter completely empty
  rdrreg = AVR32_SPI1.rdr;        // get read data register
  Enable_global_interrupt();
  return (unsigned char) (rdrreg & 0xff);
}


int fpgaspi_ctl_write(const unsigned char deviceID, const unsigned char wrval) {
  // returns 1 if write successful, otherwise 0
  if (!fpgaspi_init(deviceID))
    return 0;
  Disable_global_interrupt();
  AVR32_SPI1.tdr = WRITE_CTL_CMD;
  while (!AVR32_SPI1.SR.tdre);      // wait for byte to move
  AVR32_SPI1.tdr = wrval;
  AVR32_SPI1.cr = AVR32_SPI_TDR_LASTXFER_MASK;
  while (!AVR32_SPI1.SR.txempty);
  return 1;
}




