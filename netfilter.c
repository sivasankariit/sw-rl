#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/rtnetlink.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#include <net/neighbour.h>
#include <net/dst.h>
#include <linux/ip.h>
#include <linux/tcp.h>

#include "prl.h"

#ifndef NETFILTER
#error "Compiling netfilter.c without -DNETFILTER"
#endif

typedef int (*ok_fn_t)(struct sk_buff *);
struct ok_func_ptr {
	ok_fn_t function;
};

#define OKPTR(skb) ((struct ok_func_ptr *)((skb)->cb))

extern struct net_device *iso_netdev;
extern struct iso_rl *p5001, *rest;
extern struct iso_rl *testrls[128];

static struct nf_hook_ops hook_out;
unsigned int hook_out_func(unsigned int hooknum,
						   struct sk_buff *skb,
						   const struct net_device *in,
						   const struct net_device *out,
						   int (*okfn)(struct sk_buff *));

int iso_tx_hook_init(void);
void iso_tx_hook_exit(void);

inline void skb_xmit(struct sk_buff *skb) {
#define dst(skb) skb_dst(skb)

#if 0
	if(dst(skb)->hh)
		neigh_hh_output(dst(skb)->hh, skb);
	else if(dst(skb)->neighbour)
		dst(skb)->neighbour->output(skb);
#endif

	ok_fn_t okfn = OKPTR(skb)->function;
	if(okfn) {
		okfn(skb);
	} else {
		if(net_ratelimit())
			printk(KERN_INFO "prl: couldn't handle buff %p\n", skb);
		kfree_skb(skb);
	}
#undef dst
}

int iso_tx_hook_init() {
	if(iso_netdev == NULL)
		return 1;

	hook_out.hook = hook_out_func;
	hook_out.hooknum = NF_INET_POST_ROUTING;
	hook_out.pf = PF_INET;
	hook_out.priority = NF_IP_PRI_FIRST;

	return nf_register_hook(&hook_out);
}

void iso_tx_hook_exit() {
	nf_unregister_hook(&hook_out);
}

unsigned int hook_out_func(unsigned int hooknum,
						   struct sk_buff *skb,
						   const struct net_device *in,
						   const struct net_device *out,
						   int (*okfn)(struct sk_buff *))
{
	enum iso_verdict verdict;
	struct iso_rl *rl;
	struct iphdr *iph;
	struct tcphdr *tcph;

	int port = 0;
	int cpu = smp_processor_id();

	/* Filter packets on rate limited interface */
	if(out != iso_netdev)
		return NF_ACCEPT;

	/* TODO: better classification */
	iph = ip_hdr(skb);
	rl = testrls[0];

	if(iph->protocol == IPPROTO_TCP) {
		tcph = tcp_hdr(skb);
		port = ntohs(tcph->source) % 127 + 1;
    rl = testrls[port + 1];
	}

	rcu_read_lock_bh();
	OKPTR(skb)->function = okfn;
	verdict = iso_rl_enqueue(rl, skb, cpu);

	iso_rl_dequeue_root();
	rcu_read_unlock_bh();

	switch(verdict) {
	case ISO_VERDICT_DROP:
		return NF_DROP;

	case ISO_VERDICT_PASS:
		return NF_ACCEPT;

	case ISO_VERDICT_SUCCESS:
	default:
		return NF_STOLEN;
	}

	printk(KERN_INFO "shouldn't reach this line\n");
	return NF_DROP;
}

/* Local Variables: */
/* indent-tabs-mode:t */
/* End: */
