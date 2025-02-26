#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "cpu-server-exec.h"

static FILE* csv_file = NULL;

int init_metric_database(const char* arch_name, size_t num_features){

    char filename[256];
    snprintf(filename, sizeof(filename), "%s_metrics_%zu.csv", arch_name, num_features);
        
    // check if file exists to decide whether we need a header row.
    struct stat buffer;
    bool is_new_file = !(stat(filename, &buffer) == 0);

    csv_file = fopen(filename, "a");
    if (!csv_file) {
        LOGE(LOG_ERROR, "Error: Could not open file '%s' for append\n", filename);
        return -1;
    }

    // If this is a new file, write header.
    // e.g., "latency,ratio_0,avg_0,ratio_1,avg_1,..."
    if (is_new_file) {
        fprintf(csv_file, "latency_ns");
        for (size_t i = 0; i < num_features; i++) {
            fprintf(csv_file, ",ratio_%zu,avg_%zu", i, i);
        }
        fprintf(csv_file, "\n");
    }
}

int dump_metrics_to_database(metric_feature_t* features, size_t num_features, uint64_t latency){
    fprintf(csv_file, "%llu", (unsigned long long)latency);
    for (size_t i = 0; i < num_features; i++) {
        fprintf(csv_file, ",%f,%f", features[i].ratio, features[i].averageValue);
    }
    fprintf(csv_file, "\n");
}

int deinit_metric_database(){
    fclose(csv_file);
    return 0;
}