#pragma  once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "api-recorder.h"
#include "resource-mg.h"
#include "log.h"

void process_database_records();

void deinit_server_exec();
// don't delete record in the serialize_all_till_now function
// because the list implementation is not a linked list
int serialize_all_till_now(uint64_t timestamp);

// remove the record here
int rm_executed_api(void);

int exe_cuda_stream_synchronize_1(api_record_t* record);
int exe_cuda_stream_wait_event_1(api_record_t* record);
int exe_cuda_event_create_1(api_record_t* record);
int exe_cuda_event_create_with_flags_1(api_record_t* record);
int exe_cuda_event_destroy_1(api_record_t* record);
int exe_cuda_event_query_1(api_record_t* record);
int exe_cuda_event_record_1(api_record_t* record);
int exe_cuda_event_record_with_flags_1(api_record_t* record);
int exe_cuda_event_synchronize_1(api_record_t* record);
int exe_cuda_launch_kernel_1(api_record_t* record);

typedef struct sync_record_t {
    ptr real_id;
    ptr fake_id;
    uint64_t synced_timestamp; // timestamp of the last sync
} sync_record_t;

extern list active_stream_records;
extern list synced_stream_records; 
extern list active_event_records;
extern list synced_event_records; 


// remember to init the lists 

#define NEX_RECORD_STREAM(s_id)  \
    sync_record_t *trace_record; \
    if (list_append(&active_stream_records, (void**)&trace_record) != 0) { \
        LOGE(LOG_ERROR, "list allocation failed."); \
    } \
    trace_record->fake_id = s_id; \
    trace_record->real_id = 0;


#define NEX_RECORD_EVENT(e_id)  \
    sync_record_t *trace_record; \
    if (list_append(&active_event_records, (void**)&trace_record) != 0) { \
        LOGE(LOG_ERROR, "list allocation failed."); \
    } \
    trace_record->fake_id = e_id; \
    trace_record->real_id = 0;

void sync_records_free(void);
void sync_records_empty(void);
void nex_records_empty(void);


// for constructing profiling database
typedef struct {
    uint64_t metricId;
    const char* metricName;
    double sumValue;      // Sum of per-instruction average values for this metric
    size_t appliedCount;  // Number of instructions where this metric was observed
    double ratio;
    double averageValue;
} metric_feature_t;

int init_metric_database(const char*, size_t);
int dump_metrics_to_database(metric_feature_t*, size_t, uint64_t);
int deinit_metric_database(void);

