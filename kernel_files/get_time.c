#include <linux/linkage.h>
#include <linux/kernel.h>
#include <linux/ktime.h>
#include <linux/uaccess.h>

asmlinkage void sys_get_time(struct timespec *ut)
{
	struct timespec kt;
	getnstimeofday(&kt);
	copy_to_user(ut, &kt, sizeof(struct timespec));
	return;
}
