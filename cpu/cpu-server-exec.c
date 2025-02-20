#include "list.h"
#include <driver_types.h>
#define _GNU_SOURCE
#include <cuda_runtime_api.h>
#include <cuda.h>
#include <stdint.h>
#include <time.h>
#include "cpu-server-exec.h"
#include "api-recorder.h"
#include "cpu_rpc_prot.h"
#include "timestamp.h"


static uint64_t cur_ts = 0;
static uint64_t start_ts = 0;
static uint64_t real_start_ts = 0;
list active_stream_records;
list synced_stream_records; 
list active_event_records;
list synced_event_records; 

uint64_t lookup_synctime(ptr id, list *records){
    for(size_t i = 0; i < records->length; i++){
        sync_record_t *record;
        if (list_at(records, i, (void**)&record) != 0) {
            LOGE(LOG_ERROR, "list_at %zu returned an error.", i);
            continue;
        }
        if(record->fake_id == id){
            LOGE(LOG_DEBUG, "lookup_synctime found the record with fake id %p. ts:%lu", id, record->synced_timestamp);
            return record->synced_timestamp;
        }
    }
    LOGE(LOG_ERROR, "lookup_synctime failed to find the record with fake id %p.", id);
    return 0;
}

int remove_record(ptr id, list* records){
    for(size_t i = 0; i < records->length; i++){
        sync_record_t *record;
        if (list_at(records, i, (void**)&record) != 0) {
            LOGE(LOG_ERROR, "list_at %zu returned an error.", i);
            continue;
        }
        if(record->fake_id == id){
            list_rm(records, i);
            return 0;
        }
    }
    return 1;
}

int preprocess_before_serialize(uint64_t timestamp){

    // forget about all old events because they are all synced at this point
    // assume every stream is active until they are destroyed
    // move from synced to active stream records
    while(synced_stream_records.length > 0){
        sync_record_t *record;
        if (list_at(&synced_stream_records, 0, (void**)&record) != 0) {
            LOGE(LOG_ERROR, "list_at returned an error.");
            continue;
        }
        if (list_append_copy(&active_stream_records, (void*)record) != 0) {
            LOGE(LOG_ERROR, "list allocation failed.");
        }
        list_rm(&synced_stream_records, 0);
    }

    for(size_t i = 0; i < nex_api_records.length; i++){
        api_record_t *record;
        if (list_at(&nex_api_records, i, (void**)&record) != 0) {
            LOGE(LOG_ERROR, "list_at %zu returned an error.", i);
            continue;
        }
        LOGE(LOG_DEBUG, "preprocess_before_serialize func: %d, total: %lu", record->function, nex_api_records.length);
        if(record->ts > timestamp){
            break;
        }

        // capture new stream or event records
        switch (record->function){
            case CUDA_STREAM_CREATE:
                //TODO: should get the stream id properly
                {
                    NEX_RECORD_STREAM(record->result.ptr_result_u.ptr_result_u.ptr);
                }
                break;
            case CUDA_STREAM_CREATE_WITH_FLAGS:
                {
                    NEX_RECORD_STREAM(record->result.ptr_result_u.ptr_result_u.ptr);
                }
                break;
            case CUDA_STREAM_CREATE_WITH_PRIORITY:
                {
                    NEX_RECORD_STREAM(record->result.ptr_result_u.ptr_result_u.ptr);
                }
                break;
            case CUDA_EVENT_CREATE:
                {  
                    LOGE(LOG_DEBUG, "sync records CUDA_EVENT_CREATE %p", record->result.ptr_result_u.ptr_result_u.ptr);
                    NEX_RECORD_EVENT(record->result.ptr_result_u.ptr_result_u.ptr);
                }
                break;
            case CUDA_EVENT_CREATE_WITH_FLAGS:
                {
                    NEX_RECORD_EVENT(record->result.ptr_result_u.ptr_result_u.ptr);
                }
                break;
            case CUDA_EVENT_DESTROY:
                {
                    if(record->exe_status == 1){
                        ptr event = *(ptr*)(record->arguments);
                        // remove the stream from active and sync
                        remove_record(event, &active_event_records);
                        remove_record(event, &synced_event_records);
                    }
                }
            case CUDA_STREAM_DESTROY:
                {
                    if(record->exe_status == 1){
                        ptr stream = *(ptr*)(record->arguments);
                        // remove the stream from active and sync
                        remove_record(stream, &active_stream_records);
                        remove_record(stream, &synced_stream_records);
                    }
                }
                break;
            default:
                // ignore
                break;
        }

        // if executed, instead of recorded, remove it
        // if(record->exe_status == 1){
        //     list_rm(&nex_api_records, i);
        //     i--;
        // }
    }

}

int cal_finish_time_of_sync_call(uint64_t timestamp){
    for(int i = 0; i < nex_api_records.length; i++){
        api_record_t *record;
        if(list_at(&nex_api_records, i, (void**)&record) != 0){
            LOGE(LOG_ERROR, "list_at %d returned an error.", i);
            continue;
        }
        
        if(record->exe_status == 0){
            continue;
        }

        switch(record->function){
            case CUDA_EVENT_QUERY:
            {
                ptr fake_event = *(ptr*)(record->arguments);
                record->ts = lookup_synctime(fake_event, &synced_event_records);
                if(record->ts <= timestamp){
                    record->result.integer = 0;
                }else{
                    record->result.integer = 1;
                }
                record->ts = timestamp;
            }
                break;
            case CUDA_STREAM_QUERY:
            {
                ptr fake_stream = *(ptr*)(record->arguments);
                record->ts = lookup_synctime(fake_stream, &synced_stream_records);
                if(record->ts <= timestamp){
                    record->result.integer = 0;
                }else{
                    record->result.integer = 1;
                }
                record->ts = timestamp;
            }
                break;
            case CUDA_STREAM_SYNCHRONIZE:
            {
                ptr fake_stream = *(ptr*)(record->arguments);
                record->ts = lookup_synctime(fake_stream, &synced_stream_records);
                if(record->ts < timestamp){
                    record->ts = timestamp;
                }
                record->result.integer = 0;
            }
                break;
            case CUDA_EVENT_SYNCHRONIZE:
            {
                ptr fake_event = *(ptr*)(record->arguments);
                record->ts = lookup_synctime(fake_event, &synced_event_records);
                if(record->ts < timestamp){
                    record->ts = timestamp;
                }
                LOGE(LOG_DEBUG, "cal finishing time of %p, record->ts %lu, record %p \n", fake_event, record->ts, record);
                record->result.integer = 0;
            }
                break;
            default:
                break;
        }
    }
    return 0;
}

// this is a ugly way to handle synchronizing different streams or events
int sync_all_actions()
{
    for(size_t i = 0; i < active_event_records.length; i++){
        sync_record_t *record;
        if (list_at(&active_event_records, i, (void**)&record) != 0) {
            LOGE(LOG_ERROR, "list_at %zu returned an error.", i);
            continue;
        }
        record->real_id = (ptr)resource_mg_get(&rm_events, (void*)record->fake_id);
    }

    for(size_t i = 0; i < active_stream_records.length; i++){
        sync_record_t *record;
        if (list_at(&active_stream_records, i, (void**)&record) != 0) {
            LOGE(LOG_ERROR, "list_at %zu returned an error.", i);
            continue;
        }
        record->real_id = (ptr)resource_mg_get(&rm_streams, (void*)record->fake_id);
    }

    while(active_stream_records.length > 0 || active_event_records.length > 0){
        int any = 0;

        for(size_t i = 0; i < active_event_records.length; i++){
            sync_record_t *record;
            if (list_at(&active_event_records, i, (void**)&record) != 0) {
                LOGE(LOG_ERROR, "list_at %zu returned an error.", i);
                continue;
            }
            // avoid resource get here
            if(cudaSuccess == cudaEventQuery((void*)(record->real_id))){
                cur_ts = start_ts + timestamp_now() - real_start_ts;
                LOGE(LOG_DEBUG, "Event query success fake_id %p, event_id %p, ts %lu", record->fake_id, record->real_id, cur_ts);
                record->synced_timestamp = cur_ts;
                any = 1;
                // rm this stream to synced
                if (list_append_copy(&synced_event_records, (void*)record) != 0) {
                    LOGE(LOG_ERROR, "list allocation failed.");
                }
                list_rm(&active_event_records, i);
                i--;
            }
        }

        for(size_t i = 0; i < active_stream_records.length; i++){
            sync_record_t *record;
            if (list_at(&active_stream_records, i, (void**)&record) != 0) {
                LOGE(LOG_ERROR, "list_at %zu returned an error.", i);
                continue;
            }
            // avoid resouce get here
            if(cudaSuccess == cudaStreamQuery((void*)record->real_id)){
                cur_ts = start_ts + timestamp_now() - real_start_ts;
                record->synced_timestamp = cur_ts;
                any = 1;
                // rm this stream to synced
                if (list_append_copy(&synced_stream_records, (void*)record) != 0) {
                    LOGE(LOG_ERROR, "list allocation failed.");
                }
                list_rm(&active_stream_records, i);
                i--;
            }
        }

        if(any == 0){
            // avoid busy waiting
            usleep(5);
        }
        
    }
    return 0;
}
/*
Assume for now we only handle CUDA_LAUNCH_KERNEL and Synchronization of such calls
MemCpy MemCreate etc are serialized inline, not recorded
*/
int serialize_all_till_now(uint64_t timestamp)
{
    preprocess_before_serialize(timestamp);

    cur_ts = 0;
    start_ts = 0;
    real_start_ts = 0;
    LOGE(LOG_DEBUG, "Serializing all calls till %lu; recorded %d", timestamp, nex_api_records.length);
    for(size_t i = 0; i < nex_api_records.length; i++){
        api_record_t *record;
        if (list_at(&nex_api_records, i, (void**)&record) != 0) {
            LOGE(LOG_ERROR, "list_at %zu returned an error.", i);
            continue;
        }
        if(record->exe_status == 1){
            continue;
        }
        LOGE(LOG_DEBUG, "Serializing %d ts:%lu", record->function, record->ts);
        if(record->ts > timestamp){
            // stop 
            LOGE(LOG_DEBUG, "Reached the timestamp %lu", timestamp);
            break;
        }
        if(cur_ts == 0){
            cur_ts = record->ts;
            start_ts = cur_ts;
            real_start_ts = timestamp_now();
        }else{
            //wait some realtime until the next timestamp is reached
            uint64_t dur = record->ts - cur_ts;
            if(record->ts > cur_ts){
                struct timespec req;
                ns_to_time_spec(dur, &req);
                clock_nanosleep(CLOCK_MONOTONIC, 0, &req, NULL);
                cur_ts = record->ts;
            }else{
                // actually smaller, means there is a bug in the timestamp
                // but since we don't measure time, we actually don't know how long to wait for each API
                // fine, skip for now
                record->ts = cur_ts;
            }
        }

        // serialize this call
        // LOGE(LOG_DEBUG, "Realtime just for preparing launch API call (us) %lu", real_ts/1000);
        switch (record->function){
            int ret;
            case CUDA_STREAM_SYNCHRONIZE:
            case CUDA_EVENT_SYNCHRONIZE:
                break;

            case CUDA_STREAM_WAIT_EVENT:
                LOGE(LOG_DEBUG, "(Serialize) cudaStreamWaitEvent @ %lu", cur_ts);
                ret = exe_cuda_stream_wait_event_1(record);
                if(ret != 0){
                    LOGE(LOG_ERROR, "Error in (Serialize) cudaStreamWaitEvent");
                }
                break;
            
            case CUDA_EVENT_CREATE:
                LOGE(LOG_DEBUG, "(Serialize) cudaEventCreate @ %lu", cur_ts);
                ret = exe_cuda_event_create_1(record);
                if(ret != 0){
                    LOGE(LOG_ERROR, "Error in (Serialize) cudaEventCreate");
                }
                break;
            case CUDA_EVENT_CREATE_WITH_FLAGS:
                LOGE(LOG_DEBUG, "(Serialize) cudaEventCreateWithFlags @ %lu", cur_ts);
                ret = exe_cuda_event_create_with_flags_1(record);
                if(ret != 0){
                    LOGE(LOG_ERROR, "Error in (Serialize) cudaEventCreateWithFlags");
                }
                break;
            case CUDA_EVENT_DESTROY:
                LOGE(LOG_DEBUG, "(Serialize) cudaEventDestroy @ %lu", cur_ts);
                ret = exe_cuda_event_destroy_1(record);
                if(ret != 0){
                    LOGE(LOG_ERROR, "Error in (Serialize) cudaEventDestroy");
                }
                break;
            case CUDA_EVENT_QUERY:
                LOGE(LOG_DEBUG, "(Serialize) cudaEventQuery @ %lu", cur_ts);
                ret = exe_cuda_event_query_1(record);
                if(ret != 0){
                    LOGE(LOG_ERROR, "Error in (Serialize) cudaEventQuery");
                }
                break;
            case CUDA_EVENT_RECORD:
                LOGE(LOG_DEBUG, "(Serialize) cudaEventRecord @ %lu", cur_ts);
                ret = exe_cuda_event_record_1(record);
                if(ret != 0){
                    LOGE(LOG_ERROR, "Error in (Serialize) cudaEventRecord");
                }
                break;
            case CUDA_EVENT_RECORD_WITH_FLAGS:
                LOGE(LOG_DEBUG, "(Serialize) cudaEventRecordWithFlags @ %lu", cur_ts);
                ret = exe_cuda_event_record_with_flags_1(record);
                if(ret != 0){
                    LOGE(LOG_ERROR, "Error in (Serialize) cudaEventRecordWithFlags");
                }
                break;
            case CUDA_LAUNCH_KERNEL:
                LOGE(LOG_DEBUG, "(Serialize) cudaLaunchKernel @ %lu", cur_ts);
                ret = exe_cuda_launch_kernel_1(record);
                if(ret != 0){
                    LOGE(LOG_ERROR, "Error in (Serialize) cudaLaunchKernel");
                }
                break;
            default:
                LOGE(LOG_ERROR, "Unsupported function %d", record->function);
                break;
        }

        // cur_ts = record->ts;
        cur_ts = start_ts + timestamp_now() - real_start_ts;
        record->ts = cur_ts;
        record->exe_status = 1;
        LOGE(LOG_DEBUG, "Serialized %d, ts %lu", record->function, cur_ts);
    }

    sync_all_actions();
    
    // Now the process has all finished, now enter comfort zoom of doing whatever we want without affecting the simulated time
    
    cal_finish_time_of_sync_call(timestamp);
    LOGE(LOG_DEBUG, "Length of active, sync stream: %d, %d, and for event: %d, %d", active_stream_records.length, synced_stream_records.length, active_event_records.length, synced_event_records.length);
}

int rm_executed_api(){
     // remove all records that are already processed
    for(size_t i = 0; i < nex_api_records.length; i++){
        api_record_t *record;
        if (list_at(&nex_api_records, i, (void**)&record) != 0) {
            LOGE(LOG_ERROR, "list_at %zu returned an error.", i);
            continue;
        }
        if(record->exe_status == 1){
            list_rm(&nex_api_records, i);
            i--;
        }
    }
    return 0;
}


void sync_records_free(){
    list_free(&synced_stream_records);
    list_free(&active_stream_records);
    list_free(&synced_event_records);
    list_free(&active_event_records);
}

void nex_records_empty(){
    nex_api_records.length = 0;
}

void sync_records_empty(){
    synced_stream_records.length = 0;
    active_stream_records.length = 0;
    synced_event_records.length = 0;
    active_event_records.length = 0;
}


// case CUDA_STREAM_SYNCHRONIZE:
//             LOGE(LOG_DEBUG, "(Serialize) cudaStreamSynchronize @ %lu", cur_ts);
//             ret = exe_cuda_stream_synchronize_1(record);
//             if(ret != 0){
//                 LOGE(LOG_ERROR, "Error in (Serialize) cudaStreamSynchronize");
//             }
//             break;
// case CUDA_EVENT_SYNCHRONIZE:
//     LOGE(LOG_DEBUG, "(Serialize) cudaEventSynchronize @ %lu", cur_ts);
//     ret = exe_cuda_event_synchronize_1(record);
//     if(ret != 0){
//         LOGE(LOG_ERROR, "Error in (Serialize) cudaEventSynchronize");
//     }
//     break;
