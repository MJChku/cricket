#include <stdint.h>
#include <time.h>
#include "cpu-server-exec.h"
#include "api-recorder.h"
#include "cpu_rpc_prot.h"
#include "timestamp.h"

/*
Assume for now we only handle CUDA_LAUNCH_KERNEL and Synchronization of such calls
MemCpy MemCreate etc are serialized inline, not recorded
*/
int serialize_all_till_now(uint64_t timestamp){
    uint64_t cur_ts = 0;
    uint64_t start_ts = 0;
    uint64_t real_start_ts = 0;
    LOGE(LOG_DEBUG, "Serializing all calls till %lu; recorded %d", timestamp, nex_api_records.length);
    for(size_t i = 0; i < nex_api_records.length; i++){
        api_record_t *record;
        if (list_at(&nex_api_records, i, (void**)&record) != 0) {
            LOGE(LOG_ERROR, "list_at %zu returned an error.", i);
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
                LOGE(LOG_DEBUG, "(Serialize) cudaStreamSynchronize @ %lu", cur_ts);
                ret = exe_cuda_stream_synchronize_1(record);
                if(ret != 0){
                    LOGE(LOG_ERROR, "Error in (Serialize) cudaStreamSynchronize");
                }
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
            case CUDA_EVENT_SYNCHRONIZE:
                LOGE(LOG_DEBUG, "(Serialize) cudaEventSynchronize @ %lu", cur_ts);
                ret = exe_cuda_event_synchronize_1(record);
                if(ret != 0){
                    LOGE(LOG_ERROR, "Error in (Serialize) cudaEventSynchronize");
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
        LOGE(LOG_DEBUG, "Serialized %d, ts %lu", record->function, cur_ts);
    }

    while(nex_api_records.length > 0){
        list_rm(&nex_api_records, 0);
    }
    LOGE(LOG_DEBUG, "nex_api_recods length %d", nex_api_records.length);
}