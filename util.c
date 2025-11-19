// SPDX-License-Identifier: GPL-2.0-or-later
#include <linux/module.h>
#include <linux/debugfs.h>
#include <linux/percpu.h>
#include <linux/slab.h>

#include "util.h"

struct debugfs_percpu_u64_array_private {
	char *buf;
	size_t bufsize;
};

#define LINE_MAX_LEN 28  // Max length of one line "CPU9999: 0x0123456789abcdef\n"

static size_t format_percpu_u64_array(
	char *buf,
	size_t bufsize,
	u64 __percpu *data)
{
	int cpu;
	size_t ret = 0;
	for_each_online_cpu(cpu) {
		u64 *val = per_cpu_ptr(data, cpu);
		size_t len = snprintf(buf, bufsize, "CPU%d: 0x%016llx\n", cpu, *val);
		ret += len;
		buf += len;
		bufsize -= len;
	}
	return ret;
}

static int percpu_u64_array_open(struct inode *inode, struct file *file)
{
	struct debugfs_percpu_u64_array_descriptor *desc = inode->i_private;
	struct debugfs_percpu_u64_array_private *filep;
	size_t bufsize = num_possible_cpus() * LINE_MAX_LEN + 1;

	desc->prepare_fn(desc->data, desc->info);

	filep = kzalloc(sizeof(*filep), GFP_KERNEL);
	if (!filep)
		return -ENOMEM;

	filep->buf = kzalloc(bufsize, GFP_KERNEL);
	if (!filep->buf) {
		kfree(filep);
		return -ENOMEM;
	}

	filep->bufsize = format_percpu_u64_array(filep->buf, bufsize, desc->data);
	if (desc->prepare_unlock_fn)
		desc->prepare_unlock_fn();

	file->private_data = filep;

	return nonseekable_open(inode, file);
}

static ssize_t percpu_u64_array_read(
	struct file *file,
	char __user *buf,
	size_t count,
	loff_t *ppos)
{
	struct debugfs_percpu_u64_array_private *filep = file->private_data;
	return simple_read_from_buffer(buf, count, ppos, filep->buf, filep->bufsize);
}

static int percpu_u64_array_release(struct inode *inode, struct file *file)
{
	struct debugfs_percpu_u64_array_private *filep = file->private_data;
	kfree(filep->buf);
	kfree(filep);
	return 0;
}

static const struct file_operations percpu_u64_array_fops = {
	.owner = THIS_MODULE,
	.open = percpu_u64_array_open,
	.read = percpu_u64_array_read,
	.release = percpu_u64_array_release,
};

void debugfs_create_percpu_u64_array(
	const char *name,
	umode_t mode,
	struct dentry *parent,
	struct debugfs_percpu_u64_array_descriptor *desc)
{
	debugfs_create_file_unsafe(name, mode, parent, desc, &percpu_u64_array_fops);
}