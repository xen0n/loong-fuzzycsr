// SPDX-License-Identifier: GPL-2.0-or-later
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/debugfs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/local_lock.h>
#include <linux/module.h>
#include <linux/preempt.h>
#include <linux/sched.h>  // migrate_disable

#include <asm/cacheflush.h>

#ifndef CONFIG_DEBUG_FS
#error CONFIG_DEBUG_FS is needed
#endif

// from arch/loongarch/kernel/kdebugfs.c
extern struct dentry *arch_debugfs_dir;

struct dentry *fuzzycsr_dir;
u64 mask;

typedef u64 (*poke_csr_fn_t)(u64);
u64 poke_csr_stubs(u64 mask);
#define STUB_LEN (4*4)  // 4 insns

local_lock_t csr_lock;

// Usage:
//
// 1. write mask (u64, e.g. "0xFFFFFFFF00000000\n") to /sys/kernel/debug/loongarch/fuzzycsr/mask
// 2. read from /sys/kernel/debug/loongarch/fuzzycsr/poke-CSR_ID

static u64 poke_csr(u16 csr_id)
{
	unsigned long flags;
	u64 ret;
	poke_csr_fn_t poke_fn = (poke_csr_fn_t)(void *)(((u64)(void *)poke_csr_stubs) + (u64)(STUB_LEN * (csr_id & 0x3fff)));

	migrate_disable();
	local_lock_irqsave(&csr_lock, flags);
	ret = poke_fn(mask);
	local_unlock_irqrestore(&csr_lock, flags);
	migrate_enable();

	return ret;
}

//
// debugfs fops
//
static int poke_get(void *data, u64 *val)
{
	u16 csr_id = (u16)(u64)data;
	*val = poke_csr(csr_id);
	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(poke_fops, poke_get, NULL, "0x%016llx\n");

static int fuzzycsr_init(void)
{
	int i;
	pr_info("module_init\n");

	mask = 0;

	local_lock_init(&csr_lock);
	fuzzycsr_dir = debugfs_create_dir("fuzzycsr", arch_debugfs_dir);
	debugfs_create_x64("mask", 0644, fuzzycsr_dir, &mask);
	for (i = 0; i < 0x3fff; i++) {
		char filename[11];  // len("poke-16383") + 1
		snprintf(filename, sizeof(filename), "poke-%d", i);
		debugfs_create_file(filename, 0444, fuzzycsr_dir, (void *)(u64)i, &poke_fops);
	}

	return 0;
}

static void fuzzycsr_exit(void)
{
	pr_info("module_exit\n");
	debugfs_remove(fuzzycsr_dir);
}

module_init(fuzzycsr_init);
module_exit(fuzzycsr_exit);
MODULE_AUTHOR("WANG Xuerui <git@xen0n.name>");
MODULE_DESCRIPTION("Debugging helper for fuzzing LoongArch CSR space");
MODULE_LICENSE("GPL");
