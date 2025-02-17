#pragma once
#include <stdint.h>
#include <time.h>
#include "cpu_rpc_prot.h"

void* create_timestamp(void);
uint64_t get_ns_duration(void* start, void* end);
void ns_to_time_spec(uint64_t, struct timespec*);
void wait_until(uint64_t);
timestamp timestamp_now();

static uint64_t* nex_ctrl_addr = 0;
void nex_ctrl_init();
void nex_enter_simulation(); 
void nex_exit_simulation();
