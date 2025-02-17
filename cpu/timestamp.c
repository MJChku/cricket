#include "log.h"
#include <stdlib.h>
#include "timestamp.h"

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
}

timestamp timestamp_now(){
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000000000 + ts.tv_nsec;
}