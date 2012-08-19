#ifndef __RL_H__
#define __RL_H__

#include <linux/skbuff.h>
#include <linux/completion.h>
#include <linux/hrtimer.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/if_ether.h>
#include <linux/seq_file.h>

#define RLNAME_MAX_CHARS (64)

enum iso_verdict {
	ISO_VERDICT_SUCCESS,
	ISO_VERDICT_DROP,
	ISO_VERDICT_PASS,
};

struct iso_rl_queue {
	struct sk_buff_head list;
	int first_pkt_size;
	u64 bytes_enqueued;
	u64 bytes_xmit;
	u64 tokens;
	int cpu;
	int waiting;

	struct iso_rl *rl;
	struct hrtimer *cputimer;
	struct list_head active_list;
};

struct iso_rl {
	u32 rate;
	u32 weight, waiting;
	u32 active_weight;

	spinlock_t spinlock;
	u64 total_tokens;
	ktime_t last_update_time;
	char name[RLNAME_MAX_CHARS];
	/* 0 if the rl is really a "class" without a queue. */
	int leaf;

	struct iso_rl *parent;
	struct list_head siblings;
	struct list_head children;
	struct list_head waiting_list;
	struct list_head waiting_node;

	/* The list of all rls */
	struct list_head list;
	struct iso_rl_queue __percpu *queue;
};

/* The per-cpu control block for rate limiters */
struct iso_rl_cb {
	spinlock_t spinlock;
	struct hrtimer timer;
	struct tasklet_struct xmit_timeout;
	struct list_head active_list;
#ifdef DEBUG
	ktime_t last;
	u64 avg_us;
#endif
	int cpu;
};

extern struct iso_rl_cb __percpu *rlcb;
extern struct iso_rl *rootrl;
extern struct list_head rls;

// Parameters
extern int ISO_TOKENBUCKET_TIMEOUT_NS;
extern int ISO_MAX_BURST_TIME_US;
extern int ISO_BURST_FACTOR;
extern int ISO_RL_UPDATE_INTERVAL_US;
extern int ISO_RATE_INITIAL;
extern int ISO_MAX_QUEUE_LEN_BYTES;
extern int ISO_MIN_BURST_BYTES;
extern void skb_xmit(struct sk_buff *);

void iso_rl_exit(void);
int iso_rl_prep(void);
int iso_rl_init(struct iso_rl *);

struct iso_rl *iso_rl_new(char *name);
int iso_rl_attach(struct iso_rl *parent, struct iso_rl *child);
void iso_rl_dequeue_root(void);

void iso_rl_free(struct iso_rl *);
void iso_rl_show(struct iso_rl *, struct seq_file *);
static inline int iso_rl_should_refill(struct iso_rl *);
inline void iso_rl_clock(struct iso_rl *);
enum iso_verdict iso_rl_enqueue(struct iso_rl *, struct sk_buff *, int cpu);
void iso_rl_dequeue(unsigned long _q);
enum hrtimer_restart iso_rl_timeout(struct hrtimer *);
inline int iso_rl_borrow_tokens(struct iso_rl *, struct iso_rl_queue *);
static inline ktime_t iso_rl_gettimeout(void);
static inline u64 iso_rl_singleq_burst(struct iso_rl *);
void iso_rl_xmit_tasklet(unsigned long _cb);
inline void iso_rl_activate_queue(struct iso_rl_queue *q);
inline void iso_rl_deactivate_queue(struct iso_rl_queue *q);

inline void iso_rl_activate_tree(struct iso_rl *rl, struct iso_rl_queue *q);
inline void iso_rl_deactivate_tree(struct iso_rl *rl, struct iso_rl_queue *q);
inline void iso_rl_fill_tokens(void);

static inline int skb_size(struct sk_buff *skb) {
	return ETH_HLEN + skb->len;
}

static inline ktime_t iso_rl_gettimeout() {
	return ktime_set(0, ISO_TOKENBUCKET_TIMEOUT_NS << 1);
}


static inline u64 iso_rl_singleq_burst(struct iso_rl *rl) {
	return ((rl->rate * ISO_MAX_BURST_TIME_US) >> 3) / ISO_BURST_FACTOR;
}

static inline int iso_rl_should_refill(struct iso_rl *rl) {
	ktime_t now = ktime_get();
	if(ktime_us_delta(now, rl->last_update_time) > ISO_RL_UPDATE_INTERVAL_US)
		return 1;
	return 0;
}

#endif /* __RL_H__ */

/* Local Variables: */
/* indent-tabs-mode:t */
/* End: */
