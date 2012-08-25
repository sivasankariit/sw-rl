#include <linux/module.h>
#include <linux/netdevice.h>
#include "prl.h"
#include "stats.h"

MODULE_AUTHOR("Vimal <j.vimal@gmail.com>");
#ifdef DIRECT
MODULE_DESCRIPTION("Parallel Rate Limiter (direct edition)");
#endif

#ifdef NETFILTER
MODULE_DESCRIPTION("Parallel Rate Limiter (netfilter edition)");
#endif
MODULE_VERSION("1");
MODULE_LICENSE("GPL");

static char *dev;
MODULE_PARM_DESC(dev, "Interface to operate prl (default eth0).");
module_param(dev, charp, 0);

static int rate=1000;
MODULE_PARM_DESC(dev, "Ratelimit in Mb/s.");
module_param(rate, int, 0);

struct net_device *iso_netdev;
static int iso_init(void);
static void iso_exit(void);
struct iso_rl *p5001, *rest;
struct iso_rl *testrls[128];
int ntestrls;
int iso_exiting;

int iso_tx_hook_init(void);
void iso_tx_hook_exit(void);

static void test(void) {
  char name[32];
  int i;
  ntestrls = 128;
  for(i = 0; i < ntestrls; i++) {
    sprintf(name, "rl%d", i);
    testrls[i] = iso_rl_new(name);
    testrls[i]->weight = 1;
    iso_rl_attach(rootrl, testrls[i]);
  }
}

static void test2(void) {
	ntestrls = 2;
	testrls[0] = iso_rl_new("cap");
	testrls[0]->cap = 1;
	testrls[0]->rate = 100;
	iso_rl_attach(rootrl, testrls[0]);

	testrls[1] = iso_rl_new("rest");
	testrls[1]->cap = 0;
	iso_rl_attach(rootrl, testrls[1]);
}

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

	if(iso_tx_hook_init()) {
		iso_rl_free(rootrl);
		goto out;
	}

	test();
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
	iso_rl_free(rootrl);

	dev_put(iso_netdev);

	printk(KERN_INFO "prl: goodbye.\n");
}

module_init(iso_init);
module_exit(iso_exit);

/* Local Variables: */
/* indent-tabs-mode:t */
/* End: */
