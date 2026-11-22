#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdnoreturn.h>

#define STACK_SIZE (4 * 1024) // TODO: Learn why i can not use lower stack size.

static noreturn void hello() {
    printf("I LOVE WAKING UP ON A NEW STACK!\n");
    for (;;) {
        __asm__ volatile ("pause\n\t");
    }
}

typedef struct {
    uintptr_t rsp;
} ThreadCotext;

__attribute__((naked, noreturn))
static void gt_switch(ThreadCotext *ctx) {
    __asm__ volatile(
        "movq (%rdi), %rsp\n\t"
        "ret\n\t"
    );
}

int main (void) {
    uintptr_t stack = (uintptr_t)calloc(STACK_SIZE, sizeof(uint8_t));
    if ((uintptr_t)NULL == stack) {
        fprintf(stderr, "ERROR: calloc stack");
        return EXIT_FAILURE;
    }

    uintptr_t stack_top    = (uintptr_t)((stack + STACK_SIZE) & ~0xF);
    uintptr_t *initial_rsp = (uintptr_t *)(stack_top -  2 * sizeof(uintptr_t));
    *initial_rsp = (uintptr_t)hello;

    // display the stack as a hex dump 8byte per line
    for (uintptr_t i = 0; i < STACK_SIZE; i += 8) {
        printf("%016lx: %016lx\n", stack + i, *(uintptr_t *)(stack + i));
    }

    gt_switch(&(ThreadCotext) {(uintptr_t) initial_rsp});

    return EXIT_SUCCESS;
}
