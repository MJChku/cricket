#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>

#include "api-recorder.h"
#include "log.h"
#include "list.h"

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

ptr virtual_client_addr_gen(void){
    return virtual_ID++;
}

list api_records;

list nex_api_records;

static void api_records_free_args(void)
{
    api_record_t *record;
    for (size_t i = 0; i < api_records.length; i++) {
        if (list_at(&api_records, i, (void**)&record) != 0) {
            LOGE(LOG_ERROR, "list_at %zu returned an error.", i);
            continue;
        }
        free(record->arguments);
        record->arguments = NULL;
    }

}

static void api_records_free_data(void)
{
    api_record_t *record;
    for (size_t i = 0; i < api_records.length; i++) {
        if (list_at(&api_records, i, (void**)&record) != 0) {
            LOGE(LOG_ERROR, "list_at %zu returned an error.", i);
            continue;
        }
        free(record->data);
        record->data = NULL;
    }
}


void api_records_free(void)
{
    api_records_free_args();
    api_records_free_data();
    list_free(&api_records);
}

size_t api_records_malloc_get_size(void *ptr)
{
    api_record_t *record;
    for (size_t i = 0; i < api_records.length; i++) {
        if (list_at(&api_records, i, (void**)&record) != 0) {
            LOGE(LOG_ERROR, "list_at returned an error.");
        }
        if (record->function != CUDA_MALLOC) {
            continue;
        }
        if (ptr == (void*)record->result.ptr_result_u.ptr_result_u.ptr) {
            return *(size_t*)record->arguments;
        }
    }
    return 0;
}

void api_records_print_records(api_record_t *record)
{
    char str[128];
    sprintf(str, "function: %u ", record->function);
    switch (record->function) {
    case CUDA_MALLOC:
        sprintf(str+strlen(str), "(cuda_malloc), arg=%zu, result=%lx", *(size_t*)record->arguments, record->result.ptr_result_u.ptr_result_u.ptr);
        break;
    case CUDA_SET_DEVICE:
        sprintf(str+strlen(str), "(cuda_set_device)");
        break;
    case CUDA_EVENT_CREATE:
        sprintf(str+strlen(str), "(cuda_event_create)");
        break;
    case CUDA_MEMCPY_HTOD:
        sprintf(str+strlen(str), "(cuda_memcpy_htod)");
        break;
    case CUDA_EVENT_RECORD:
        sprintf(str+strlen(str), "(cuda_event_record)");
        break;
    case CUDA_EVENT_DESTROY:
        sprintf(str+strlen(str), "(cuda_event_destroy)");
        break;
    case CUDA_STREAM_CREATE_WITH_FLAGS:
        sprintf(str+strlen(str), "(cuda_stream_create_with_flags)");
        break;
    }
    LOG(LOG_DEBUG, "%s", str);
}

void api_records_print(void)
{
    api_record_t *record;
    printf("server api records:\n");
    for (size_t i = 0; i < api_records.length; i++) {
        if (list_at(&api_records, i, (void**)&record) != 0) {
            LOGE(LOG_ERROR, "list_at returned an error.");
        }
        api_records_print_records(record);
    }

}

