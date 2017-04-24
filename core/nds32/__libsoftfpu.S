/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

	.text
	.align	2
	.global	__cmpsf2
	.type	__cmpsf2, @function
__cmpsf2:
	.global	__eqsf2
	.type	__eqsf2, @function
__eqsf2:
	.global	__ltsf2
	.type	__ltsf2, @function
__ltsf2:
	.global	__lesf2
	.type	__lesf2, @function
__lesf2:
	.global	__nesf2
	.type	__nesf2, @function
__nesf2:
	move    $r4, #1
	j	.LA
	.global	__gesf2
	.type	__gesf2, @function
__gesf2:
	.global	__gtsf2
	.type	__gtsf2, @function
__gtsf2:
	move	$r4, #-1
.LA:
	push    $lp
	slli    $r2, $r0, #1
	slli    $r3, $r1, #1
	or      $r5, $r2, $r3
	beqz    $r5, .LMequ
	move    $r5, #0xff000000
	slt     $r15, $r5, $r2
	bnezs8  .LMnan
	slt     $r15, $r5, $r3
	bnezs8  .LMnan
	srli    $r2, $r2, #1
	sltsi   $r15, $r0, #0
	beqzs8  .Li48
	subri   $r2, $r2, #0
.Li48:
	srli    $r3, $r3, #1
	sltsi   $r15, $r1, #0
	beqzs8  .Li49
	subri   $r3, $r3, #0
.Li49:
	slts    $r15, $r2, $r3
	beqzs8  .Li50
	move    $r0, #-1
	j       .LM999
.Li50:
	slts    $r15, $r3, $r2
	beqzs8  .LMequ
	move    $r0, #1
	j       .LM999
.LMequ:
	move    $r0, #0
.LM999:
	pop     $lp
	ret5    $lp
.LMnan:
	move    $r0, $r4
	j       .LM999
	.size   __cmpsf2, .-__cmpsf2
	.size   __eqsf2, .-__eqsf2
	.size   __ltsf2, .-__ltsf2
	.size   __lesf2, .-__lesf2
	.size   __nesf2, .-__nesf2
	.size   __gesf2, .-__gesf2
	.size   __gtsf2, .-__gtsf2

	.text
	.align	2
	.global	__floatsisf
	.type	__floatsisf, @function
__floatsisf:
	push    $lp
	move    $r4, #0x80000000
	and     $r2, $r0, $r4
	beqz    $r0, .Li39
	sltsi   $r15, $r0, #0
	beqzs8  .Li40
	subri   $r0, $r0, #0
.Li40:
	move    $r1, #0x9e
	pushm	$r0, $r2
	pushm	$r4, $r5
	bal	__clzsi2
	move	$r3, $r0
	popm	$r4, $r5
	popm	$r0, $r2
	sub     $r1, $r1, $r3
	sll     $r0, $r0, $r3
	move    $r15, #0x80
	add     $r0, $r0, $r15
	slt     $r15, $r0, $r15
	add     $r1, $r1, $r15
	srai    $r4, $r0, #8
	andi    $r4, $r4, #1
	sub     $r0, $r0, $r4
	slli    $r0, $r0, #1
	srli    $r0, $r0, #9
	slli    $r4, $r1, #23
	or      $r0, $r0, $r4
.Li39:
	or      $r0, $r0, $r2
.LH999:
	pop     $lp
	ret5    $lp
	.size	__floatsisf, .-__floatsisf

	.text
	.align	2
	.global	__fixsfsi
	.type	__fixsfsi, @function
__fixsfsi:
	push    $lp
	slli    $r1, $r0, #8
	move    $r3, #0x80000000
	or      $r1, $r1, $r3
	srli    $r3, $r0, #23
	andi    $r3, $r3, #0xff
	subri   $r2, $r3, #0x9e
	blez    $r2, .LJspec
	sltsi   $r15, $r2, #0x20
	bnezs8  .Li42
	move    $r0, #0
	j       .LJ999
.Li42:
	srl     $r1, $r1, $r2
	sltsi   $r15, $r0, #0
	beqzs8  .Li43
	subri   $r1, $r1, #0
.Li43:
	move    $r0, $r1
.LJ999:
	pop     $lp
	ret5    $lp
.LJspec:
	move    $r3, #0x7f800000
	slt     $r15, $r3, $r0
	beqzs8  .Li44
	move    $r0, #0x80000000
	j       .LJ999
.Li44:
	move    $r0, #0x7fffffff
	j       .LJ999
	.size	__fixsfsi, .-__fixsfsi
