// SPDX-License-Identifier: GPL-2.0-or-later
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>

static int fuzzycsr_init(void) {
	pr_info("module_init\n");
	return 0;
}

static void fuzzycsr_exit(void) {
	pr_info("module_exit\n");
}

module_init(fuzzycsr_init);
module_exit(fuzzycsr_exit);
MODULE_AUTHOR("WANG Xuerui <git@xen0n.name>");
MODULE_DESCRIPTION("Debugging helper for fuzzing LoongArch CSR space");
MODULE_LICENSE("GPL");
