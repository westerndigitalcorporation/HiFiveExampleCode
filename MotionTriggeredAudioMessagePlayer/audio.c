// Copyright 2018 Western Digital Corporation or its affiliates
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// audio.c Risc-V audio mono playback activated when pin 18 is grounded (falling edge).
// Intended to work with modified Adafruit Wave Shield v1.1.
//
// Wave Shield Board modifications as follows (refer to schematics file wave11schem.png):
// Do not apply the jumper wiring instructions for JP13 in the Wave Shield assembly PDF.
//	JP13	GPIO D#		SPI signal
//	1	D10		CS_
//	2	D13		SCK
//	3	D11		MOSI
//	4	GND		LCS_ (DAC Latch always asserted)
//
// Input: GPIO12 (SiFive pin 18), (Wave Shield pin 4 Analog In) is falling edge trigger to start playback of audio data.
// Attach button to ground or sensor to start playback. Volume control for amplification.
// Green LED is on during playback.
//
// Note: The SD card on the Wave Shield is not used in this application.
//
// 1/26/2018 Shaun Astarabadi

#include <stdio.h>
#include <stdlib.h>
#include "platform.h"
#include <string.h>
#include "plic/plic_driver.h"
#include "encoding.h"
#include <unistd.h>
#include "stdatomic.h"

// audio data stored in code-space flash scaled for 12bit DAC with minimum sample value = 0.
//#include "sinewave.h"	// test 1kHz sine wave
#include "wavefile.h"	// audio playback sample file (converted from wave format using my Matlab program wave2header.m)

void initializtion (void);
void spi_tx16(uint16_t bytes);

int play_audio = 1;		// playback 0=stopped, 1=playing

// Structures for registering different interrupt handlers
// for different parts of the application.
typedef void (*function_ptr_t) (void);

void no_interrupt_handler (void) {};

function_ptr_t g_ext_interrupt_handlers[PLIC_NUM_INTERRUPTS];

// Instance data for the PLIC.
plic_instance_t g_plic;

/*Entry Point for PLIC Interrupt Handler*/
void handle_m_ext_interrupt()
{
  plic_source int_num  = PLIC_claim_interrupt(&g_plic);
  if ((int_num >=1 ) && (int_num < PLIC_NUM_INTERRUPTS)) {
    g_ext_interrupt_handlers[int_num]();
  }
  else {
    exit(1 + (uintptr_t) int_num);
  }
  PLIC_complete_interrupt(&g_plic, int_num);
}

/*Entry Point for Machine Timer Interrupt Handler*/
void handle_m_time_interrupt()
{
  static unsigned int index=0;
  volatile uint16_t w;

  clear_csr(mie, MIP_MTIP);

  // Reset the timer for 3s in the future.
  // This also clears the existing timer interrupt.
  volatile uint64_t * mtime       = (uint64_t*) (CLINT_CTRL_ADDR + CLINT_MTIME);
  volatile uint64_t * mtimecmp    = (uint64_t*) (CLINT_CTRL_ADDR + CLINT_MTIMECMP);
  uint64_t now = *mtime;
  //uint64_t then = now + 2 * RTC_FREQ;	// 2 sec original demo code
  uint64_t then = now + 3;	// now+3 ticks = about 11kHz rate to work with 11.025kHz wave audio
  *mtimecmp = then;

  if(play_audio)
  {
    GPIO_REG(GPIO_OUTPUT_VAL) &= ~(0x1 << GREEN_LED_OFFSET);	// pin 3 delta T is 1.26us to fill SPI fifo
    if(index >= SAMPLES)
    {
      index = 0;	// for next time
      play_audio = 0;	// stop
    }
    // send the audio sample to the DAC over SPI interface
    w = wave[index++] & 0x0fff;
    w |= (1<<14)|(1<<13)|(1<<12); 	// control bit 15:A_=0|14:BUF=1|13:GA_=1|12:SHDN_=1
    spi_tx16(w);			// 16 bit output: 4b control+12 sample
    GPIO_REG(GPIO_OUTPUT_VAL) |= (0x1 << GREEN_LED_OFFSET);	// pin 3 measure max sample rate ~= 10.92kHz
  }

  // Re-enable the timer interrupt.
  set_csr(mie, MIP_MTIP);
}

// To UART console
const char * banner_msg =
" \
\n\
	Audio Application\n\
	1/16/2018 - Shaun Astarabadi\n\
\n\
";

void print_banner()
{
  write (STDOUT_FILENO, banner_msg, strlen(banner_msg));
}

// Trigger playback
void button_0_handler(void)
{
  // Red LED on
  GPIO_REG(GPIO_OUTPUT_VAL) |= (0x1 << RED_LED_OFFSET);

  play_audio = 1;	// start playing audio, will be cleared when playback is finished

  // Clear the GPIO Pending interrupt by writing 1.
  GPIO_REG(GPIO_RISE_IP) = (0x1 << BUTTON_0_OFFSET);
}

// calculate SPI clock divisor
unsigned int spi_divisor(unsigned long fsck)
{
  unsigned long fin = get_cpu_freq();
  unsigned int div;

  div = ((fin/fsck)-2) >> 1;
  printf("core freq= %d Hz SPI fsck= %d Hz div= %d\n", fin, fsck, div);

  return div;
}

void spi_init(void)
{
  unsigned int div = spi_divisor(10e6);	// target fsck=10MHz

  // SPI1 port configuration
  SPI1_REG(SPI_REG_FCTRL) 	= 0;	// SPI Flash mode OFF

  SPI1_REG(SPI_REG_SCKDIV) 	= div;	// div=12 for fsck=10MHz clock: fsck = fin/2(div+1); fin=264MHz VCO freq
  SPI1_REG(SPI_REG_SCKMODE) 	= 0; 	// ~(SPI_SCK_PHA + SPI_SCK_POL)
  SPI1_REG(SPI_REG_CSID) 	= 0;	// CS = SS0
  SPI1_REG(SPI_REG_CSDEF) 	= 1; 	// CS default is high
  SPI1_REG(SPI_REG_CSMODE) 	= SPI_CSMODE_AUTO; // SPI_CSMODE_HOLD; //SPI_CSMODE_AUTO;

  SPI1_REG(SPI_REG_DCSSCK) 	= 1;	// CS to SCK Delay
  SPI1_REG(SPI_REG_DSCKCS) 	= 1;	// SCK to CS Delay
  SPI1_REG(SPI_REG_DINTERCS) 	= 1;	// Minimum CS inactive time
  SPI1_REG(SPI_REG_DINTERXFR) 	= 0;	// Maximum interframe delay

  SPI1_REG(SPI_REG_FMT) 	=  SPI_FMT_LEN(8) 	// [19:16] 8 bit data frame
				 | SPI_FMT_DIR(SPI_DIR_TX)| SPI_FMT_ENDIAN(SPI_ENDIAN_MSB) | SPI_FMT_PROTO(SPI_PROTO_S);

  // SPI1_REG(SPI_REG_TXFIFO) 	= bytes to be transmitted, if bit31 is set fifo buffer full
  // SPI1_REG(SPI_REG_RXFIFO) 	= bytes received, if bit31=0 buffer not empty
  // check (SPI1_REG(SPI_REG_TXFIFO) & SPI_TXFIFO_FULL) before write
  // check (SPI1_REG(SPI_REG_RXFIFO) & SPI_RXFIFO_EMPTY) before read

						// See notes in spi_tx16()
  SPI1_REG(SPI_REG_TXCTRL) 	= SPI_TXWM(1);	// threshold at which the Tx FIFO watermark interrupt triggers
  SPI1_REG(SPI_REG_RXCTRL) 	= SPI_RXWM(0);

  //SPI1_REG(SPI_REG_IE)	// no interrupt setting
  //SPI1_REG(SPI_REG_IP)

  // configure GPIO pins for alternate IOFunction
  GPIO_REG(GPIO_IOF_EN)  |=   (1<<IOF_SPI1_SS0) | (1<<IOF_SPI1_MOSI) | (1<<IOF_SPI1_MISO) | (1<<IOF_SPI1_SCK); // pins: [10:13]
  GPIO_REG(GPIO_IOF_SEL) &= ~((1<<IOF_SPI1_SS0) | (1<<IOF_SPI1_MOSI) | (1<<IOF_SPI1_MISO) | (1<<IOF_SPI1_SCK)); // pins: [10:13]
}

void initializtion (void)
{
	// Disable the machine & timer interrupts until setup is done.
	clear_csr(mie, MIP_MEIP);
	clear_csr(mie, MIP_MTIP);

	// initialize interrupt table
	for (int ii = 0; ii < PLIC_NUM_INTERRUPTS; ii ++){
		g_ext_interrupt_handlers[ii] = no_interrupt_handler;
	}

	// hook our interrupt handlers
	g_ext_interrupt_handlers[INT_DEVICE_BUTTON_0] = button_0_handler;

	print_banner();

	// Have to enable the interrupt both at the GPIO level,
	// and at the PLIC level.
	PLIC_enable_interrupt (&g_plic, INT_DEVICE_BUTTON_0);

	// Priority must be set > 0 to trigger the interrupt.
	PLIC_set_priority(&g_plic, INT_DEVICE_BUTTON_0, 1);

	GPIO_REG(GPIO_RISE_IE)    |=  (1 << BUTTON_0_OFFSET);
	GPIO_REG(GPIO_OUTPUT_EN)  &= ~(1 << BUTTON_0_OFFSET);
	GPIO_REG(GPIO_PULLUP_EN)  |=  (1 << BUTTON_0_OFFSET);
	GPIO_REG(GPIO_INPUT_EN)   |=  (1 << BUTTON_0_OFFSET);

	// Set up the GPIOs such that the LED GPIO
	// can be used as both Inputs and Outputs.
	GPIO_REG(GPIO_INPUT_EN)    &= ~((0x1<< RED_LED_OFFSET) | (0x1<< GREEN_LED_OFFSET) | (0x1 << BLUE_LED_OFFSET)) ;
	GPIO_REG(GPIO_OUTPUT_EN)   |=  ((0x1<< RED_LED_OFFSET)| (0x1<< GREEN_LED_OFFSET) | (0x1 << BLUE_LED_OFFSET)) ;
	GPIO_REG(GPIO_OUTPUT_VAL)  |=   (0x1 << BLUE_LED_OFFSET) ;
	GPIO_REG(GPIO_OUTPUT_VAL)  &=  ~((0x1<< RED_LED_OFFSET) | (0x1<< GREEN_LED_OFFSET)) ;


	// Set the machine timer to go off in 2 seconds.
	volatile uint64_t * mtime       = (uint64_t*) (CLINT_CTRL_ADDR + CLINT_MTIME);
	volatile uint64_t * mtimecmp    = (uint64_t*) (CLINT_CTRL_ADDR + CLINT_MTIMECMP);
	uint64_t now = *mtime;
	uint64_t then = now + 2*RTC_FREQ;
	*mtimecmp = then;

	// Enable the Machine-External bit in MIE
	set_csr(mie, MIP_MEIP);

	// Enable the Machine-Timer bit in MIE
	set_csr(mie, MIP_MTIP);

	// Enable interrupts in general.
	set_csr(mstatus, MSTATUS_MIE);
}

// 8 bit SPI output with CS
void spi_tx(unsigned char byte)
{
  while(SPI1_REG(SPI_REG_TXFIFO) & SPI_TXFIFO_FULL);
  SPI1_REG(SPI_REG_TXFIFO) = byte;		// automatic CS usage supports 8 bit transfers only
}

// 16 bit output with chip select asserted until all bits are transmitted.
// FIFO depth for tx must be selected to be 1 during initialization (SPI_REG_TXCTRL). Function waits for 1st byte to
// complete transmission before setting CS back to auto mode to maintain CS.
// Automatic CS function is only defined for up to 8 bits in SiFive implementation.
void spi_tx16(uint16_t bytes)
{
  SPI1_REG(SPI_REG_CSMODE) 	= SPI_CSMODE_HOLD;	// force CS low
  while(SPI1_REG(SPI_REG_TXFIFO) & SPI_TXFIFO_FULL);
  SPI1_REG(SPI_REG_TXFIFO) = (bytes >> 8) & 0xff;	// high byte
  while(SPI1_REG(SPI_REG_TXFIFO) & SPI_TXFIFO_FULL);
  SPI1_REG(SPI_REG_TXFIFO) = bytes & 0xff;		// low byte
  while((SPI1_REG(SPI_REG_IP) & SPI_IP_TXWM) != SPI_IP_TXWM);  // wait for first byte to go through to avoid CS change
  SPI1_REG(SPI_REG_CSMODE) 	= SPI_CSMODE_AUTO; 	// go back to auto mode
}


int main(int argc, char **argv)
{
	volatile size_t i=0;
	/**************************************************************************
	* Set up the PLIC
	*************************************************************************/
	PLIC_init(&g_plic,
		PLIC_CTRL_ADDR,
		PLIC_NUM_INTERRUPTS,
		PLIC_NUM_PRIORITIES);

	initializtion();	// ports
	spi_init();

	while (1)
	{
	    i++;	// nothing
	    // ------ TEST --------
//	    spi_tx(0x55);	// byte output
//	    spi_tx(0xAA);
//	    spi_tx16(0x55AA);	// 16 bit output
	}

	return 0;
}
