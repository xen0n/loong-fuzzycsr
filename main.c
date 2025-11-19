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
#define POKE_STUB_LEN (4*4)  // 4 insns

typedef u64 (*read_csr_fn_t)(void);
u64 read_csr_stubs(void);
#define READ_STUB_LEN (2*4)  // 2 insns

local_lock_t csr_lock;

static u64 poke_csr(u16 csr_id)
{
	unsigned long flags;
	u64 ret;
	poke_csr_fn_t poke_fn = (poke_csr_fn_t)(void *)(((u64)(void *)poke_csr_stubs) + (u64)(POKE_STUB_LEN * (csr_id & 0x3fff)));

	migrate_disable();
	local_lock_irqsave(&csr_lock, flags);
	ret = poke_fn(mask);
	local_unlock_irqrestore(&csr_lock, flags);
	migrate_enable();

	return ret;
}

static u64 read_csr(u16 csr_id)
{
	read_csr_fn_t read_fn = (read_csr_fn_t)(void *)(((u64)(void *)read_csr_stubs) + (u64)(READ_STUB_LEN * (csr_id & 0x3fff)));
	return read_fn();
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

static int read_get(void *data, u64 *val)
{
	u16 csr_id = (u16)(u64)data;
	*val = read_csr(csr_id);
	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(read_fops, read_get, NULL, "0x%016llx\n");

static void fuzzycsr_create_files(struct dentry *parent)
{
	int i;
	struct dentry *poke_dir, *read_dir;

	poke_dir = debugfs_create_dir("poke", parent);
	debugfs_create_x64("mask", 0644, poke_dir, &mask);
	for (i = 0; i <= 0x3fff; i++) {
		char filename[6];  // len("16383") + 1
		snprintf(filename, sizeof(filename), "%d", i);
		debugfs_create_file(filename, 0444, poke_dir, (void *)(u64)i, &poke_fops);
	}

	read_dir = debugfs_create_dir("read", parent);
	for (i = 0; i <= 0x3fff; i++) {
		char filename[6];  // len("16383") + 1
		snprintf(filename, sizeof(filename), "%d", i);
		debugfs_create_file(filename, 0444, read_dir, (void *)(u64)i, &read_fops);
	}
}

static int fuzzycsr_init(void)
{
	pr_info("module_init\n");

	mask = 0;
	local_lock_init(&csr_lock);
	fuzzycsr_dir = debugfs_create_dir("fuzzycsr", arch_debugfs_dir);
	fuzzycsr_create_files(fuzzycsr_dir);

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
