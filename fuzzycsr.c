// SPDX-License-Identifier: GPL-2.0-or-later
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/debugfs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/local_lock.h>
#include <linux/module.h>
#include <linux/preempt.h>
#include <linux/sched.h>  // migrate_disable
#include <linux/set_memory.h>

#include <asm/cacheflush.h>
#include <asm/inst.h>

#ifndef CONFIG_DEBUG_FS
#error CONFIG_DEBUG_FS is needed
#endif

// from arch/loongarch/kernel/kdebugfs.c
extern struct dentry *arch_debugfs_dir;

struct dentry *fuzzycsr_dir;
u16 csr_id;
u64 mask;

typedef u64 (*poke_csr_fn_t)(u64 mask);
poke_csr_fn_t jit_poke_worker_fn;
local_lock_t csr_lock; 

// Usage:
//
// 1. write CSR number (u14, e.g. "12345\n") to /sys/kernel/debug/loongarch/fuzzycsr/csr
// 2. write mask (u64, e.g. "0xFFFFFFFF00000000\n") to /sys/kernel/debug/loongarch/fuzzycsr/mask
// 3. read from /sys/kernel/debug/loongarch/fuzzycsr/poke

static int update_op_page(u16 csr)
{
	int ret;
	u32 *p = (u32 *)jit_poke_worker_fn;
	unsigned long addr = (unsigned long)jit_poke_worker_fn;

	// a0 = mask
	//
	//    0:   0015008c        move            $t0, $a0
	//    4:   04000184        csrxchg         $a0, $t0, 0x0  # now a0 is old val
	//    8:   04000184        csrxchg         $a0, $t0, 0x0  # immediately restore csr, read back written val
	//    c:   4c000020        ret
	u32 csrxchg_insn = 0x04000184 | ((csr & 0x3fff) << 10);

	ret = set_memory_nx(addr, 1);
	if (ret)
		return ret;
	ret = set_memory_rw(addr, 1);
	if (ret)
		return ret;

	*p++ = 0x0015008c;
	*p++ = csrxchg_insn;
	*p++ = csrxchg_insn;
	*p++ = 0x4c000020;

	ret = set_memory_rox(addr, 1);
	if (ret)
		return ret;
	flush_icache_range(addr, addr + PAGE_SIZE);
	return 0;
}

static u64 poke_csr(void)
{
	unsigned long flags;
	u64 ret;

	migrate_disable();
	local_lock_irqsave(&csr_lock, flags);
	ret = jit_poke_worker_fn(mask);
	local_unlock_irqrestore(&csr_lock, flags);
	migrate_enable();

	return ret;
}

//
// debugfs fops
//
static int csr_id_get(void *data, u64 *val)
{
	*val = csr_id;
	return 0;
}

static int csr_id_set(void *data, u64 val)
{
	unsigned long flags;
	int ret;
	local_lock_irqsave(&csr_lock, flags);
	csr_id = (u16)val;
	ret = update_op_page(csr_id);
	local_unlock_irqrestore(&csr_lock, flags);
	return ret;
}
DEFINE_DEBUGFS_ATTRIBUTE(csr_id_fops, csr_id_get, csr_id_set, "%llu\n");

static int poke_get(void *data, u64 *val)
{
	*val = poke_csr();
	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(poke_fops, poke_get, NULL, "0x%016llx\n");

static int fuzzycsr_init(void)
{
	int i;
	u32 *p;

	pr_info("module_init\n");
	p = kzalloc(PAGE_SIZE, GFP_KERNEL);
	if (!p)
		return -ENOMEM;
	for (i = 0; i < PAGE_SIZE / 4; i++)
		p[i] = INSN_BREAK;
	jit_poke_worker_fn = (poke_csr_fn_t)p;
	update_op_page(0);

	csr_id = 0;
	mask = 0;

	local_lock_init(&csr_lock);
	fuzzycsr_dir = debugfs_create_dir("fuzzycsr", arch_debugfs_dir);
	debugfs_create_file("csr", 0644, fuzzycsr_dir, NULL, &csr_id_fops);
	debugfs_create_x64("mask", 0644, fuzzycsr_dir, &mask);
	debugfs_create_file("poke", 0444, fuzzycsr_dir, NULL, &poke_fops);

	return 0;
}

static void fuzzycsr_exit(void)
{
	pr_info("module_exit\n");
	debugfs_remove(fuzzycsr_dir);
	kfree(jit_poke_worker_fn);
}

module_init(fuzzycsr_init);
module_exit(fuzzycsr_exit);
MODULE_AUTHOR("WANG Xuerui <git@xen0n.name>");
MODULE_DESCRIPTION("Debugging helper for fuzzing LoongArch CSR space");
MODULE_LICENSE("GPL");
