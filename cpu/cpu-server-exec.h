#include <stdint.h>
#include <stdbool.h>
#include "api-recorder.h"
#include "resource-mg.h"
#include "log.h"

int serialize_all_till_now(uint64_t timestamp);

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