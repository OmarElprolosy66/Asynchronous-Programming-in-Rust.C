#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdnoreturn.h>
#include <sys/cdefs.h>

#define STACK_SIZE (4 * 1024)
#define MAX_FIBERS 4

typedef void (*counter)(void); // hard coded to be as the book example.

typedef enum {
    AVAILABLE,
    RUNNING,
    READY,
} ar_fiber_state_t;

typedef struct {
    uintptr_t rsp;
    uintptr_t r15;
    uintptr_t r14;
    uintptr_t r13;
    uintptr_t r12;
    uintptr_t rbx;
    uintptr_t rbp;
} ar_fiber_context_t;

typedef struct {
    uintptr_t          stack;
    ar_fiber_context_t ctx;
    ar_fiber_state_t   state;
} ar_fiber_t;

typedef struct {
    ar_fiber_t fibers[MAX_FIBERS];
    size_t     current;
} ar_runtime_t;

ar_fiber_t ar_init_fiber() {
    uintptr_t stack = (uintptr_t)calloc(STACK_SIZE, sizeof(uint8_t));
    if ((uintptr_t)NULL == stack) {
        perror("stack calloc");
        exit(EXIT_FAILURE);
    }

    return (ar_fiber_t) {
        .stack = stack,
        .ctx   = {0},
        .state = AVAILABLE,
    };
}

ar_runtime_t *RUNTIME;
ar_runtime_t *ar_init_runtime() {
    ar_runtime_t *runtime = (ar_runtime_t *)malloc(sizeof(ar_runtime_t));
    if ((ar_runtime_t *)NULL == runtime) {
        perror("runtime alloc");
        exit(EXIT_FAILURE);
    }

    for (int i = 0; i < MAX_FIBERS; i++) {
        runtime->fibers[i] = ar_init_fiber();
    }
    runtime->fibers[0].state = RUNNING;
    runtime->current = 0; // zero points to the first fiber as the base fiber

    RUNTIME = runtime;
    return runtime;
}

__attribute__((noinline))
static bool ar_yield(ar_runtime_t *runtime) {
    size_t curr = runtime->current;
    while (runtime->fibers[curr].state != READY) {
        curr++;
        if (MAX_FIBERS == curr)       curr = 0;
        if (curr == runtime->current) return false;
    }

    if (runtime->fibers[runtime->current].state != AVAILABLE) {
        runtime->fibers[runtime->current].state = READY;
    }

    runtime->fibers[curr].state = RUNNING;

    ar_fiber_context_t *old_ctx = &runtime->fibers[runtime->current].ctx;
    ar_fiber_context_t *new_ctx = &runtime->fibers[curr].ctx;

    runtime->current = curr;

    __asm__ volatile (
        "movq %0, %%rdi\n\t"
        "movq %1, %%rsi\n\t"
        "call ar_switch_context\n\t"
        :
        : "r"(old_ctx), "r"(new_ctx)
        :
    );
    return true;
}

__attribute__((naked))
static void ar_switch_context(ar_fiber_context_t *old_ctx, ar_fiber_context_t *new_ctx) {
    __asm__ volatile(
        // save old_ctx state
        "movq %%rsp, %c0(%%rdi)\n\t"
        "movq %%r15, %c1(%%rdi)\n\t"
        "movq %%r14, %c2(%%rdi)\n\t"
        "movq %%r13, %c3(%%rdi)\n\t"
        "movq %%r12, %c4(%%rdi)\n\t"
        "movq %%rbx, %c5(%%rdi)\n\t"
        "movq %%rbp, %c6(%%rdi)\n\t"
        // recover new_ctx state
        "movq %c0(%%rsi), %%rsp\n\t"
        "movq %c1(%%rsi), %%r15\n\t"
        "movq %c2(%%rsi), %%r14\n\t"
        "movq %c3(%%rsi), %%r13\n\t"
        "movq %c4(%%rsi), %%r12\n\t"
        "movq %c5(%%rsi), %%rbx\n\t"
        "movq %c6(%%rsi), %%rbp\n\t"
        "ret\n\t"
        :
        : "i"(offsetof(ar_fiber_context_t, rsp)),
          "i"(offsetof(ar_fiber_context_t, r15)),
          "i"(offsetof(ar_fiber_context_t, r14)),
          "i"(offsetof(ar_fiber_context_t, r13)),
          "i"(offsetof(ar_fiber_context_t, r12)),
          "i"(offsetof(ar_fiber_context_t, rbx)),
          "i"(offsetof(ar_fiber_context_t, rbp))
        :
    );
}

static void ar_return() {
    if(0 != RUNTIME->current) {
        RUNTIME->fibers[RUNTIME->current].state = AVAILABLE;
        ar_yield(RUNTIME);
    }
}

__attribute__((naked, noreturn))
static void skip() { __asm__ volatile ("ret\n\t"); }

static void guard() {
    if(0 != RUNTIME->current) {
        ar_return();
    }
}

void ar_destroy_runtime(ar_runtime_t *runtime) {
    for (int i = 0; i < MAX_FIBERS; i++) {
        free((void *)runtime->fibers[i].stack);
    }
    free(runtime);
}

void ar_spawn(ar_runtime_t *runtime, counter f) {
    ar_fiber_t *fiber = NULL;
    for (int i = 0; i < MAX_FIBERS; i++) {
        if (runtime->fibers[i].state == AVAILABLE) { fiber = &runtime->fibers[i]; break; }
    }
    if (NULL == fiber) exit(EXIT_FAILURE);

    uintptr_t *stack_top = (uintptr_t *)((fiber->stack + STACK_SIZE) & ~0xF);
    *(stack_top - 1) = (uintptr_t)guard;
    *(stack_top - 2) = (uintptr_t)skip;
    *(stack_top - 3) = (uintptr_t)f;
    fiber->ctx.rsp = (uintptr_t)(stack_top - 3);

    fiber->state = READY;
}

void ar_yield_fiber() {
    ar_yield(RUNTIME);
}

void ar_run() {
    while (ar_yield(RUNTIME)) {}
    exit(EXIT_SUCCESS);
}

void count() {
    size_t id = RUNTIME->current;
    printf("----Start: %zu\n", id);

    for (int i = 0; i < 10; i++) {
        printf("fiber: %zu, counter: %d\n", id, i);
        ar_yield_fiber();
    }


    printf("----Finished: %zu\n", id);
}


int main(void) {
    ar_init_runtime();

    ar_spawn(RUNTIME, count);
    ar_spawn(RUNTIME, count);

    ar_run();

    ar_destroy_runtime(RUNTIME);

    return 0;
}
