/* SPDX-License-Identifier: GPL-2.0-or-later */

#include <linux/linkage.h>

.macro gen_csr_stub csr
	move $a1, $a0
	csrxchg $a0, $a1, \csr
	csrxchg $a0, $a1, \csr
	ret
.endm

.text
SYM_FUNC_START(poke_csr_stubs)
.set csr, 0
.rept 16384
	gen_csr_stub csr
.set csr, csr + 1
.endr
SYM_FUNC_END(poke_csr_stubs)
