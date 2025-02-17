#include <stdlib.h>
#include <stdio.h>
#include "timestamp.h"
#include "log.h"

void nex_ctrl_init(){
    char* mmio_base_str = getenv("ACCVM_MMIO_BASE");
    printf("str: %s\n", mmio_base_str);
    uintptr_t mmio_base = (uintptr_t)strtoul(mmio_base_str, NULL, 0);
    // Turn on epoch scheduling
    *(uint64_t*)(mmio_base+3*4096) = 0x1000;
    nex_ctrl_addr = (uint64_t*)(mmio_base + 3*4096);
}

void nex_enter_simulation(){
    // stop the simulation and jail break the current thread
    *nex_ctrl_addr = 0x5000;
}

void nex_exit_simulation(){
    // continue the simulation and remove the jail break
    *nex_ctrl_addr = 0x6000;
}


void* create_timestamp(void){
    struct timespec *ts = malloc(sizeof(struct timespec));
    if (ts == NULL) {
        LOGE(LOG_ERROR, "malloc timespec failed.");
        return NULL;
    }
    clock_gettime(CLOCK_MONOTONIC, ts);
    return ts;
}

uint64_t get_ns_duration(void* start, void* end){
    struct timespec *start_ts = (struct timespec*)start;
    struct timespec *end_ts = (struct timespec*)end;
    uint64_t start_ns = start_ts->tv_sec * 1000000000 + start_ts->tv_nsec;
    uint64_t end_ns = end_ts->tv_sec * 1000000000 + end_ts->tv_nsec;
    return end_ns - start_ns;
}

void ns_to_time_spec(uint64_t ns, struct timespec *ts){
    ts->tv_sec = ns / 1000000000;
    ts->tv_nsec = ns % 1000000000;
}

void wait_until(uint64_t ns){
    while(timestamp_now() < ns){
        // busy wait
    }
    printf("wait_until done to us %lu\n", ns/1000);
}

timestamp timestamp_now(){
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000000000 + ts.tv_nsec;
}