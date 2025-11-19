// SPDX-License-Identifier: GPL-2.0-or-later
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/debugfs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/local_lock.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/preempt.h>
#include <linux/sched.h>  // migrate_disable

#include "util.h"

#ifndef CONFIG_DEBUG_FS
#error CONFIG_DEBUG_FS is needed
#endif

// from arch/loongarch/kernel/kdebugfs.c
extern struct dentry *arch_debugfs_dir;

struct dentry *fuzzycsr_dir;
struct debugfs_percpu_u64_array_descriptor global_poke_descs[16384];
struct debugfs_percpu_u64_array_descriptor global_read_descs[16384];
u64 mask;

typedef u64 (*poke_csr_fn_t)(u64);
u64 poke_csr_stubs(u64 mask);
#define POKE_STUB_LEN (4*4)  // 4 insns

typedef u64 (*read_csr_fn_t)(void);
u64 read_csr_stubs(void);
#define READ_STUB_LEN (2*4)  // 2 insns

local_lock_t csr_lock;

struct mutex global_csr_op_result_mutex;
DEFINE_PER_CPU(u64, global_csr_op_result);

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

struct percpu_info_desc {
	u16 csr_id;
	u64 __percpu *result;
};

static void poke_csr_percpu(void *info)
{
	struct percpu_info_desc *desc = info;
	this_cpu_write(desc->result, poke_csr(desc->csr_id));
}

static void read_csr_percpu(void *info)
{
	struct percpu_info_desc *desc = info;
	this_cpu_write(desc->result, read_csr(desc->csr_id));
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

//
// global csr ops
//

static void global_poke_prepare(u64 __percpu *data, void *info)
{
	struct percpu_info_desc desc = {
		.csr_id = (u16)(u64)info,
		.result = data,
	};
	mutex_lock(&global_csr_op_result_mutex);
	on_each_cpu(poke_csr_percpu, &desc, 1);
	// unlocking is deferred to after the buffer has been prepared
}


static void global_read_prepare(u64 __percpu *data, void *info)
{
	struct percpu_info_desc desc = {
		.csr_id = (u16)(u64)info,
		.result = data,
	};
	mutex_lock(&global_csr_op_result_mutex);
	on_each_cpu(read_csr_percpu, &desc, 1);
	// unlocking is deferred to after the buffer has been prepared
}

static void global_array_unlock(void)
{
	mutex_unlock(&global_csr_op_result_mutex);
}

//
// wire up everything
//

static void fuzzycsr_create_files(struct dentry *parent, bool global)
{
	int i;
	struct dentry *poke_dir, *read_dir;

	poke_dir = debugfs_create_dir("poke", parent);

	if (!global)
		debugfs_create_x64("mask", 0644, poke_dir, &mask);

	for (i = 0; i <= 0x3fff; i++) {
		char filename[6];  // len("16383") + 1
		snprintf(filename, sizeof(filename), "%d", i);
		if (global)
			debugfs_create_percpu_u64_array(
				filename,
				0444,
				poke_dir,
				&global_poke_descs[i]);
		else
			debugfs_create_file(filename, 0444, poke_dir, (void *)(u64)i, &poke_fops);
	}

	read_dir = debugfs_create_dir("read", parent);
	for (i = 0; i <= 0x3fff; i++) {
		char filename[6];  // len("16383") + 1
		snprintf(filename, sizeof(filename), "%d", i);
		if (global)
			debugfs_create_percpu_u64_array(
				filename,
				0444,
				read_dir,
				&global_read_descs[i]);
		else
			debugfs_create_file(filename, 0444, read_dir, (void *)(u64)i, &read_fops);
	}
}

static int fuzzycsr_init(void)
{
	struct dentry *global_dir;
	int i;

	pr_info("module_init\n");

	mask = 0;
	local_lock_init(&csr_lock);
	fuzzycsr_dir = debugfs_create_dir("fuzzycsr", arch_debugfs_dir);
	fuzzycsr_create_files(fuzzycsr_dir, false);

	for (i = 0; i < 16384; i++) {
		global_poke_descs[i].data = &get_cpu_var(global_csr_op_result);
		global_poke_descs[i].prepare_fn = global_poke_prepare;
		global_poke_descs[i].prepare_unlock_fn = global_array_unlock;
		global_poke_descs[i].info = (void *)(u64)i;

		global_read_descs[i].data = &get_cpu_var(global_csr_op_result);
		global_read_descs[i].prepare_fn = global_read_prepare;
		global_read_descs[i].prepare_unlock_fn = global_array_unlock;
		global_read_descs[i].info = (void *)(u64)i;
	}

	mutex_init(&global_csr_op_result_mutex);
	global_dir = debugfs_create_dir("global", fuzzycsr_dir);
	fuzzycsr_create_files(global_dir, true);

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
