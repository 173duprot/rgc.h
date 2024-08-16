#include <stdatomic.h>
#include <stdalign.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>

#define MAX_THREADS 2
#define CACHE_LINE_SIZE 64

typedef struct node_t {
    alignas(64) struct node_t *next;
    void *data;
} node_t;

typedef struct {
    alignas(64) atomic_uint epoch;
                node_t *garbage[3];
    alignas(64) atomic_bool active[MAX_THREADS];
    alignas(64) atomic_uint thread_epoch[MAX_THREADS];
} epoch_gc_t;

void init_gc(epoch_gc_t *gc) {
    atomic_store_explicit(&gc->epoch, 0, memory_order_relaxed);
    for (int i = 0; i < 3; i++) {
        gc->garbage[i] = NULL;
    }
    for (int i = 0; i < MAX_THREADS; i++) {
        atomic_store_explicit(&gc->active[i], false, memory_order_relaxed);
        atomic_store_explicit(&gc->thread_epoch[i], 0, memory_order_relaxed);
    }
}

void enter_critical(epoch_gc_t *gc, int tid) {
    atomic_store_explicit(&gc->thread_epoch[tid], atomic_load_explicit(&gc->epoch, memory_order_relaxed), memory_order_release);
    atomic_store_explicit(&gc->active[tid], true, memory_order_release);
}

void exit_critical(epoch_gc_t *gc, int tid) {
    atomic_store_explicit(&gc->active[tid], false, memory_order_release);
}

void reclaim_nodes(node_t *head) {
    while (head) {
        node_t *temp = head;
        head = head->next;
        free(temp);
    }
}

void collect_garbage(epoch_gc_t *gc) {
    unsigned int global_epoch = atomic_load_explicit(&gc->epoch, memory_order_acquire);
    unsigned int prev_epoch = (global_epoch + 2) % 3;

    bool can_advance = true;
    for (int i = 0; i < MAX_THREADS; i++) {
        bool is_active = atomic_load_explicit(&gc->active[i], memory_order_acquire);
        unsigned int thread_epoch = atomic_load_explicit(&gc->thread_epoch[i], memory_order_acquire);

        if (is_active && thread_epoch != global_epoch) {
            can_advance = false;
            break;
        }
    }

    if (can_advance) {
        unsigned int new_epoch = (global_epoch + 1) % 3;
        if (atomic_compare_exchange_strong_explicit(&gc->epoch, &global_epoch, new_epoch, memory_order_release, memory_order_relaxed)) {
            reclaim_nodes(gc->garbage[prev_epoch]);
            gc->garbage[prev_epoch] = NULL;
        }
    }
}

void add_garbage(epoch_gc_t *gc, node_t *node) {
    unsigned int current_epoch = atomic_load_explicit(&gc->epoch, memory_order_acquire);
    node->next = gc->garbage[current_epoch];
    gc->garbage[current_epoch] = node;
}

