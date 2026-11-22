#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdnoreturn.h>

#define STACK_SIZE (64 * 1024)

typedef struct {
    uintptr_t rsp;
} ThreadContext;

static noreturn void hello() {
    puts("I LOVE WAKING UP ON A NEW STACK!");
    for (;;) {
        __asm__ volatile ("pause\n\t");
    }
}

/*
* Switch to another stack and jump to its entry point.
*
* This is a minimal x86-64 System V ABI demonstration.
*/
__attribute__((noreturn, naked))
static void context_switch(ThreadContext *ctx) {
    __asm__ volatile (
        "movq (%rdi), %rsp\n\t"
        "ret\n\t"
    );
}

int main (void) {
    uintptr_t stack = (uintptr_t)calloc(STACK_SIZE, sizeof(char));
    if (stack == (uintptr_t)NULL) {
        perror("calloc");
        return EXIT_FAILURE;
    }

    uintptr_t stack_top = (uintptr_t)(
        // stack grows downward // aligne it to 16 bytes.
        (stack + STACK_SIZE) & ~0xF
    );

    // Reserve space for the initial return address.
    // We remove 16 bytes instead of 8 to make sure the rsp is 16bytes aligned after return;
    // because ret readd 8bytes again to rsp AND system V ABI requers function address to be 16bytes aligned.
    uintptr_t *initial_rsp = (uintptr_t *)(stack_top - 2 * sizeof(uintptr_t));
    *initial_rsp = (uintptr_t)hello;

    ThreadContext ctx = { (uintptr_t)initial_rsp };

    context_switch(&ctx);
    return 0;
}
