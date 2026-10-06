/* SystemProcessorPerformanceInformation (class 8), x86 records of 48 bytes.
 * Mach supplies per-CPU scheduler ticks. KernelTime includes IdleTime, and
 * user time includes nice time. Windows-only reserved fields remain zero.
 * The guest exposes the same first 32 CPUs as GetSystemInfo. No host pointer
 * enters guest memory; the OS writes only host-owned stack/VM buffers.
 * References: Microsoft winternl NtQuerySystemInformation documentation;
 * Apple XNU osfmk/kern/processor.c, processor_cpu_load_info().
 */
#include "guest_system_query.h"
#include <mach/mach.h>
#include <mach/processor_info.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int range(uint64_t base, uint32_t address, uint32_t length) {
    return base && address && (uint64_t)address + length <= UINT64_C(0x100000000)
        && base <= UINTPTR_MAX - ((uint64_t)address + length - 1);
}
static void release_snapshot(processor_info_array_t data, mach_msg_type_number_t words) {
    if (vm_deallocate(mach_task_self(), (vm_address_t)data,
                      (vm_size_t)words * sizeof(integer_t)) != KERN_SUCCESS) {
        fputs("TribesPad: processor snapshot release failed\n", stderr); abort();
    }
}
uint32_t tribes_nt_query_system_information(uint64_t base, uint32_t information_class,
    uint32_t output, uint32_t capacity, uint32_t return_length) {
    if (information_class != 8) {
        fprintf(stderr, "TribesPad: unsupported NtQuerySystemInformation class %u\n", information_class);
        abort();
    }
    if (return_length && !range(base, return_length, 4)) return 0xc0000005u;
    if (capacity < 48) {
        uint32_t length = 0;
        if (return_length) memcpy((void *)(uintptr_t)(base + return_length), &length, 4);
        return 0xc0000004u;
    }
    if (!output) return 0xc0000005u;
    long rate = sysconf(_SC_CLK_TCK);
    if (rate <= 0) return 0xc0000001u;
    natural_t count = 0;
    processor_info_array_t data = NULL;
    mach_msg_type_number_t words = 0;
    mach_port_t host = mach_host_self();
    kern_return_t status = host_processor_info(host, PROCESSOR_CPU_LOAD_INFO,
                                               &count, &data, &words);
    kern_return_t released = mach_port_deallocate(mach_task_self(), host);
    if (released != KERN_SUCCESS) {
        fputs("TribesPad: processor host port release failed\n", stderr); abort();
    }
    if (status != KERN_SUCCESS) return 0xc0000001u;
    if (!count || !data || (uint64_t)count * CPU_STATE_MAX > words) {
        if (data) release_snapshot(data, words);
        return 0xc0000001u;
    }
    uint32_t exposed = count > 32 ? 32 : count;
    if (exposed > capacity / 48) exposed = capacity / 48;
    uint32_t length = exposed * 48;
    if (!range(base, output, length)) { release_snapshot(data, words); return 0xc0000005u; }
    uint64_t records[32][6] = {{0}};
    for (uint32_t i = 0; i < exposed; ++i) {
        const natural_t *ticks = (const natural_t *)data + i * CPU_STATE_MAX;
        uint64_t idle = ticks[CPU_STATE_IDLE], system = ticks[CPU_STATE_SYSTEM];
        uint64_t user = (uint64_t)ticks[CPU_STATE_USER] + ticks[CPU_STATE_NICE];
        records[i][0] = idle * UINT64_C(10000000) / (uint64_t)rate;
        records[i][1] = (system + idle) * UINT64_C(10000000) / (uint64_t)rate;
        records[i][2] = user * UINT64_C(10000000) / (uint64_t)rate;
    }
    release_snapshot(data, words);
    memcpy((void *)(uintptr_t)(base + output), records, length);
    if (return_length) memcpy((void *)(uintptr_t)(base + return_length), &length, 4);
    return 0;
}
