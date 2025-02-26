#include "log.h"
#define _GNU_SOURCE
#include "list.h"
#include <cuda_runtime.h>
#include <driver_types.h>
#include <cuda_runtime_api.h>
#include <cuda.h>
#include <stdint.h>
#include <time.h>
#include "cpu-server-exec.h"
#include "api-recorder.h"
#include "cpu_rpc_prot.h"
#include "timestamp.h"
#include <cupti.h>
#include <cupti_profiler_target.h>
#include <nvperf_host.h>
#include <nvperf_target.h>
#include <cupti_target.h>
#include <cupti_sass_metrics.h>


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
            if(record->function == CUDA_LAUNCH_KERNEL){
                record->exe_status = 0;
                list_append_copy(&database_records, (void*)record);
            }
            list_rm(&nex_api_records, i);
            i--;
        }
    }

    process_database_records();

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

#define CHECK_CU(err) \
    if (err != CUDA_SUCCESS) { \
        LOGE(LOG_DEBUG, "CUDA Error: %d at %s:%d\n", err, __FILE__, __LINE__); \
        exit(-1); \
    }

#define CHECK_CUPTI(err) \
    if (err != CUPTI_SUCCESS) { \
        LOGE(LOG_DEBUG, "CUPTI Error: %d at %s:%d\n", err, __FILE__, __LINE__); \
        exit(-1); \
    }


static CUpti_SassMetrics_MetricDetails* supportedMetrics;
static size_t numOfMetrics=0;
static size_t deviceIndex=0;
static uint8_t outputGranularity=CUPTI_SASS_METRICS_OUTPUT_GRANULARITY_GPU;
static int supported_metric_initialized = 0;

const char* search_metric_name(CUpti_SassMetrics_MetricDetails* _supportedMetrics, size_t _numOfMetrics, uint64_t metricId){
    LOGE(LOG_DEBUG, "Searching for metric name for %p, total %d, id %lu", _supportedMetrics, _numOfMetrics, metricId );
    for(size_t i = 0; i < _numOfMetrics; i++){
        if(_supportedMetrics[i].metricId == metricId){
            return _supportedMetrics[i].pMetricName;
        }
    }
    return NULL;
}

void init_sass_metrics(){
    if(supported_metric_initialized == 1){
        return;
    }

    CUpti_Profiler_Initialize_Params profilerParams = {CUpti_Profiler_Initialize_Params_STRUCT_SIZE};
    profilerParams.pPriv = NULL;
    CHECK_CUPTI(cuptiProfilerInitialize(&profilerParams));
    
    CUpti_Device_GetChipName_Params getChipParams;
    memset(&getChipParams, 0, sizeof(getChipParams));
    getChipParams.structSize = CUpti_Device_GetChipName_Params_STRUCT_SIZE;
    CHECK_CUPTI(cuptiDeviceGetChipName(&getChipParams));

    // Get the number of available SASS metrics
    CUpti_SassMetrics_GetNumOfMetrics_Params getNumOfMetricParams;
    memset(&getNumOfMetricParams, 0, sizeof(getNumOfMetricParams));
    getNumOfMetricParams.pChipName = getChipParams.pChipName;
    getNumOfMetricParams.structSize = CUpti_SassMetrics_GetNumOfMetrics_Params_STRUCT_SIZE;
    CHECK_CUPTI(cuptiSassMetricsGetNumOfMetrics(&getNumOfMetricParams));

    numOfMetrics = getNumOfMetricParams.numOfMetrics;

    // initalize database
    init_metric_database(getChipParams.pChipName, numOfMetrics);

    supportedMetrics = 
        (CUpti_SassMetrics_MetricDetails*) malloc(getNumOfMetricParams.numOfMetrics * sizeof(CUpti_SassMetrics_MetricDetails));
    memset(supportedMetrics, 0, getNumOfMetricParams.numOfMetrics * sizeof(CUpti_SassMetrics_MetricDetails));
    if (!supportedMetrics) {
        LOGE(LOG_ERROR, "supportedMetrics Memory allocation failed!");
        return;
    }

    CUpti_SassMetrics_GetMetrics_Params getMetricsParams;
    memset(&getMetricsParams, 0, sizeof(getMetricsParams));
    getMetricsParams.structSize = CUpti_SassMetrics_GetMetrics_Params_STRUCT_SIZE;
    getMetricsParams.pChipName = getChipParams.pChipName;
    getMetricsParams.pMetricsList = supportedMetrics;
    getMetricsParams.numOfMetrics = getNumOfMetricParams.numOfMetrics;
    CHECK_CUPTI(cuptiSassMetricsGetMetrics(&getMetricsParams));

    // Print the available metrics
    LOGE(LOG_DEBUG, "Number of available SASS metrics: %lu\n", getMetricsParams.numOfMetrics);
    for (size_t i = 0; i < getMetricsParams.numOfMetrics; i++) {
        LOGE(LOG_DEBUG, "Metric Name: %s, %s\n",
               supportedMetrics[i].pMetricName,
            //    supportedMetrics[i].metricId,
               supportedMetrics[i].pMetricDescription);
    }
    
    
    CUpti_SassMetricsSetConfig_Params sassMetricsSetConfigParams;
    memset(&sassMetricsSetConfigParams, 0, sizeof(sassMetricsSetConfigParams));
    sassMetricsSetConfigParams.structSize = CUpti_SassMetricsSetConfig_Params_STRUCT_SIZE;
    sassMetricsSetConfigParams.deviceIndex = deviceIndex;
    sassMetricsSetConfigParams.numOfMetricConfig = numOfMetrics;

    CUpti_SassMetrics_Config* metricConfigs = (CUpti_SassMetrics_Config*) malloc(sassMetricsSetConfigParams.numOfMetricConfig * sizeof(CUpti_SassMetrics_Config));
    memset(metricConfigs, 0, sassMetricsSetConfigParams.numOfMetricConfig * sizeof(CUpti_SassMetrics_Config));
    for(size_t index = 0; index < sassMetricsSetConfigParams.numOfMetricConfig; index++){
        metricConfigs[index].metricId = supportedMetrics[index].metricId;
        metricConfigs[index].outputGranularity = outputGranularity;
        LOGE(LOG_DEBUG, "SetConfig for Metric Name: %s\n",
               supportedMetrics[index].pMetricName);
    }

    sassMetricsSetConfigParams.pConfigs = metricConfigs;
    CHECK_CUPTI(cuptiSassMetricsSetConfig(&sassMetricsSetConfigParams));

    supported_metric_initialized = 1;
    return;
}

void deinit_sass_metrics(){

    LOGE(LOG_DEBUG, "Deinit SASS Metrics");
    if(supported_metric_initialized == 0){
        return;
    }
    
    deinit_metric_database();

    // Unset the CUPTI SASS Metrics configuration
    CUpti_SassMetricsUnsetConfig_Params unsetConfigParams;
    memset(&unsetConfigParams, 0, sizeof(unsetConfigParams));
    unsetConfigParams.structSize = CUpti_SassMetricsUnsetConfig_Params_STRUCT_SIZE;
    unsetConfigParams.deviceIndex = deviceIndex;
    CHECK_CUPTI(cuptiSassMetricsUnsetConfig(&unsetConfigParams));
    // Free allocated memory
    free(supportedMetrics);
}

metric_feature_t* assemble_metrics(CUpti_SassMetricsFlushData_Params* flushDataParams, CUpti_SassMetricsGetDataProperties_Params* getDataPropParams){

    // compute features based on the kernels:
    metric_feature_t* features = (metric_feature_t*)malloc(numOfMetrics * sizeof(metric_feature_t));
    if (!features) {
        LOGE(LOG_ERROR, "Failed to allocate features array!");
    }

    for(size_t m = 0; m < numOfMetrics; m++){
        features[m].metricId   = supportedMetrics[m].metricId;
        features[m].metricName = supportedMetrics[m].pMetricName;
        features[m].sumValue   = 0.0;
        features[m].appliedCount = 0;
    }

    // Iterate over each patched instruction record
    for (size_t recordIndex = 0; recordIndex < getDataPropParams->numOfPatchedInstructionRecords; recordIndex++) {
        CUpti_SassMetrics_Data* pMetricsData = &(flushDataParams->pMetricsData[recordIndex]);
        
        // Loop over all instance slots for this instruction record.
        for (size_t instanceIndex = 0; instanceIndex < getDataPropParams->numOfInstances; instanceIndex++) {
            CUpti_SassMetrics_InstanceValue* pInstanceValue = &(pMetricsData->pInstanceValues[instanceIndex]);
            // If this instance corresponds to metric m, accumulate its value.
            int pos = numOfMetrics;
            for(int m = 0; m < numOfMetrics; m++){
                if(pInstanceValue->metricId == features[m].metricId){
                    pos = m;
                    break;
                }
            }
            
            if(pos == numOfMetrics){
                LOGE(LOG_ERROR, "Metric ID %lu not found in supported metrics", pInstanceValue->metricId);
                return NULL;
            }

            features[pos].appliedCount++;
            features[pos].sumValue += (double)pInstanceValue->value;
        }
    }

    // Compute and log the features for each metric.
    // The ratio is the fraction of patched instructions that reported this metric.
    // The average value is the mean of per-instruction averages (over those instructions where it applies).
    for (size_t m = 0; m < numOfMetrics; m++) {
        double ratio = (double) features[m].appliedCount / (double)getDataPropParams->numOfPatchedInstructionRecords/(double)getDataPropParams->numOfInstances;
        double averageValue = (features[m].appliedCount > 0) ? features[m].sumValue / features[m].appliedCount : 0.0;
        if (false && features[m].appliedCount > 0) {
            LOGE(LOG_INFO, "Feature for metric %s: ratio = %.3f, average value = %.3f\n",
            features[m].metricName,
            ratio, averageValue);
        }
        features[m].ratio = ratio;
        features[m].averageValue = averageValue;
    }

    return features;
}

metric_feature_t* get_sass_metrics(api_record_t* record){

    init_sass_metrics();
    // Get the list of available SASS metrics

    // Enable SASS Patching
    metric_feature_t* features = NULL;

    CUpti_SassMetricsEnable_Params enableParams;
    memset(&enableParams, 0, sizeof(enableParams));
    enableParams.structSize = CUpti_SassMetricsEnable_Params_STRUCT_SIZE;
    enableParams.enableLazyPatching = 1;
    CHECK_CUPTI(cuptiSassMetricsEnable(&enableParams));

    exe_cuda_launch_kernel_1(record);
    cudaDeviceSynchronize();

    // Get data properties for the patched instructions
    CUpti_SassMetricsGetDataProperties_Params getDataPropParams;
    memset(&getDataPropParams, 0, sizeof(getDataPropParams));
    getDataPropParams.structSize = CUpti_SassMetricsGetDataProperties_Params_STRUCT_SIZE;
    CHECK_CUPTI(cuptiSassMetricsGetDataProperties(&getDataPropParams));

    LOGE(LOG_DEBUG, "Data properties: numOfInstances: %lu, numOfPatchedInstructionRecords: %lu\n",
           getDataPropParams.numOfInstances,
           getDataPropParams.numOfPatchedInstructionRecords);

    if (getDataPropParams.numOfInstances != 0 && getDataPropParams.numOfPatchedInstructionRecords != 0)
    {
        // allocate memory for getting patched data.

        CUpti_SassMetricsFlushData_Params flushDataParams;
        memset(&flushDataParams, 0, sizeof(flushDataParams));
        flushDataParams.structSize = CUpti_SassMetricsFlushData_Params_STRUCT_SIZE;
        flushDataParams.numOfInstances = getDataPropParams.numOfInstances;
        flushDataParams.numOfPatchedInstructionRecords = getDataPropParams.numOfPatchedInstructionRecords;
        flushDataParams.pMetricsData =
                (CUpti_SassMetrics_Data*)malloc(getDataPropParams.numOfPatchedInstructionRecords * sizeof(CUpti_SassMetrics_Data));
        memset(flushDataParams.pMetricsData, 0, getDataPropParams.numOfPatchedInstructionRecords * sizeof(CUpti_SassMetrics_Data));
        for (size_t recordIndex = 0;
            recordIndex < getDataPropParams.numOfPatchedInstructionRecords;
            ++recordIndex)
        {
            flushDataParams.pMetricsData[recordIndex].structSize = CUpti_SassMetricsFlushData_Params_STRUCT_SIZE;
            flushDataParams.pMetricsData[recordIndex].pInstanceValues =
                (CUpti_SassMetrics_InstanceValue*) malloc(getDataPropParams.numOfInstances * sizeof(CUpti_SassMetrics_InstanceValue));
            memset(flushDataParams.pMetricsData[recordIndex].pInstanceValues, 0, getDataPropParams.numOfInstances * sizeof(CUpti_SassMetrics_InstanceValue));
        }

        CHECK_CUPTI(cuptiSassMetricsFlushData(&flushDataParams));
        
        features = assemble_metrics(&flushDataParams, &getDataPropParams);
    }

    // All kernels patched earlier will be reset to their original state.
    CUpti_SassMetricsDisable_Params disableParams;
    memset(&disableParams, 0, sizeof(disableParams));
    disableParams.structSize = CUpti_SassMetricsDisable_Params_STRUCT_SIZE;
    CHECK_CUPTI(cuptiSassMetricsDisable(&disableParams));
    return features;
}

void cupti_profile(api_record_t* record){
}

// process the database records 
void process_database_records(){
    // can you take some abstract feature of a kernel and record how long it takes
    for(int i = 0; i < database_records.length; i++){
        api_record_t *record;
        if(list_at(&database_records, i, (void**)&record) != 0){
            LOGE(LOG_ERROR, "list_at %d returned an error.", i);
            continue;
        }
        if(record->exe_status == 1) continue;
        switch(record->function){
            case CUDA_LAUNCH_KERNEL:
                LOGE(LOG_DEBUG, "Processing database record for CUDA_LAUNCH_KERNEL");
                cudaEvent_t start, stop;
                cuda_launch_kernel_1_argument *arg = (cuda_launch_kernel_1_argument*)record->arguments;
                ptr stream = arg->arg6;
                cudaEventCreate(&start);
                cudaEventCreate(&stop);
                cudaEventRecord(start, (void*)stream);
                exe_cuda_launch_kernel_1(record);
                cudaEventRecord(stop, (void*)stream);
                cudaEventSynchronize(stop);
                float ms;
                cudaEventElapsedTime(&ms, start, stop);
                // note ts is heavily overloaded with its meaning
                // here it represents the duration of the kernel
                record->ts = (uint64_t)(ms*1000000) - record->ts;
                LOGE(LOG_DEBUG, "func %p, latency ts(us) %lu\n", (void*) arg->arg1, (record->ts)/1000);

                metric_feature_t* features = get_sass_metrics(record);
                if(features == NULL){
                    LOGE(LOG_ERROR, "Failed to get SASS metrics");
                    break;
                }else{
                    dump_metrics_to_database(features, numOfMetrics, record->ts);
                    free(features);
                }
                
                record->ts = 0;
                break;
            default:
                break;
        }
    }

}

void deinit_server_exec(){
    deinit_sass_metrics();
}
