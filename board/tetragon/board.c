/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* HyperDebug board configuration */

#include "clock.h"
#include "clock_chip.h"
#include "common.h"
#include "ec_version.h"
#include "queue_policies.h"
#include "registers.h"
#include "task.h"
#include "timer.h"
#include "hooks.h"
#include "usb-stream.h"

/* Must come after other header files and interrupt handler declarations */
#include "gpio_list.h"

void board_config_pre_init(void)
{
}

/******************************************************************************
 * Define the strings used in our USB descriptors.
 */

const void *const usb_strings[] = {
	[USB_STR_DESC] = usb_string_desc,
	[USB_STR_VENDOR] = USB_STRING_DESC("Google LLC"),
	[USB_STR_PRODUCT] = USB_STRING_DESC("Tetragon CMSIS-DAP"),
	[USB_STR_SERIALNO] = 0,
	[USB_STR_VERSION] = USB_STRING_DESC(CROS_EC_VERSION32),
	[USB_STR_CONSOLE_NAME] = USB_STRING_DESC("Tetragon Shell"),
	[USB_STR_SPI_NAME] = USB_STRING_DESC("SPI"),
	[USB_STR_CMSIS_DAP_NAME] = USB_STRING_DESC("I2C CMSIS-DAP"),
	[USB_STR_USART2_STREAM_NAME] = USB_STRING_DESC("UART2"),
	[USB_STR_USART3_STREAM_NAME] = USB_STRING_DESC("UART3"),
	[USB_STR_USART4_STREAM_NAME] = USB_STRING_DESC("UART4"),
	[USB_STR_USART5_STREAM_NAME] = USB_STRING_DESC("UART5"),
	[USB_STR_DFU_NAME] = USB_STRING_DESC("DFU"),
};

BUILD_ASSERT(ARRAY_SIZE(usb_strings) == USB_STR_COUNT);

/******************************************************************************
 * Initialize board.  (More initialization done by hooks in other files.)
 */
#ifdef SECTION_IS_RW

static void board_init(void)
{
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_PRE_DEFAULT);

bool half = false;

static void board_tick(void)
{
	gpio_set_level(GPIO_TX_LED, half);
	half = !half;
}
DECLARE_HOOK(HOOK_TICK, board_tick, HOOK_PRIO_DEFAULT);


static int command_reinit(int argc, const char **argv)
{
	/* Let every module know to re-initialize to power-on state. */
	hook_notify(HOOK_REINIT);

	return EC_SUCCESS;
}

DECLARE_CONSOLE_COMMAND_FLAGS(
	reinit, command_reinit, "",
	"Stop any ongoing operation, revert to power-on state.",
	CMD_FLAG_RESTRICTED);

#endif

#ifdef SECTION_IS_RO
void __keep exception_panic(void)
{
}

/*
 * Load stack pointer and reset vector from an ARM vector table, and jump.
 */
static void jump_to_arm_reset_vector(uint32_t addr)
{
	/*
	 * The first 32-bit entry ARM vector table is the initial value of the
	 * stack pointer, the second entry is the reset vector.
	 */
	asm("mov r1, %0\n"
	    /* Load stack pointer */
	    "ldr r0, [r1, 0]\n"
	    "msr msp, r0\n"
	    /*
	     * Memory barrier to ensure subsequent instructions uses modified
	     * stack pointer.
	     */
	    "isb\n"
	    /* Load reset vector */
	    "ldr r0, [r1, 4]\n"
	    /* Jump without saving return address (would modify msp) */
	    "bx r0\n"
	    : /* no outputs */
	    : "r"(addr)
	    :);
}

static void jump_to_rw(void)
{
	jump_to_arm_reset_vector(CONFIG_PROGRAM_MEMORY_BASE +
				 CONFIG_RW_MEM_OFF);
}

void halt_led(void);

void main(void) {

	jump_to_rw();

	//halt_led();
  
	// PIO2_7, PIO2_8

	LPC_GPIO_DIR(LPC_GPIO_BASE(2)) = 0x0180;

	LPC_GPIO_DATA(LPC_GPIO_BASE(2), 0x0180) = 0x0100;

	for (;;);
}
#endif
