#pragma once
#include <stdint.h>
#include <time.h>
#include "cpu_rpc_prot.h"

void* create_timestamp(void);
uint64_t get_ns_duration(void* start, void* end);
void ns_to_time_spec(uint64_t, struct timespec*);
void wait_until(uint64_t);
timestamp timestamp_now();
