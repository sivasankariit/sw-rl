#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/fs.h>
#include <linux/seq_file.h>
#include "stats.h"
#include "prl.h"

static void *iso_stats_proc_seq_start(struct seq_file *s, loff_t *pos)
{
	static unsigned long counter = 0;
	if (*pos == 0) {
		return &counter;
	}
	else {
		*pos = 0;
		return NULL;
	}
}

static void *iso_stats_proc_seq_next(struct seq_file *s, void *v, loff_t *pos)
{
	unsigned long *index = (unsigned long *)v;
	(*index)++;

	return NULL;
}

static void iso_stats_proc_seq_stop(struct seq_file *s, void *v)
{
	/* nothing to do, we use a static value in start() */
}

static int iso_stats_proc_seq_show(struct seq_file *s, void *v)
{
	iso_rl_show(&prl, s);
	return 0;
}

static struct proc_dir_entry *iso_stats_proc;

static struct seq_operations iso_stats_proc_seq_ops = {
	.start = iso_stats_proc_seq_start,
	.next = iso_stats_proc_seq_next,
	.stop = iso_stats_proc_seq_stop,
	.show = iso_stats_proc_seq_show
};

static int iso_stats_proc_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &iso_stats_proc_seq_ops);
}

static struct file_operations iso_stats_proc_file_ops = {
	.owner = THIS_MODULE,
	.open = iso_stats_proc_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release
};


int iso_stats_init() {
	int ret = 0;

	iso_stats_proc = create_proc_entry(ISO_STATS_PROC_NAME, 0, NULL);
	if(iso_stats_proc) {
		iso_stats_proc->proc_fops = &iso_stats_proc_file_ops;
	} else {
		ret = 1;
	}

	return ret;
}

void iso_stats_exit() {
	remove_proc_entry(ISO_STATS_PROC_NAME, NULL);
}

/* Local Variables: */
/* indent-tabs-mode:t */
/* End: */
