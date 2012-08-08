
#include "prl.h"

struct iso_rl_cb __percpu *rlcb;
struct iso_rl *prl;
extern int iso_exiting;

int ISO_TOKENBUCKET_TIMEOUT_NS=50*1000;
int ISO_MAX_BURST_TIME_US=100;
int ISO_BURST_FACTOR=8;
int ISO_RL_UPDATE_INTERVAL_US=20;
int ISO_RATE_INITIAL=1000;
int ISO_MAX_QUEUE_LEN_BYTES=256 * 1024;
int ISO_MIN_BURST_BYTES=65536;

/* Called the first time when the module is initialised */
int iso_rl_prep() {
	int cpu;

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

  prl = kmalloc(sizeof(*prl), GFP_KERNEL);
  if(prl == NULL) {
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

		list_del_init(&q->active_list);
		iso_rl_clock(q->rl);
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

		INIT_LIST_HEAD(&q->active_list);
	}

  return 0;
}


void iso_rl_free(struct iso_rl *rl) {
	free_percpu(rl->queue);
	kfree(rl);
}


/* Called with rcu lock */
void iso_rl_show(struct iso_rl *rl, struct seq_file *s) {
	struct iso_rl_queue *q;
	int i, first = 1;

	seq_printf(s, "rate %u   total_tokens %llu   last %llx   %p\n",
			   rl->rate, rl->total_tokens, *(u64 *)&rl->last_update_time, rl);

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
	struct iso_rl_queue *q = per_cpu_ptr(rl->queue, cpu);
	enum iso_verdict verdict;
	s32 len, diff;

#define MIN_PKT_SIZE (600)

	iso_rl_clock(rl);
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

timeout:
	if(timeout && !iso_exiting) {
		struct iso_rl_cb *cb = per_cpu_ptr(rlcb, q->cpu);

		/* don't recursively add! */
		if(list_empty(&q->active_list)) {
			list_add_tail(&q->active_list, &cb->active_list);
		}

		if(!hrtimer_active(&cb->timer))
			hrtimer_start(&cb->timer, iso_rl_gettimeout(), HRTIMER_MODE_REL_PINNED);
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
