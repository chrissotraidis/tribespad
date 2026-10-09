#include "../port/llasm-support/llasm_cpu.h"
#include "../port/core/guest_memory_checks.h"
#include <assert.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

extern const uint32_t tribes_dispatch_count, tribes_dispatch_vas[];
extern void (*const tribes_dispatch_fns[])(_cpu *);
extern void (*tribes_lookup(uint32_t))(_cpu *);
static _Thread_local uint64_t permissions;
static _Thread_local uint32_t denied;
static _Thread_local int provider;
void tribes_return_to_host(_cpu *cpu) { (void)cpu; }
static void first(_cpu *cpu) { (void)cpu; }
static void second(_cpu *cpu) { (void)cpu; }
int tribes_dispatch_permitted(uint32_t address) {
    ++permissions;
    return address != denied;
}
void *tribes_native_lookup(uint32_t address) {
    return address == 0xfff12340 ? (provider ? second : first) : NULL;
}
void *tribes_dsound_native_lookup(uint32_t address) {
    return address == 0xfff12344 ? (provider ? second : first) : NULL;
}
void *tribes_graphics_native_lookup(uint32_t address) {
    return address == 0xfff12348 ? (provider ? second : first) : NULL;
}
static void check(uint32_t index) {
    uint64_t before = permissions;
    assert(tribes_lookup(tribes_dispatch_vas[index]) == tribes_dispatch_fns[index]);
    assert(permissions == before + 1);
}
static void *worker(void *argument) {
    uint32_t state = (uint32_t)(uintptr_t)argument + 1;
    for (unsigned i = 0; i < 50000; ++i) {
        state = state * 1664525 + 1013904223;
        check(state % tribes_dispatch_count);
        check(state % tribes_dispatch_count); /* includes a warm lookup */
    }
    return NULL;
}
static double now(void) {
    struct timespec t;
    assert(!clock_gettime(CLOCK_MONOTONIC, &t));
    return t.tv_sec + t.tv_nsec * 1e-9;
}
static void expect_abort(uint32_t address, int deny) {
    pid_t child = fork(); assert(child >= 0);
    if (!child) {
        if (deny) denied = address;
        (void)tribes_lookup(address);
        _exit(99);
    }
    int status;
    assert(waitpid(child, &status, 0) == child);
    assert(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT);
}
int main(int argc, char **argv) {
    (void)argv;
    void *guest = mmap(NULL, 65536, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANON, -1, 0);
    assert(guest != MAP_FAILED && tribes_memory_checks_configure((uintptr_t)guest, 65536));
    assert(tribes_dispatch_count > 100000);
    for (uint32_t i = 0; i < tribes_dispatch_count; ++i) { check(i); check(i); }
    for (uint32_t i = tribes_dispatch_count; i-- > 0;) check(i);
    /* Publication must be rechecked even for a cached successful target. */
    check(0);
    if (argc == 1) {
        expect_abort(tribes_dispatch_vas[0], 1);
        expect_abort(0xfff1234c, 0);
    }
    assert(tribes_lookup(0xfffff000) == tribes_return_to_host);
    for (uint32_t address = 0xfff12340; address <= 0xfff12348; address += 4) {
        provider = 0; assert(tribes_lookup(address) == first);
        provider = 1; assert(tribes_lookup(address) == second);
    }
    pthread_t threads[8];
    for (uintptr_t i = 0; i < 8; ++i) assert(!pthread_create(&threads[i], NULL, worker, (void *)i));
    for (unsigned i = 0; i < 8; ++i) assert(!pthread_join(threads[i], NULL));
    /* A localized workload drawn from the full real-address table. This is
       deliberately a lookup benchmark, not a prediction of game frame rate. */
    double start = now();
    for (unsigned i = 0; i < 2000000; ++i) check(((i & 63) * 4729u) % tribes_dispatch_count);
    double hot = now() - start;
    start = now();
    for (unsigned i = 0; i < 400000; ++i) check((i * 73u) % tribes_dispatch_count);
    double cold = now() - start;
    printf("PASS: %u table addresses, warm/reverse/threaded identities, per-call publication, changing native providers; hot=%.6f cold=%.6f\n", tribes_dispatch_count, hot, cold);
    assert(!munmap(guest, 65536));
}
