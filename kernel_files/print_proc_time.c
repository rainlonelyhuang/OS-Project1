#include <linux/linkage.h>
#include <linux/kernel.h>
#include <linux/ktime.h>

asmlinkage void sys_print_proc_time(const pid_t pid, const struct timespec start, const struct timespec end)
{
	printk("[Project1] %d %09d.%09d %09d.%09d\n", pid, start.tv_sec, start.tv_nsec, end.tv_sec, end.tv_nsec);
	return;
}
