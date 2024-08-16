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
    atomic_store_explicit(&gc->active[tid], true, memory_order_relaxed);
    atomic_store_explicit(&gc->thread_epoch[tid], atomic_load_explicit(&gc->epoch, memory_order_relaxed), memory_order_relaxed);
}

void exit_critical(epoch_gc_t *gc, int tid) {
    atomic_store_explicit(&gc->active[tid], false, memory_order_relaxed);
}

void reclaim_nodes(node_t *head) {
    while (head) {
        node_t *temp = head;
        head = head->next;
        free(temp);
    }
}

void collect_garbage(epoch_gc_t *gc) {
    unsigned int global_epoch = atomic_load_explicit(&gc->epoch, memory_order_relaxed);
    unsigned int prev_epoch = (global_epoch + 2) % 3;

    bool can_advance = true;
    for (int i = 0; i < MAX_THREADS; i++) {
        if (atomic_load_explicit(&gc->active[i], memory_order_relaxed) && atomic_load_explicit(&gc->thread_epoch[i], memory_order_relaxed) != global_epoch) {
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
    unsigned int current_epoch = atomic_load_explicit(&gc->epoch, memory_order_relaxed);
    node->next = gc->garbage[current_epoch];
    gc->garbage[current_epoch] = node;
}

// Example usage with a Treiber Stack

typedef struct {
    atomic_uintptr_t head;
    epoch_gc_t gc;
} my_stack_t;

void init_stack(my_stack_t *stack) {
    atomic_store_explicit(&stack->head, (uintptr_t)NULL, memory_order_relaxed);
    init_gc(&stack->gc);
}

void push(my_stack_t *stack, void *data, int tid) {
    node_t *node = malloc(sizeof(node_t));
    node->data = data;
    
    enter_critical(&stack->gc, tid);
    
    uintptr_t head = atomic_load_explicit(&stack->head, memory_order_relaxed);
    node->next = (node_t *)head;
    bool success = false;
    while (!success) {
        success = atomic_compare_exchange_strong_explicit(&stack->head, &head, (uintptr_t)node, memory_order_release, memory_order_relaxed);
        if (!success) {
            node->next = (node_t *)head;
        }
    }
    
    exit_critical(&stack->gc, tid);
}

void *pop(my_stack_t *stack, int tid) {
    void *data = NULL;
    
    enter_critical(&stack->gc, tid);
    
    uintptr_t head = atomic_load_explicit(&stack->head, memory_order_acquire);
    node_t *node = NULL;
    while (head != 0) {
        node = (node_t *)head;
        uintptr_t next = (uintptr_t)(node->next);
        if (atomic_compare_exchange_strong_explicit(&stack->head, &head, next, memory_order_release, memory_order_relaxed)) {
            data = node->data;
            add_garbage(&stack->gc, node);
            break;
        }
    }
    
    if (head == 0) {
        data = NULL;
    }
    
    exit_critical(&stack->gc, tid);
    collect_garbage(&stack->gc);
    
    return data;
}

typedef struct {
    my_stack_t *stack;
    int tid;
} thread_arg_t;

void *thread_func(void *arg) {
    thread_arg_t *thread_arg = (thread_arg_t *)arg;
    my_stack_t *stack = thread_arg->stack;
    int tid = thread_arg->tid;

    for (int i = 0; i < 10; i++) {  // Adjust the number of pushes
        push(stack, (void *)(intptr_t)i, tid);
        printf("Thread %d pushed: %d\n", tid, i);
    }

    for (int i = 0; i < 10; i++) {  // Adjust the number of pops
        void *data = pop(stack, tid);
        if (data != NULL) {
            printf("Thread %d popped: %ld\n", tid, (long)data);
        } else {
            printf("Thread %d popped: NULL\n", tid);
        }
    }

    return NULL;
}

int main() {
    my_stack_t stack;
    init_stack(&stack);

    pthread_t threads[MAX_THREADS];
    thread_arg_t thread_args[MAX_THREADS];

    for (int i = 0; i < MAX_THREADS; i++) {
        thread_args[i].stack = &stack;
        thread_args[i].tid = i;
        pthread_create(&threads[i], NULL, thread_func, &thread_args[i]);
    }

    for (int i = 0; i < MAX_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    return 0;
}

