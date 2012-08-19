
#include "prl.h"

struct iso_rl_cb __percpu *rlcb;
struct iso_rl *rootrl;
struct list_head rls;
extern int iso_exiting;

int ISO_TOKENBUCKET_TIMEOUT_NS=25*1000;
int ISO_MAX_BURST_TIME_US=100;
int ISO_BURST_FACTOR=8;
int ISO_RL_UPDATE_INTERVAL_US=20;
int ISO_RATE_INITIAL=1000;
int ISO_MAX_QUEUE_LEN_BYTES=256 * 1024;
int ISO_MIN_BURST_BYTES=65536;

/* Called the first time when the module is initialised */
int iso_rl_prep() {
	int cpu;
	INIT_LIST_HEAD(&rls);

	rlcb = alloc_percpu(struct iso_rl_cb);
	if(rlcb == NULL)
		return -1;

	/* Init everything; but what about hotplug?  Hmm... */
	for_each_possible_cpu(cpu) {
		struct iso_rl_cb *cb = per_cpu_ptr(rlcb, cpu);
		spin_lock_init(&cb->spinlock);

		hrtimer_init(&cb->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL_PINNED);
		cb->timer.function = iso_rl_timeout;

		tasklet_init(&cb->xmit_timeout, iso_rl_xmit_tasklet, (unsigned long)cb);
		INIT_LIST_HEAD(&cb->active_list);

#ifdef DEBUG
		cb->last = ktime_get();
		cb->avg_us = 0;
#endif

		cb->cpu = cpu;
	}

	rootrl = iso_rl_new("root");
	if(rootrl == NULL) {
		free_percpu(rlcb);
		return -1;
	}

	return 0;
}

void iso_rl_exit(void) {
	int cpu;

	for_each_possible_cpu(cpu) {
		struct iso_rl_cb *cb = per_cpu_ptr(rlcb, cpu);
		tasklet_kill(&cb->xmit_timeout);
		hrtimer_cancel(&cb->timer);
	}

	free_percpu(rlcb);
}

void iso_rl_xmit_tasklet(unsigned long _cb) {
	struct iso_rl_cb *cb = (struct iso_rl_cb *)_cb;
	struct iso_rl_queue *q, *qtmp, *first;
	ktime_t dt;
	int count = 0;

#define budget 500

	if(iso_exiting)
		return;

	// determine all rates in one pass.  maybe use just a single lock
	// for this calculation?
	iso_rl_fill_tokens();

#ifdef DEBUG
	{
		/* This block is not needed, but just for debugging purposes */
		ktime_t last;
		last = cb->last;
		cb->last = ktime_get();
		cb->avg_us = ktime_us_delta(cb->last, last);
	}
#endif

	first = list_entry(cb->active_list.next, struct iso_rl_queue, active_list);

	list_for_each_entry_safe(q, qtmp, &cb->active_list, active_list) {
		count++;
		if(qtmp == first || count++ > budget) {
			/* Break out of looping */
			break;
		}

		iso_rl_dequeue((unsigned long)q);
	}

	if(!list_empty(&cb->active_list) && !iso_exiting) {
		dt = iso_rl_gettimeout();
		hrtimer_start(&cb->timer, dt, HRTIMER_MODE_REL_PINNED);
	}
}


int iso_rl_init(struct iso_rl *rl) {
	int i;
	rl->rate = ISO_RATE_INITIAL;
	rl->total_tokens = 15000;
	rl->last_update_time = ktime_get();
	rl->queue = alloc_percpu(struct iso_rl_queue);

	if(rl->queue == NULL)
		return -1;

	rl->weight = 1;
	rl->active_weight = 0;
	rl->waiting = 0;

	spin_lock_init(&rl->spinlock);

	for_each_possible_cpu(i) {
		struct iso_rl_queue *q = per_cpu_ptr(rl->queue, i);
		struct iso_rl_cb *cb = per_cpu_ptr(rlcb, i);

		skb_queue_head_init(&q->list);
		q->first_pkt_size = 0;
		q->bytes_enqueued = 0;
		q->bytes_xmit = 0;

		q->tokens = 0;

		q->cpu = i;
		q->rl = rl;
		q->cputimer = &cb->timer;
		q->waiting = 0;

		INIT_LIST_HEAD(&q->active_list);
	}

  INIT_LIST_HEAD(&rl->list);
  INIT_LIST_HEAD(&rl->siblings);
  INIT_LIST_HEAD(&rl->children);
  INIT_LIST_HEAD(&rl->waiting_list);
  INIT_LIST_HEAD(&rl->waiting_node);
  rl->leaf = 1;
  rl->parent = NULL;
  return 0;
}

struct iso_rl *iso_rl_new(char *name) {
	struct iso_rl *rl = kmalloc(sizeof(*rl), GFP_KERNEL);
	if(rl == NULL) {
		printk(KERN_INFO "Could not allocate rl %s\n", name);
		return NULL;
	}

	if(iso_rl_init(rl)) {
		printk(KERN_INFO "rl %s init failed\n", name);
		kfree(rl);
		return NULL;
	}

	strcpy(rl->name, name);
	list_add_tail(&rl->list, &rls);
	return rl;
}

// Conveniently ignore locking for now
int iso_rl_attach(struct iso_rl *rl, struct iso_rl *child) {
	if(child->parent == rl)
		return 0;

	// TODO: ensure rl's and child's queues are flushed
	if(rl->leaf) {
		rl->leaf = 0;
		free_percpu(rl->queue);
	}

	list_move_tail(&child->siblings, &rl->children);
	child->parent = rl;
	return 0;
}

void iso_rl_free(struct iso_rl *rl) {
	list_del_init(&rl->list);
	if(rl->leaf)
		free_percpu(rl->queue);
	kfree(rl);
}

/* Dequeue from active rate limiters on this cpu */
void iso_rl_dequeue_root() {
	struct iso_rl_cb *cb = per_cpu_ptr(rlcb, smp_processor_id());
	iso_rl_xmit_tasklet((unsigned long) cb);
}

/* Called with rcu lock */
void iso_rl_show(struct iso_rl *rl, struct seq_file *s) {
	struct iso_rl_queue *q;
	int i, first = 1;

	seq_printf(s, "name %s  parent %s  rate %u  total_tokens %llu   last %llx   %p (%p)\n",
             rl->name, rl->parent ? rl->parent->name : "(root)",
             rl->rate, rl->total_tokens, *(u64 *)&rl->last_update_time,
             rl, rl->parent);

	seq_printf(s, "   wt %d, aw %d, waiting %d\n",
						 rl->weight, rl->active_weight, rl->waiting);

	for_each_online_cpu(i) {
		if(first) {
			seq_printf(s, "\tcpu   len"
								 "   first_len   queued   tokens  active?\n");
			first = 0;
		}
		q = per_cpu_ptr(rl->queue, i);

		if(q->tokens > 0 || skb_queue_len(&q->list) > 0) {
			seq_printf(s, "\t%3d   %3d   %3d   %10llu   %10llu   %d,%d\n",
								 i, skb_queue_len(&q->list), q->first_pkt_size,
								 q->bytes_enqueued, q->tokens,
								 !list_empty(&q->active_list), hrtimer_active(q->cputimer));
		}
	}
}

/* This function could be called from HARDIRQ context */
inline void iso_rl_clock(struct iso_rl *rl) {
	u64 cap, us;
	ktime_t now;

	if(!iso_rl_should_refill(rl))
		return;

	now = ktime_get();
	us = ktime_us_delta(now, rl->last_update_time);
	rl->total_tokens += (rl->rate * us) >> 3;

	/* This is needed if we have TSO.  MIN_BURST_BYTES will be ~64K */
	cap = max((rl->rate * ISO_MAX_BURST_TIME_US) >> 3, (u32)ISO_MIN_BURST_BYTES);
	rl->total_tokens = min(cap, rl->total_tokens);

	rl->last_update_time = now;
}

enum iso_verdict iso_rl_enqueue(struct iso_rl *rl, struct sk_buff *pkt, int cpu) {
	struct iso_rl_queue *q;
	enum iso_verdict verdict;
	s32 len, diff;

	if(!rl->leaf) {
		printk(KERN_INFO "bug: enqueueing into non-leaf rate limiter (%s)\n", rl->name);
		return ISO_VERDICT_PASS;
	}

#define MIN_PKT_SIZE (600)
	q = per_cpu_ptr(rl->queue, cpu);
	// iso_rl_clock(rl);
	len = (s32) skb_size(pkt);

	if(q->bytes_enqueued + len > ISO_MAX_QUEUE_LEN_BYTES) {
		diff = (s32)q->bytes_enqueued + len - ISO_MAX_QUEUE_LEN_BYTES;
		if(diff > len || diff - len < MIN_PKT_SIZE) {
			verdict = ISO_VERDICT_DROP;
			goto done;
		} else {
			skb_trim(pkt, diff);
		}
	}

	/* we don't need locks */
	__skb_queue_tail(&q->list, pkt);
	q->bytes_enqueued += skb_size(pkt);

	verdict = ISO_VERDICT_SUCCESS;
	iso_rl_activate_queue(q);
 done:
	return verdict;
}

/* This function MUST be executed with interrupts enabled */
void iso_rl_dequeue(unsigned long _q) {
	int timeout = 0;
	u64 sum = 0;
	u32 size;
	struct sk_buff *pkt;
	struct iso_rl_queue *q = (struct iso_rl_queue *)_q;
	struct iso_rl *rl = q->rl;
	struct sk_buff_head *skq, list;

	/* Try to borrow from the global token pool; if that fails,
	   program the timeout for this queue */

	if(unlikely(q->tokens < q->first_pkt_size)) {
		timeout = iso_rl_borrow_tokens(rl, q);
		if(timeout)
			goto timeout;
	}

	skb_queue_head_init(&list);
	skq = &q->list;

	if(skb_queue_len(skq) == 0)
		goto unlock;

	pkt = skb_peek(skq);
	sum = size = skb_size(pkt);
	q->first_pkt_size = size;
	timeout = 1;

	while(sum <= q->tokens) {
		pkt = __skb_dequeue(skq);
		q->tokens -= size;
		q->bytes_enqueued -= size;

		// TODO: if skb_xmit fails, break
		skb_xmit(pkt);
		q->bytes_xmit += size;

		if(skb_queue_len(skq) == 0) {
			timeout = 0;
			break;
		}

		pkt = skb_peek(skq);
		sum += (size = skb_size(pkt));
		q->first_pkt_size = size;
	}

 unlock:

	if(!timeout) {
		iso_rl_deactivate_queue(q);
		iso_rl_deactivate_tree(rl, q);
	}

 timeout:
	if(timeout && !iso_exiting) {
		iso_rl_activate_queue(q);
		iso_rl_activate_tree(rl, q);
	}
}

/* HARDIRQ timeout */
enum hrtimer_restart iso_rl_timeout(struct hrtimer *timer) {
	/* schedue xmit tasklet to go into softirq context */
	struct iso_rl_cb *cb = container_of(timer, struct iso_rl_cb, timer);
	tasklet_schedule(&cb->xmit_timeout);
	return HRTIMER_NORESTART;
}

inline int iso_rl_borrow_tokens(struct iso_rl *rl, struct iso_rl_queue *q) {
	unsigned long flags;
	u64 borrow;
	int timeout = 1;

	if(!spin_trylock_irqsave(&rl->spinlock, flags))
		return timeout;

	borrow = max(iso_rl_singleq_burst(rl), (u64)q->first_pkt_size);

	if(rl->total_tokens >= borrow) {
		rl->total_tokens -= borrow;
		q->tokens += borrow;
		timeout = 0;
	}

	if(iso_exiting)
		timeout = 0;

	spin_unlock_irqrestore(&rl->spinlock, flags);
	return timeout;
}

inline void iso_rl_activate_queue(struct iso_rl_queue *q) {
	struct iso_rl_cb *cb = per_cpu_ptr(rlcb, q->cpu);
	// TODO: add front or back of list depending on q->rl->rate
	// Have 4 priorities: 4 (highest), 3 (5--8G), 2 (1--4G), 1 (< 1G)
	if(list_empty(&q->active_list))
		list_add_tail(&q->active_list, &cb->active_list);

	if(!hrtimer_active(&cb->timer))
		hrtimer_start(&cb->timer, iso_rl_gettimeout(), HRTIMER_MODE_REL_PINNED);
}

inline void iso_rl_deactivate_queue(struct iso_rl_queue *q) {
	if(!list_empty(&q->active_list))
		list_del_init(&q->active_list);
}

inline void iso_rl_activate_tree(struct iso_rl *rl, struct iso_rl_queue *q) {
	unsigned long flags, done;
	struct iso_rl *parent = rl->parent;

	if(!q->waiting && parent) {
		q->waiting = 1;
		done = 0;
		do {
			spin_lock_irqsave(&parent->spinlock, flags);
			rl->waiting += 1;
			if(rl->waiting == 1) {
				parent->active_weight += rl->weight;
				list_add_tail(&rl->waiting_node, &parent->waiting_list);
			} else {
				/* parent has something waiting already, so it would
				 * already be present in its parent's waiting list */
				done = 1;
			}
			spin_unlock_irqrestore(&parent->spinlock, flags);
			rl = parent;
			parent = rl->parent;
		} while(!done && parent);
	}
}

inline void iso_rl_deactivate_tree(struct iso_rl *rl, struct iso_rl_queue *q) {
	unsigned long flags, done;
	struct iso_rl *parent = rl->parent;

	if(q->waiting && parent) {
		q->waiting = 0;
		done = 0;

		do {
			spin_lock_irqsave(&parent->spinlock, flags);
			rl->waiting -= 1;
			if(rl->waiting == 0) {
				parent->active_weight -= rl->weight;
				list_del_init(&rl->waiting_node);
			} else {
				/* parent still has someone else waiting, so let's not
				 * remove parent from the tree */
				done = 1;
			}
			spin_unlock_irqrestore(&rl->parent->spinlock, flags);

			rl = parent;
			parent = rl->parent;
		} while(!done && parent);
	}
}

inline void _iso_rl_fill_tokens(struct iso_rl *rl, u64 tokens) {
	struct iso_rl *childrl, *rlnext;
	unsigned long flags;
	u32 child_share;

	spin_lock_irqsave(&rl->spinlock, flags);
	if(rl->parent == NULL) {
		iso_rl_clock(rl);
	} else {
		/* TODO: cap total_tokens and carry over the unused tokens */
		rl->total_tokens += tokens;
	}

	if(!rl->active_weight) {
		/* we've reached a leaf, so just unlock and get away! */
		goto unlock;
	}

	child_share = rl->total_tokens / rl->active_weight;
	if(child_share == 0)
		goto unlock;

	list_for_each_entry_safe(childrl, rlnext, &rl->waiting_list, waiting_node) {
		rl->total_tokens -= child_share;
		_iso_rl_fill_tokens(childrl, child_share);
	}

 unlock:
	spin_unlock_irqrestore(&rl->spinlock, flags);
}

inline void iso_rl_fill_tokens(void) {
	/* Needn't execute this simultaneously on all CPUs */
	static unsigned long flags = 0;
	if(test_and_set_bit(0, &flags)) {
		_iso_rl_fill_tokens(rootrl, 0);
		clear_bit(0, &flags);
	}
}

/* Local Variables: */
/* indent-tabs-mode:t */
/* End: */
