/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ish_aontask.h"
#include <config_chip.h>
#include <power_mgt.h>
#include "ia_structs.h"
#include "ish_aon_share.h"

void ish_aon_main(void);

/* 8 byte reserved on stack, or GDB goes wrong during source code stepping */
#define AON_SP_RESERVED (8)

/* tss segment for aon task */
tss_t aon_tss = {
	.prev_task_link = 0,
	.reserved1 = 0,
	.esp0 = (char *)(CONFIG_ISH_AON_SRAM_ROM_START - AON_SP_RESERVED),
	/*All segments are LDT = 1, DPL = 0, index is 0 for CS and 1 for
	 * all the rest
	 */
	.ss0 = 0xc,
	.reserved2 = 0,
	.esp1 = 0,
	.ss1 = 0,
	.reserved3 = 0,
	.esp2 = 0,
	.ss2 = 0,
	.reserved4 = 0,
	.cr3 = 0,
	.eip = (uint32_t)&ish_aon_main,
	.eflags = 0,
	.eax = 0,
	.ecx = 0,
	.edx = 0,
	.ebx = 0,
	.esp = CONFIG_ISH_AON_SRAM_ROM_START,
	.ebp = AON_SP_RESERVED,
	.esi = 0,
	.edi = 0,
	.es = 0xc,
	.reserved5 = 0,
	.cs = 0x4,
	.reserved6 = 0,
	.ss = 0xc,
	.reserved7 = 0,
	.ds = 0xc,
	.reserved8 = 0,
	.fs = 0xc,
	.reserved9 = 0,
	.gs = 0xc,
	.reserved10 = 0,
	.ldt_seg_selector = 0,
	.reserved11 = 0,
	.trap_debug = 0,
	.iomap_base_addr = 0x67
};

/* The LDT CS and DS descriptors are initialized with base = 0,
 * limit = 0xFFFFFFFF, DPL = 0, Present = 1, Page Granularity = 1, S = 1.
 * The type is 0xb for CS and 0x3 for DS
 */

ldt_entry_t aon_ldt[2] = {
	{ .dword_one = 0xFFFF, .dword_two = ((0x9b << 8) | (0xCF << 16)) },
	{ .dword_one = 0xFFFF, .dword_two = ((0x93 << 8) | (0xCF << 16)) }
};

/* shared data structure between main FW and aon task */
ish_aon_share_t aon_share = {
	.tss_ptr = (tss_t *)((uint32_t)&aon_tss),
	.ldt_ptr = (uint32_t)&aon_ldt,
	.ldt_size = sizeof(aon_ldt),
};

static void handle_d0i2(void)
{
	/* TODO set SRAM to retention mode*/

	/* wakeup from PMU interrupt */
	/* ish_halt(); */

	/* TODO set SRAM to normal operation mode */
}

static void handle_d0i3(void)
{
	/* TODO store main FW 's context to IMR DDR from main sram */
	/* TODO power off main SRAM */

	/* wakeup from PMU interrupt */
	/* ish_halt(); */

	/* TODO power on main SRAM */
	/* TODO restore main FW 's context to SRAM from IMR DDR */
}

static void handle_d3(void)
{
	/* TODO store main FW 's context to IMR DDR from main sram */
	/* TODO power off main SRAM */

	/* TODO handle D3 */
}

static void handle_reset_prep(void)
{
	/* TODO store main FW 's context to IMR DDR from main sram */
	/* TODO power off main SRAM */

	/* TODO handle reset prep */
}

static void handle_unknown_state(void)
{
	/* TODO store main FW 's context to IMR DDR from main sram */
	/* TODO power off main SRAM */

	/* TODO handle unknown state */
}

void ish_aon_main(void)
{
	/* TODO reset IDT */

	while (1) {
		switch (aon_share.pm_state) {
		case ISH_PM_STATE_D0I2:
			handle_d0i2();
			break;
		case ISH_PM_STATE_D0I3:
			handle_d0i3();
			break;
		case ISH_PM_STATE_D3:
			handle_d3();
			break;
		case ISH_PM_STATE_RESET_PREP:
			handle_reset_prep();
			break;
		default:
			handle_unknown_state();
			break;
		}

		/* switch back to main FW */
		__asm__ volatile("iret ;\n\t");
	}
}
