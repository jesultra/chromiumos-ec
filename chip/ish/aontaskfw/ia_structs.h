/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_IA_STRUCTS_H
#define __CROS_EC_IA_STRUCTS_H

#include <stdint.h>

typedef struct {
	uint32_t dword_one;
	uint32_t dword_two;
} ldt_entry_t;

typedef struct {
	uint32_t dword_one;
	uint32_t dword_two;
} idt_entry_t;

typedef struct {
	uint16_t idt_size;
	uint32_t idt_start;
} __attribute__((packed)) idt_ptr_t;

typedef struct {
	uint16_t limit_lw; /* bits 0:15 of segment limit */
	uint16_t base_addr_lw; /* bits 0:15 of segment base address */
	uint8_t base_addr_mb; /* bits 16:23 of segment base address */
	uint8_t type; /* descriptor type fields */
	uint8_t limit_ub; /* bits 16:19 of limit + more type fields */
	uint8_t base_addr_ub; /* bits 24:31 of segment base address */
} gdt_descriptor_t;

/*
 * structure definition for the GDT "header"
 * (does not include the GDT entries).
 * The structure is packed to force the structure to appear "as is".
 */

typedef struct {
	uint16_t limit; /* GDT limit */
	gdt_descriptor_t *p_entries; /* pointer to the GDT entries */
} __attribute__((__packed__)) gdt_header_t;

typedef struct {
	uint16_t prev_task_link;
	uint16_t reserved1;
	char *esp0;
	uint16_t ss0;
	uint16_t reserved2;
	char *esp1;
	uint16_t ss1;
	int16_t reserved3;
	char *esp2;
	uint16_t ss2;
	uint16_t reserved4;
	uint32_t cr3;
	int32_t eip;
	uint32_t eflags;
	uint32_t eax;
	uint32_t ecx;
	int32_t edx;
	uint32_t ebx;
	uint32_t esp;
	uint32_t ebp;
	uint32_t esi;
	uint32_t edi;
	int16_t es;
	uint16_t reserved5;
	uint16_t cs;
	uint16_t reserved6;
	uint16_t ss;
	uint16_t reserved7;
	uint16_t ds;
	uint16_t reserved8;
	uint16_t fs;
	uint16_t reserved9;
	uint16_t gs;
	uint16_t reserved10;
	uint16_t ldt_seg_selector;
	uint16_t reserved11;
	uint16_t trap_debug;
	uint16_t iomap_base_addr; /* offset from TSS base for I/O perms */
} __attribute__((packed)) tss_t;

#endif /* __CROS_EC_IA_STRUCTS_H */
