/* $SchulteIT: queue.c 15189 2025-10-27 05:41:45Z schulte $ */
/* $JDTAUS$ */

/*
 * Copyright (c) 2018 - 2026 Christian Schulte <cs@schulte.it>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifdef HAVE_HOST_H
#include "host.h"
#endif

#include "heap.h"
#include "queue.h"
#include "thread.h"
#include "time.h"

struct Queue {
  void **items;
  size_t capacity;
  time_t timeout;
  size_t size;
  size_t front;
  size_t rear;
  _Atomic bool running;
  _Atomic bool enqueue_timedout;
  _Atomic bool dequeue_timedout;
  mtx_t mtx;
  cnd_t not_empty;
  cnd_t not_full;
};

inline struct Queue *Queue_new(const size_t capacity, const time_t timeout) {
  struct Queue *restrict q = heap_malloc(sizeof(struct Queue));
  q->items = heap_calloc(capacity, sizeof(void *));
  mutex_init(&q->mtx);
  condition_init(&q->not_empty);
  condition_init(&q->not_full);
  q->capacity = capacity;
  q->timeout = timeout;
  q->size = 0;
  q->front = 0;
  q->rear = -1;
  q->running = false;
  q->enqueue_timedout = false;
  q->dequeue_timedout = false;
  return q;
}

inline void Queue_delete(struct Queue *restrict const q,
                         void (*cb)(void *restrict const)) {
  condition_destroy(&q->not_empty);
  condition_destroy(&q->not_full);
  mutex_destroy(&q->mtx);

  if (cb)
    for (size_t i = q->capacity; i-- > 0;)
      cb(q->items[i]);

  heap_free(q->items);
  heap_free(q);
}

inline void Queue_start(struct Queue *restrict const q) { q->running = true; }

inline void Queue_stop(struct Queue *restrict const q) {
  q->running = false;
  condition_broadcast(&q->not_empty);
  condition_broadcast(&q->not_full);
}

inline bool Queue_enqueue_timedout(struct Queue *restrict const q) {
  return q->enqueue_timedout;
}
inline bool Queue_dequeue_timedout(struct Queue *restrict const q) {
  return q->dequeue_timedout;
}

inline void Queue_enqueue_await(struct Queue *restrict const q,
                                void *restrict const item) {
  struct timespec to;

  mutex_lock(&q->mtx);

  while (q->running && q->size == q->capacity) {
    if (q->timeout) {
      time_now(&to);

      to.tv_sec += q->timeout;

      q->enqueue_timedout = !condition_timedwait(&q->not_full, &q->mtx, &to);
    } else
      condition_wait(&q->not_full, &q->mtx);
  }

  if (q->running && !q->enqueue_timedout) {
    q->rear = (q->rear + 1) % q->capacity;
    q->items[q->rear] = item;
    q->size++;

    condition_signal(&q->not_empty);
  }

  mutex_unlock(&q->mtx);
}

inline void *Queue_dequeue_await(struct Queue *restrict const q) {
  void *restrict item = NULL;
  struct timespec to;

  mutex_lock(&q->mtx);

  while (q->running && q->size == 0) {
    if (q->timeout) {
      time_now(&to);

      to.tv_sec += q->timeout;

      q->dequeue_timedout = !condition_timedwait(&q->not_empty, &q->mtx, &to);
    } else
      condition_wait(&q->not_empty, &q->mtx);
  }

  if (q->running && !q->dequeue_timedout) {
    item = q->items[q->front];
    q->items[q->front] = NULL;
    q->front = (q->front + 1) % q->capacity;
    q->size--;

    condition_signal(&q->not_full);
  }

  mutex_unlock(&q->mtx);

  return item;
}
