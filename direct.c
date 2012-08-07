#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/rtnetlink.h>
#include "prl.h"

#ifndef DIRECT
#error "Compiling direct.c without -DDIRECT"
#endif

extern struct net_device *iso_netdev;
extern struct iso_rl rl;

static netdev_tx_t (*old_ndo_start_xmit)(struct sk_buff *, struct net_device *);
netdev_tx_t iso_ndo_start_xmit(struct sk_buff *, struct net_device *);
rx_handler_result_t iso_rx_handler(struct sk_buff **);

int iso_tx_hook_init(void);
void iso_tx_hook_exit(void);

enum iso_verdict iso_tx(struct sk_buff *skb, const struct net_device *out);
enum iso_verdict iso_rx(struct sk_buff *skb, const struct net_device *in);

/* Called with bh disabled */
inline void skb_xmit(struct sk_buff *skb) {
	struct netdev_queue *txq;
	int cpu;
	int locked = 0;

	if(likely(old_ndo_start_xmit != NULL)) {
		cpu = smp_processor_id();
		txq = netdev_get_tx_queue(iso_netdev, skb_get_queue_mapping(skb));

		if(txq->xmit_lock_owner != cpu) {
			HARD_TX_LOCK(iso_netdev, txq, cpu);
			locked = 1;
		}
		/* XXX: will the else condition happen? */

		if(!netif_tx_queue_stopped(txq)) {
			old_ndo_start_xmit(skb, iso_netdev);
		} else {
			kfree_skb(skb);
		}

		if(locked) {
			HARD_TX_UNLOCK(iso_netdev, txq);
		}
	}
}

int iso_tx_hook_init() {
	struct net_device_ops *ops;

	if(iso_netdev == NULL || iso_netdev->netdev_ops == NULL)
		return 1;

	ops = (struct net_device_ops *)iso_netdev->netdev_ops;

	if(ops == NULL) {
		printk(KERN_INFO "device %s has no ops\n", iso_netdev->name);
		return 1;
	}

	rtnl_lock();
	old_ndo_start_xmit = ops->ndo_start_xmit;
	ops->ndo_start_xmit = iso_ndo_start_xmit;
	rtnl_unlock();

	synchronize_net();
	return 0;
}

void iso_tx_hook_exit() {
	struct net_device_ops *ops = (struct net_device_ops *)iso_netdev->netdev_ops;

	rtnl_lock();
	ops->ndo_start_xmit = old_ndo_start_xmit;
	rtnl_unlock();

	synchronize_net();
}

/* Called with bh disabled */
netdev_tx_t iso_ndo_start_xmit(struct sk_buff *skb, struct net_device *out) {
	enum iso_verdict verdict;
	struct netdev_queue *txq;
	struct iso_rl_queue *q;

	int cpu = smp_processor_id();

	txq = netdev_get_tx_queue(iso_netdev, skb_get_queue_mapping(skb));
	HARD_TX_UNLOCK(iso_netdev, txq);

	skb_reset_mac_header(skb);
	verdict = iso_rl_enqueue(prl, skb, cpu);
	q = per_cpu_ptr(prl->queue, cpu);

	iso_rl_dequeue((unsigned long)q);

	switch(verdict) {
	case ISO_VERDICT_DROP:
		kfree_skb(skb);
		break;

	case ISO_VERDICT_PASS:
		skb_xmit(skb);
		break;

	case ISO_VERDICT_SUCCESS:
	default:
		break;
	}

	HARD_TX_LOCK(iso_netdev, txq, cpu);
	return NETDEV_TX_OK;
}

/* Local Variables: */
/* indent-tabs-mode:t */
/* End: */
