#include <stdint.h>
#include <time.h>
#include "cpu-server-exec.h"
#include "cpu_rpc_prot.h"

/*
Assume for now we only handle CUDA_LAUNCH_KERNEL and Synchronization of such calls
MemCpy MemCreate etc are serialized inline, not recorded
*/
int serialize_all_till_now(uint64_t timestamp){
    uint64_t cur_ts = 0;
    for(size_t i = 0; i < nex_api_records.length; i++){
        api_record_t *record;
        if (list_at(&api_records, i, (void**)&record) != 0) {
            LOGE(LOG_ERROR, "list_at %zu returned an error.", i);
            continue;
        }
        if(record->ts > timestamp){
            // stop 
            break;
        }
        if(cur_ts == 0){
            cur_ts = record->ts;
        }else{
            //wait some realtime until the next timestamp is reached
            uint64_t dur = record->ts - cur_ts;
            if(record->ts > cur_ts){
                struct timespec req;
                ns_to_time_spec(dur, &req);
                clock_nanosleep(CLOCK_MONOTONIC, 0, &req, NULL);
            }
            cur_ts = record->ts;
        }

        // serialize this call
        switch (record->function){
            case CUDA_STREAM_SYNCHRONIZE:
                exe_cuda_stream_synchronize_1(record);
                break;
            case CUDA_STREAM_WAIT_EVENT:
                exe_cuda_stream_wait_event_1(record);
                break;
             case CUDA_EVENT_CREATE:
                exe_cuda_event_create_1(record);
                break;
            case CUDA_EVENT_CREATE_WITH_FLAGS:
                exe_cuda_event_create_with_flags_1(record);
                break;
            case CUDA_EVENT_DESTROY:
                exe_cuda_event_destroy_1(record);
                break;
            case CUDA_EVENT_QUERY:
                exe_cuda_event_query_1(record);
                break;
            case CUDA_EVENT_RECORD:
                exe_cuda_event_record_1(record);
                break;
            case CUDA_EVENT_RECORD_WITH_FLAGS:
                exe_cuda_event_record_with_flags_1(record);
                break;
            case CUDA_EVENT_SYNCHRONIZE:
                exe_cuda_event_synchronize_1(record);
                break;
            case CUDA_LAUNCH_KERNEL:
                exe_cuda_launch_kernel_1(record);
                break;
            default:
                LOGE(LOG_ERROR, "Unsupported function %d", record->function);
                break;
        }

        cur_ts = record->ts;
    }
}