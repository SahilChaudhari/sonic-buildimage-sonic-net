// SPDX-License-Identifier: MIT or GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (C) 2024 Advanced Micro Devices, Inc.

#ifdef __KERNEL__
#include <linux/kthread.h>    // included for threading related functions
#include <linux/sched.h>      // included to create tesk_struct
#include <linux/delay.h>      // included for the sleep/delay function in the thread
#include <linux/version.h>    // included for kernel compat
#include <linux/hrtimer.h>    // included for hrtimer declarations
#endif

#include <zephyr/kernel.h>


#ifdef __KERNEL__
static void k_work_fn(struct work_struct *ws)
{
  struct k_work *w = CONTAINER_OF(ws, struct k_work, w);
  unsigned long flags;

  spin_lock_irqsave(&w->s, flags);
  if (!(w->flags & K_WORK_QUEUED)) {
    /* see k_work_cancel */
    spin_unlock_irqrestore(&w->s, flags);
    return;
  }
  w->flags &= ~K_WORK_QUEUED;
  w->flags |= K_WORK_RUNNING;
  spin_unlock_irqrestore(&w->s, flags);

  w->fn(w);

  spin_lock_irqsave(&w->s, flags);
  w->flags &= ~K_WORK_RUNNING;
  if (w->flags & K_WORK_CANCELING) {
    complete(&w->canceled);
    w->flags &= ~K_WORK_CANCELING;
  }
  spin_unlock_irqrestore(&w->s, flags);
}

void k_work_queue_fini(struct k_work_q *q)
{
  destroy_workqueue(q->workq);
}
#else

static void *k_worker_fn(void *arg)
{
  struct k_work_q *q = arg;

  pthread_mutex_lock(&q->m);

  while (1) {
    sys_dnode_t *node;

    if (q->stop) {
      while ((node = sys_dlist_get(&q->q))) {
        struct k_work *w = CONTAINER_OF(node, struct k_work, node);
        w->q = NULL; // atomic??
      }
      break;
    } else if ((node = sys_dlist_get(&q->q))) {
      struct k_work *w = CONTAINER_OF(node, struct k_work, node);
      w->flags &= ~K_WORK_QUEUED;
      w->flags |= K_WORK_RUNNING;
      pthread_mutex_unlock(&q->m);
      w->fn(w);
      pthread_mutex_lock(&q->m);
      w->flags &= ~K_WORK_RUNNING;
      if (w->flags & K_WORK_CANCELING) {
        pthread_cond_signal(&w->canceled);
        w->flags &= ~K_WORK_CANCELING;
      }
      w->q = NULL;// atomic??
    } else {
      pthread_cond_wait(&q->c, &q->m);
    }
  }

  pthread_mutex_unlock(&q->m);
  return NULL;
}

static void k_work_queue_fini(struct k_work_q *q)
{
  pthread_mutex_lock(&q->m);
  q->stop = true;
  pthread_cond_signal(&q->c);
  pthread_mutex_unlock(&q->m);
  pthread_join(q->t, NULL);
}

static struct k_work_q sys_queue;

__attribute__((constructor))
static void sys_queue_init(void)
{
  k_work_queue_init(&sys_queue);
}

__attribute__((destructor))
static void sys_queue_fini(void)
{
  k_work_queue_fini(&sys_queue);
}
#endif

void k_work_queue_init(struct k_work_q *q)
{
#ifdef __KERNEL__
  q->workq = alloc_workqueue("tawk_wq", WQ_UNBOUND | WQ_CPU_INTENSIVE | WQ_HIGHPRI, 0);
  if (!q->workq) {
    panic("Couldn't allocate TAWK IPC workqueue\n");
  }
#else
  sys_dlist_init(&q->q);
  q->stop = false;
  q->m = (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;
  q->c = (pthread_cond_t)PTHREAD_COND_INITIALIZER;
  pthread_create(&q->t, NULL, k_worker_fn, q);
#endif
}

void k_work_init(struct k_work *w, void (*fn)(struct k_work *))
{
  w->fn = fn;
#ifdef __KERNEL__
  INIT_WORK(&w->w, k_work_fn);
  spin_lock_init(&w->s);
  init_completion(&w->canceled);
#else
  w->q = NULL;
  sys_dnode_init(&w->node);
  w->canceled = (pthread_cond_t)PTHREAD_COND_INITIALIZER;
#endif
  w->flags = 0;
}

void k_work_queue_start(struct k_work_q *queue, k_thread_stack_t *stack, size_t stack_size, int prio,
                        const struct k_work_queue_config *cfg)
{
  /* Do nothing, we start the threads as part of the init process */
}

int k_work_submit_to_queue(struct k_work_q *q, struct k_work *w)
{
#ifdef __KERNEL__
  bool queued;
  int rc;

  spin_lock(&w->s);

  if (q)
    queued = queue_work(q->workq, &w->w);
  else
    queued = schedule_work(&w->w);

  if (queued) {
    w->flags |= K_WORK_QUEUED;
    rc = w->flags & K_WORK_RUNNING ? 2 : 0;
  } else {
    rc = 1;
  }

  spin_unlock(&w->s);

  return rc;
#else
  if (!sys_dnode_is_linked(&w->node)) {
    pthread_mutex_lock(&q->m);
    sys_dlist_append(&q->q, &w->node);
    w->flags |= K_WORK_QUEUED;
    w->q = q;
    pthread_cond_signal(&q->c);
    pthread_mutex_unlock(&q->m);
    return 0;
  }
#endif
}

int k_work_submit(struct k_work *w)
{
#ifdef __KERNEL__
  return k_work_submit_to_queue(NULL, w);
#else
  return k_work_submit_to_queue(&sys_queue, w);
#endif
}

int k_work_busy_get(struct k_work *w)
{
  unsigned long irq_flags;
  int flags;

#ifdef __KERNEL__
  spin_lock_irqsave(&w->s, irq_flags);
#endif

  flags = w->flags;

#ifdef __KERNEL__
  spin_unlock_irqrestore(&w->s, irq_flags);
#endif

  return flags;
}

static void __k_work_cancel(struct k_work *w, int sync)
{
#ifdef __KERNEL__
  unsigned long flags;
  bool need_wait = false;

  spin_lock_irqsave(&w->s, flags);

  if (w->flags & K_WORK_QUEUED)
    w->flags &= ~K_WORK_QUEUED;

  if (sync && (w->flags & K_WORK_RUNNING)) {
    w->flags |= K_WORK_CANCELING;
    need_wait = true;
  }

  spin_unlock_irqrestore(&w->s, flags);

  if (need_wait)
    wait_for_completion(&w->canceled);
#else
  struct k_work_q *q = w->q;

  if (q != NULL) {
    pthread_mutex_lock(&q->m);
    if (sys_dnode_is_linked(&w->node)) {
      sys_dlist_remove(&w->node);
      w->flags &= ~K_WORK_QUEUED;
    }
    if (sync && (w->flags & K_WORK_RUNNING)) {
      w->flags |= K_WORK_CANCELING;
      pthread_cond_wait(&w->canceled, &q->m);
    }
    pthread_mutex_unlock(&q->m);
  }
#endif
}

void k_work_cancel(struct k_work *w)
{
  __k_work_cancel(w, 0);
}

void k_work_cancel_sync(struct k_work *w, struct k_work_sync *ws)
{
  __k_work_cancel(w, 1);
}

k_timepoint_t sys_now(void)
{
  k_timepoint_t now;

#if defined(__KERNEL__) && LINUX_VERSION_CODE >= KERNEL_VERSION(5,6,0)
  struct timespec64 spec;
#else
  struct timespec spec;
#endif
  int rc;

#ifdef __KERNEL__
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,6,0)
  ktime_get_ts64(&spec);
#else
  ktime_get_ts(&spec);
#endif
  (void)rc;
#else
  rc = clock_gettime(CLOCK_REALTIME, &spec);
  assert(rc == 0);
#endif

  now = spec.tv_sec * 1000000000 + spec.tv_nsec;
  return now;
}

k_timepoint_t sys_timepoint_calc(k_timeout_t timeout)
{
  if (timeout == K_FOREVER)
    return K_FOREVER;
  return sys_now() + timeout;
}

bool sys_timepoint_expired(k_timepoint_t timepoint)
{
  return sys_now() > timepoint;
}

int k_sleep(k_timeout_t timeout)
{
#ifdef __KERNEL__
  msleep(timeout / 1000000);
#else
  usleep(timeout / 1000);
#endif
  return 0;
}


void k_timer_init(struct k_timer *t,
                  void (*f_timer)(struct k_timer *),
                  void (*f_stop)(struct k_timer *))
{
  t->f_timer = f_timer;
  t->f_stop = f_stop;
  t->running = false;
}

#ifdef __KERNEL__
static int k_timer_fn(void *arg)
{
  struct k_timer *t = arg;

  wait_event_hrtimeout(t->wq, t->stop, ns_to_ktime(t->d1));
  while (!t->stop) {
    t->f_timer(t);
    if (t->d2 == K_NO_WAIT) {
      t->stop = true;
      break;
    }
    wait_event_hrtimeout(t->wq, t->stop, ns_to_ktime(t->d2));
  }

  t->running = false;

  complete(&t->stopped);
  return 0;
}
#else
static void *k_timer_fn(void *arg)
{
  struct k_timer *t = arg;

  k_sleep(t->d1);
  while (!t->stop) {
    t->f_timer(t);
    if (t->d2 == K_NO_WAIT) {
      t->stop = true;
      break;
    }
    k_sleep(t->d2);
  }

  t->running = false;

  return NULL;
}
#endif

void k_timer_start(struct k_timer *t, k_timeout_t d1, k_timeout_t d2)
{
  t->d1 = d1;
  t->d2 = d2;
  t->running = true;
  t->stop = false;
#ifdef __KERNEL__
  init_waitqueue_head(&t->wq);
  init_completion(&t->stopped);
  t->kth = kthread_create(k_timer_fn, t, "timmy timer");
  wake_up_process(t->kth);
#else
  pthread_create(&t->t, NULL, k_timer_fn, t);
#endif
}

void k_timer_stop(struct k_timer *t)
{
  if (!t->running)
    return;

  /* let's hope the timer is short */
  t->stop = true;
#ifdef __KERNEL__
  wake_up(&t->wq);
  wait_for_completion(&t->stopped);
#else
  pthread_join(t->t, NULL);
#endif
  if (t->f_stop)
    t->f_stop(t);
#ifdef __KERNEL__
  if (t->running)
    panic("claimed still running");
#else
  assert(!t->running);
#endif
}

void k_timer_status_sync(struct k_timer *t)
{
  /* Stop joined the thread, so nothing to do (we only support start/stop for now) */
  (void) t;
}

void k_mutex_init(struct k_mutex *m)
{
#ifdef __KERNEL__
  mutex_init(&m->m);
#else
  m->m = (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;
#endif
}

int k_mutex_lock(struct k_mutex *m, k_timeout_t t)
{
  switch (t) {
  case K_FOREVER: {
#ifdef __KERNEL__
    mutex_lock(&m->m);
#else
    pthread_mutex_lock(&m->m);
#endif
  } break;

  case K_NO_WAIT:{
#ifdef __KERNEL__
    int got_lock = mutex_trylock(&m->m);
    if (!got_lock)
      return EBUSY;
#else
    int rc = pthread_mutex_trylock(&m->m);
    if (rc != 0)
      return rc;
#endif
  } break;

  default: {
#ifdef __KERNEL__
    k_timepoint_t expiry = sys_timepoint_calc(t);
    while (1) {
      int got_lock = mutex_trylock(&m->m);
      if (got_lock)
        break;
      else if (sys_timepoint_expired(expiry))
        return ETIME;
      msleep(1);
    }
#else
    struct timespec spec;
    spec.tv_sec = t / 1000000000;
    spec.tv_nsec = t % 1000000000;
    int rc = pthread_mutex_timedlock(&m->m, &spec);
    if (rc != 0)
      return rc;
#endif
  } break;
  }
  m->owner = _current;
  return 0;
}

void k_mutex_unlock(struct k_mutex *m)
{
  m->owner = NULL;
#ifdef __KERNEL__
  mutex_unlock(&m->m);
#else
  pthread_mutex_unlock(&m->m);
#endif
}


void k_condvar_init(struct k_condvar *c)
{
#ifdef __KERNEL__
  init_completion(&c->c);
#else
  c->c = (pthread_cond_t)PTHREAD_COND_INITIALIZER;
#endif
}

int k_condvar_wait(struct k_condvar *c, struct k_mutex *m, k_timeout_t t)
{
  int rv;

#ifdef __KERNEL__
  /* bleurgh */
  k_mutex_unlock(m);
  wait_for_completion(&c->c);
  k_mutex_lock(m, K_FOREVER);
  rv = 0;
#else
  m->owner = NULL;
  rv = pthread_cond_wait(&c->c, &m->m);
  m->owner = _current;
#endif
  return rv;
}

void k_condvar_signal(struct k_condvar *c)
{
#ifdef __KERNEL__
  complete(&c->c);
#else
  pthread_cond_signal(&c->c);
#endif
}
