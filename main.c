#include <linux/module.h>
#include <linux/netdevice.h>
#include "prl.h"
#include "stats.h"

MODULE_AUTHOR("Vimal <j.vimal@gmail.com>");
MODULE_DESCRIPTION("Parallel Rate Limiter");
MODULE_VERSION("1");
MODULE_LICENSE("GPL");

static char *dev;
MODULE_PARM_DESC(dev, "Interface to operate prl.");
module_param(dev, charp, 0);

static int rate=1000;
MODULE_PARM_DESC(dev, "Ratelimit in Mb/s.");
module_param(rate, int, 0);

struct net_device *iso_netdev;
static int iso_init(void);
static void iso_exit(void);
extern struct iso_rl prl;

int iso_exiting;

int iso_tx_hook_init(void);
void iso_tx_hook_exit(void);

static int iso_init() {
	int i, ret = -1;
	iso_exiting = 0;

	if(dev == NULL) {
		dev = "eth0\0";
	}

	/* trim */
	for(i = 0; i < 32 && dev[i] != '\0'; i++) {
		if(dev[i] == '\n') {
			dev[i] = '\0';
			break;
		}
	}

	ISO_RATE_INITIAL = rate;

	rcu_read_lock();
	iso_netdev = dev_get_by_name(&init_net, dev);
	rcu_read_unlock();

	if(iso_netdev == NULL) {
		printk(KERN_INFO "prl: device %s not found", dev);
		goto out;
	}

	printk(KERN_INFO "prl: operating on %s (%p)\n",
		   dev, iso_netdev);

	if(iso_stats_init())
		goto out;

	if(iso_rl_prep())
		goto out;

	if(iso_rl_init(&prl))
		goto out;

	if(iso_tx_hook_init())
		goto out;

	ret = 0;

 out:
	return ret;
}

static void iso_exit() {
	iso_exiting = 1;
	mb();

	iso_tx_hook_exit();
	iso_stats_exit();
	iso_rl_exit();
	free_percpu(prl.queue);

	dev_put(iso_netdev);

	printk(KERN_INFO "prl: goodbye.\n");
}

module_init(iso_init);
module_exit(iso_exit);

/* Local Variables: */
/* indent-tabs-mode:t */
/* End: */
