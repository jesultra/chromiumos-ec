/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <console.h>
#include <task.h>
#include <system.h>
#include <hwtimer.h>
#include <util.h>
#include "aontaskfw/ish_aon_share.h"
#include "power_mgt.h"

#ifdef CONFIG_ISH_PM_DEBUG
#define CPUTS(outstr) cputs(CC_SYSTEM, outstr)
#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_SYSTEM, format, ##args)
#else
#define CPUTS(outstr)
#define CPRINTS(format, args...)
#define CPRINTF(format, args...)
#endif

typedef struct {
	ish_aon_share_t *p_aon_share;
	int aon_tss_selector[2];
} __attribute__((packed)) pm_ctx_t;

static pm_ctx_t pm_ctx = {
	.p_aon_share = (ish_aon_share_t *)CONFIG_ISH_AON_SRAM_BASE_START
};

typedef struct {
	uint64_t d0i0_cnt;
	uint64_t d0i0_time_us;

#ifdef CONFIG_ISH_PM_D0I1
	uint64_t d0i1_cnt;
	uint64_t d0i1_time_us;
#endif

#ifdef CONFIG_ISH_PM_D0I2
	uint64_t d0i2_cnt;
	uint64_t d0i2_time_us;
#endif

#ifdef CONFIG_ISH_PM_D0I3
	uint64_t d0i3_cnt;
	uint64_t d0i3_time_us;
#endif

} __attribute__((packed)) pm_stats_t;

static pm_stats_t pm_stats;

#ifdef CONFIG_ISH_PM_AONTASK

#define GEN_GDT_DESC_LO(base, limit, flags)                                    \
	((((limit) >> 12) & 0xFFFF) | (((base)&0xFFFF) << 16))

#define GEN_GDT_DESC_HI(base, limit, flags)                                    \
	((((base) >> 16) & 0xFF) | (((flags) << 8) & 0xFF00) |                 \
	 (((limit) >> 12) & 0xFF0000) | ((base)&0xFF000000) | 0xc00000)

/* The GDT  - initialized in init.S */
extern gdt_descriptor_t __gdt[6];
extern gdt_header_t __gdt_ptr[1];

/* TSS desccriptor for saving main FW's context during aontask switching */
tss_t main_tss = { .prev_task_link = 0,
		   .reserved1 = 0,
		   .esp0 = 0,
		   .ss0 = 0,
		   .reserved2 = 0,
		   .esp1 = 0,
		   .ss1 = 0,
		   .reserved3 = 0,
		   .esp2 = 0,
		   .ss2 = 0,
		   .reserved4 = 0,
		   .cr3 = 0,
		   .eip = 0,
		   .eflags = 0,
		   .eax = 0,
		   .ecx = 0,
		   .edx = 0,
		   .ebx = 0,
		   .esp = 0,
		   .ebp = 0,
		   .esi = 0,
		   .edi = 0,
		   .es = 0,
		   .reserved5 = 0,
		   .cs = 0,
		   .reserved6 = 0,
		   .ss = 0,
		   .reserved7 = 0,
		   .ds = 0,
		   .reserved8 = 0,
		   .fs = 0,
		   .reserved9 = 0,
		   .gs = 0,
		   .reserved10 = 0,
		   .ldt_seg_selector = 0,
		   .reserved11 = 0,
		   .trap_debug = 0,
		   .iomap_base_addr = 0x67 };

/**
 * add new entry in GDT
 *
 * @param desc_lo	lower DW of the entry descriptor
 * @param desc_hi	high DW of the entry descriptor
 *
 * @return		the descriptor selector index of the added entry
 */
static uint32_t add_gdt_entry(uint32_t desc_lo, uint32_t desc_hi)
{
	volatile uint32_t *ptr;

	volatile uint16_t *limit = (volatile uint16_t *)&(__gdt_ptr[0].limit);

	ptr = (volatile uint32_t *)&(__gdt[(*limit) >> 3]);
	ptr[0] = desc_lo;
	ptr[1] = desc_hi;

	*limit += sizeof(gdt_descriptor_t);

	return *limit - sizeof(gdt_descriptor_t);
}

static void init_aon_task(void)
{
	uint32_t desc_lo, desc_hi;
	ish_aon_share_t *aon_share = pm_ctx.p_aon_share;
	tss_t *aon_tss = aon_share->tss_ptr;

	pm_ctx.aon_tss_selector[0] = 0;

	/* fill in 3 placeholder gdt entries */
	desc_lo = GEN_GDT_DESC_LO((uint32_t)&main_tss, 0x67, 0x89);
	desc_hi = GEN_GDT_DESC_HI((uint32_t)&main_tss, 0x67, 0x89);
	add_gdt_entry(desc_lo, desc_hi);

	desc_lo = GEN_GDT_DESC_LO((uint32_t)aon_tss, 0x67, 0x89);
	desc_hi = GEN_GDT_DESC_HI((uint32_t)aon_tss, 0x67, 0x89);
	pm_ctx.aon_tss_selector[1] = add_gdt_entry(desc_lo, desc_hi);

	desc_lo = GEN_GDT_DESC_LO((uint32_t)aon_share->ldt_ptr,
				  aon_share->ldt_size, 0x82);
	desc_hi = GEN_GDT_DESC_HI((uint32_t)aon_share->ldt_ptr,
				  aon_share->ldt_size, 0x82);
	aon_tss->ldt_seg_selector = add_gdt_entry(desc_lo, desc_hi);

	/* update gdt and set tss */

	__asm__ volatile("lgdt __gdt_ptr;\n\t"
			 "movw $0x18, %ax;\n\t"
			 "ltr %ax;\n\t");
}

static void switch_to_aontask(void)
{
	interrupt_disable();

	__sync_synchronize();

	/* disable cache*/
	__asm__ volatile("movl %%cr0, %%eax;\n\t"
			 "orl $0x60000000, %%eax;\n\t"
			 "movl %%eax, %%cr0; \n\t"
			 "wbinvd ; \n\t"
			 :
			 :
			 : "eax");

	__asm__ volatile("lcall *%0; \n\t" ::"m"(*pm_ctx.aon_tss_selector) :);

	/* enable cache*/
	__asm__ volatile("clts;\n\t"
			 "movl %%cr0, %%eax;\n\t"
			 "andl $0x9FFFFFFF, %%eax;\n\t"
			 "movl %%eax, %%cr0; \n\t"
			 :
			 :
			 : "eax");

	interrupt_enable();
}

#endif

static void enter_d0i0(void)
{
	timestamp_t t0, t1;
	t0 = get_time();

	ish_halt();

	t1 = get_time();

	pm_ctx.p_aon_share->pm_state = ISH_PM_STATE_D0I0;

	pm_stats.d0i0_time_us += t1.val - t0.val;
	pm_stats.d0i0_cnt++;
}

#ifdef CONFIG_ISH_PM_D0I1

static void enter_d0i1(void)
{
	timestamp_t t0, t1;
	t0 = get_time();

	pm_stats.d0i1_cnt++;

	pm_ctx.p_aon_share->pm_state = ISH_PM_STATE_D0I1;

	/*TODO TCG enable */

	ish_halt();

	/*TODO TCG disable */

	t1 = get_time();

	pm_ctx.p_aon_share->pm_state = ISH_PM_STATE_D0;

	pm_stats.d0i1_time_us += t1.val - t0.val;
	pm_stats.d0i1_cnt++;
}

#endif

#ifdef CONFIG_ISH_PM_D0I2

static void enter_d0i2(void)
{
	timestamp_t t0, t1;
	t0 = get_time();

	pm_ctx.p_aon_share->pm_state = ISH_PM_STATE_D0I2;

	/*TODO some preparing work for D0i2 */

	switch_to_aontask();

	/*TODO just for test, will remove later */
	ish_halt();

	/*TODO some restore work for D0i2 */

	t1 = get_time();

	pm_ctx.p_aon_share->pm_state = ISH_PM_STATE_D0;

	pm_stats.d0i2_time_us += t1.val - t0.val;
	pm_stats.d0i2_cnt++;
}

#endif

#ifdef CONFIG_ISH_PM_D0I3

static void enter_d0i3(void)
{
	timestamp_t t0, t1;
	t0 = get_time();

	pm_ctx.p_aon_share->pm_state = ISH_PM_STATE_D0I3;

	/*TODO some preparing work for D0i3 */

	switch_to_aontask();

	/*TODO just for test, will remove later */
	ish_halt();

	/*TODO some restore work for D0i3 */

	t1 = get_time();

	pm_ctx.p_aon_share->pm_state = ISH_PM_STATE_D0;

	pm_stats.d0i3_time_us += t1.val - t0.val;
	pm_stats.d0i3_cnt++;
}

#endif

static int d0ix_decide(uint32_t idle_us)
{
	int pm_state = ISH_PM_STATE_D0I0;

	if (DEEP_SLEEP_ALLOWED) {
#ifdef CONFIG_ISH_PM_D0I1
		pm_state = ISH_PM_STATE_D0I1;
#endif

#ifdef CONFIG_ISH_PM_D0I2
		if (idle_us >= CONFIG_ISH_D0I2_MIN_USEC) {
			pm_state = ISH_PM_STATE_D0I2;
		}
#endif

#ifdef CONFIG_ISH_PM_D0I3
		if (idle_us >= CONFIG_ISH_D0I2_MIN_USEC) {
			pm_state = ISH_PM_STATE_D0I3;
		}
#endif
	}

	return pm_state;
}

static void pm_process(uint32_t idle_us)
{
	int decide;
	decide = d0ix_decide(idle_us);

	switch (decide) {
#ifdef CONFIG_ISH_PM_D0I1
	case ISH_PM_STATE_D0I1:
		enter_d0i1();
		break;
#endif
#ifdef CONFIG_ISH_PM_D0I2
	case ISH_PM_STATE_D0I2:
		enter_d0i2();
		break;
#endif
#ifdef CONFIG_ISH_PM_D0I3
	case ISH_PM_STATE_D0I3:
		enter_d0i3();
		break;
#endif
	default:
		enter_d0i0();
		break;
	}
}

void ish_pm_init(void)
{
#ifdef CONFIG_ISH_PM_AONTASK
	init_aon_task();
#endif
}

void __idle(void)
{
	timestamp_t t0;
	int next_delay = 0;

	while (1) {
		t0 = get_time();
		next_delay = __hw_clock_event_get() - t0.le.lo;

		pm_process(next_delay);
	}
}

/**
 * Print low power idle statistics
 */
static int command_idle_stats(int argc, char **argv)
{
	ccprintf("Ldle sleep:\n");
	ccprintf("	D0i0:\n");
	ccprintf("		calls:	%ld\n", pm_stats.d0i0_cnt);
	ccprintf("		time:	%.6lds\n", pm_stats.d0i0_time_us);

	ccprintf("Deep sleep:\n");
#ifdef CONFIG_ISH_PM_D0I1
	ccprintf("	D0i1:\n");
	ccprintf("		calls:	%ld\n", pm_stats.d0i1_cnt);
	ccprintf("		time:	%.6lds\n", pm_stats.d0i1_time_us);
#endif

#ifdef CONFIG_ISH_PM_D0I2
	ccprintf("	D0i2:\n");
	ccprintf("		calls:	%ld\n", pm_stats.d0i2_cnt);
	ccprintf("		time:	%.6lds\n", pm_stats.d0i2_time_us);
#endif

#ifdef CONFIG_ISH_PM_D0I3
	ccprintf("	D0i3:\n");
	ccprintf("		calls:	%ld\n", pm_stats.d0i3_cnt);
	ccprintf("		time:	%.6lds\n", pm_stats.d0i3_time_us);
#endif

	ccprintf("Total time on:	%.6lds\n", get_time().val);

	return EC_SUCCESS;
}

DECLARE_CONSOLE_COMMAND(idlestats, command_idle_stats, "",
			"Print last idle stats");
