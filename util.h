/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _FUZZYCSR_UTIL_H
#define _FUZZYCSR_UTIL_H

#include <linux/dcache.h>
#include <linux/percpu.h>

typedef void (*percpu_u64_array_prepare_fn_t)(u64 __percpu *data, void *info);

struct debugfs_percpu_u64_array_descriptor {
	u64 __percpu *data;
	percpu_u64_array_prepare_fn_t prepare_fn;
	void (*prepare_unlock_fn)(void);
	void *info;
};

void debugfs_create_percpu_u64_array(
	const char *name,
	umode_t mode,
	struct dentry *parent,
	struct debugfs_percpu_u64_array_descriptor *desc);

#endif /* _FUZZYCSR_UTIL_H */