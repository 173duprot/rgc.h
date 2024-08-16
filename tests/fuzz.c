#include "../rgc.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <assert.h>

#define NUM_THREADS 4
#define NUM_ITERATIONS 100000

typedef struct {
    epoch_gc_t *gc;
    int tid;
} thread_arg_t;

void *thread_func(void *arg) {
    thread_arg_t *targ = (thread_arg_t *)arg;
    epoch_gc_t *gc = targ->gc;
    int tid = targ->tid;

    for (int i = 0; i < NUM_ITERATIONS; i++) {
        enter_critical(gc, tid);

        // Simulate work and add garbage
        node_t *node = (node_t *)malloc(sizeof(node_t));
        assert(node != NULL);  // Ensure malloc succeeded
        node->data = (void *)(uintptr_t)(rand() % 1000);

        add_garbage(gc, node);

        // Randomly decide whether to collect garbage
        if (rand() % 2 == 0) {
            collect_garbage(gc);
        }

        exit_critical(gc, tid);

        // Random delay to simulate variable workload
        usleep(rand() % 1000);
    }

    return NULL;
}

void run_fuzz_test() {
    epoch_gc_t gc;
    init_gc(&gc);

    pthread_t threads[NUM_THREADS];
    thread_arg_t thread_args[NUM_THREADS];

    // Create threads
    for (int i = 0; i < NUM_THREADS; i++) {
        thread_args[i].gc = &gc;
        thread_args[i].tid = i;
        pthread_create(&threads[i], NULL, thread_func, &thread_args[i]);
    }

    // Wait for all threads to finish
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    // Final garbage collection
    collect_garbage(&gc);
    collect_garbage(&gc);
    collect_garbage(&gc);

    // Check if garbage collection succeeded (no remaining garbage)
    for (int i = 0; i < 3; i++) {
        assert(gc.garbage[i] == NULL);
    }

    printf("Fuzz test completed successfully.\n");
}

int main() {
    srand(time(NULL));
    run_fuzz_test();
    return 0;
}

