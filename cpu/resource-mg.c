#include "resource-mg.h"
#include "list.h"
#include "log.h"
#include <stdio.h>


int resource_mg_init(resource_mg *mg, int bypass)
{
    int ret = 0;
    if ((ret = list_init(&mg->new_res, sizeof(void*))) != 0) {
        LOGE(LOG_ERROR, "error initializing new_res list");
        goto out;
    }
    if (bypass == 0) {
        if ((ret = list_init(&mg->map_res, sizeof(resource_mg_map_elem))) != 0) {
            LOGE(LOG_ERROR, "error initializing map_res list");
            goto out;
        }
    }
    mg->bypass = bypass;
 out:
    return ret;
}

void resource_mg_free(resource_mg *mg)
{
    list_free(&mg->new_res);
    if (mg->bypass == 0) {
        list_free(&mg->map_res);
    }
}

int resource_mg_create(resource_mg *mg, void *cuda_address)
{
    if (list_append_copy(&mg->new_res, &cuda_address) != 0) {
        LOGE(LOG_ERROR, "failed to append to new_res");
        return 1;
    }
    return 0;
}

static void* resource_mg_search_map(resource_mg *mg, void *client_address)
{
    size_t start = 0;
    size_t end;
    size_t mid;
    resource_mg_map_elem *mid_elem;
    if (mg == NULL) {
        LOGE(LOG_ERROR, "resource manager mg is NULL");
        return NULL;
    }
    if (mg->map_res.length == 0) {
        return client_address;
    }
    end = mg->map_res.length-1;
    
    while (end >= start) {
        mid = start + (end-start)/2;
        mid_elem = list_get(&mg->map_res, mid);
        if (mid_elem == NULL) {
            LOG(LOG_ERROR, "list state of map_res is inconsistent");
            return NULL;
        }

        if (mid_elem->client_address > client_address) {
            end = mid-1;
            if (mid == 0) {
                break;
            }
        } else if (mid_elem->client_address < client_address) {
            start = mid+1;
        } else /*if (mid_elem->client_address == client_address)*/ {
            return mid_elem->cuda_address;
        }
    }
    LOGE(LOG_DEBUG, "no find: %p", client_address);
    return client_address;
}

void resource_mg_print(resource_mg *mg)
{
    size_t i;
    resource_mg_map_elem *elem;
    if (mg == NULL) {
        LOGE(LOG_ERROR, "resource manager mg is NULL");
        return;
    }
    LOG(LOG_DEBUG, "new_res:");
    for (i = 0; i < mg->new_res.length; i++) {
        LOG(LOG_DEBUG, "%p", *(void**)list_get(&mg->new_res, i));
    }
    if (mg->bypass == 0) {
        LOG(LOG_DEBUG, "map_res:");
        for (i = 0; i < mg->map_res.length; i++) {
            elem = list_get(&mg->map_res, i);
            LOG(LOG_DEBUG, "%p -> %p", elem->client_address, elem->cuda_address);
        }
    }
}

inline void* resource_mg_get(resource_mg *mg, void* client_address)
{
    if (mg->bypass) {
        return client_address;
    } else {
        return resource_mg_search_map(mg, client_address);
    }
    return 0;
}

int resource_mg_add_sorted(resource_mg *mg, void* client_address, void* cuda_address)
{
    ssize_t start = 0;
    ssize_t end = mg->map_res.length-1;
    ssize_t mid;
    struct resource_mg_map_elem_t new_elem = {.client_address = client_address,
                                              .cuda_address = cuda_address};
    resource_mg_map_elem *mid_elem;
    if (mg == NULL) {
        LOGE(LOG_ERROR, "resource manager mg is NULL");
        return 1;
    }
    if (mg->bypass) {
        LOGE(LOG_ERROR, "cannot add to bypassed resource manager");
        return 1;
    }
    if (mg->map_res.length == 0) {
        return list_append_copy(&mg->map_res, &new_elem);
    }
    end = mg->map_res.length-1;
    
    while (end >= start) {
        mid = start + (end-start)/2;
        mid_elem = list_get(&mg->map_res, mid);
        if (mid_elem == NULL) {
            LOG(LOG_ERROR, "list state of map_res is inconsistent");
            return 1;
        }

        if (mid_elem->client_address > client_address) {
            end = mid-1;
        } else if (mid_elem->client_address < client_address) {
            start = mid+1;
        } else /*if (mid_elem->client_address == client_address)*/ {
            LOGE(LOG_WARNING, "duplicate resource! The first resource will be overwritten");
            mid_elem->cuda_address = cuda_address;
            return 0;
        }
    }
    if (end < 0LL) {
        end = 0;
    }
    resource_mg_map_elem *end_elem = list_get(&mg->map_res, end);
    if (end_elem->client_address < client_address) {
        end++;
    }
    return list_insert(&mg->map_res, end, &new_elem);
}


int memory_mg_init(memory_mg *mg, int bypass)
{
    int ret = 0;
    if ((ret = list_init(&mg->rm.new_res, sizeof(memory_mg_new_elem))) != 0) {
        LOGE(LOG_ERROR, "error initializing new_res list");
        goto out;
    }
    if (bypass == 0) {
        if ((ret = list_init(&mg->rm.map_res, sizeof(memory_mg_map_elem))) != 0) {
            LOGE(LOG_ERROR, "error initializing map_res list");
            goto out;
        }
    }
    mg->rm.bypass = bypass;
 out:
    return ret;
}

void memory_mg_free(memory_mg *mg)
{
    resource_mg_free(&mg->rm);
}

int memory_mg_add_sorted(memory_mg *mg, void* client_address, void* cuda_address, size_t size)
{
    ssize_t start = 0;
    ssize_t end = mg->rm.map_res.length-1;
    ssize_t mid;
    struct memory_mg_map_elem_t new_elem = {.client_address = client_address,
                                            .cuda_address = cuda_address,
                                            size = size};
    resource_mg_map_elem *mid_elem;
    if (mg == NULL) {
        LOGE(LOG_ERROR, "resource manager mg is NULL");
        return 1;
    }
    if (mg->rm.bypass) {
        LOGE(LOG_ERROR, "cannot add to bypassed resource manager");
        return 1;
    }
    if (mg->rm.map_res.length == 0) {
        return list_append_copy(&mg->rm.map_res, &new_elem);
    }
    end = mg->rm.map_res.length-1;
    
    while (end >= start) {
        mid = start + (end-start)/2;
        mid_elem = list_get(&mg->rm.map_res, mid);
        if (mid_elem == NULL) {
            LOG(LOG_ERROR, "list state of map_res is inconsistent");
            return 1;
        }

        if (mid_elem->client_address > client_address) {
            end = mid-1;
        } else if (mid_elem->client_address < client_address) {
            start = mid+1;
        } else /*if (mid_elem->client_address == client_address)*/ {
            LOGE(LOG_WARNING, "duplicate resource! The first resource will be overwritten");
            mid_elem->cuda_address = cuda_address;
            return 0;
        }
    }
    if (end < 0LL) {
        end = 0;
    }
    memory_mg_map_elem *end_elem = list_get(&mg->rm.map_res, end);
    if (end_elem->client_address < client_address) {
        end++;
    }
    return list_insert(&mg->rm.map_res, end, &new_elem);
}

int memory_mg_create(memory_mg *mg, void* cuda_address, size_t size)
{
    memory_mg_new_elem *elem;
    if (list_append(&mg->rm.new_res, (void**)&elem) != 0) {
        LOGE(LOG_ERROR, "failed to append to new_res");
        return 1;
    }
    elem->cuda_address = cuda_address;
    elem->size = size;
    return 0;  
}

static void* memory_mg_search_map(memory_mg *mg, void *client_address)
{
    size_t start = 0;
    size_t end;
    size_t mid;
    memory_mg_map_elem *mid_elem;
    if (mg == NULL) {
        LOGE(LOG_ERROR, "resource manager mg is NULL");
        return NULL;
    }
    if (mg->rm.map_res.length == 0) {
        return client_address;
    }
    end = mg->rm.map_res.length-1;
    
    while (end >= start) {
        mid = start + (end-start)/2;
        mid_elem = list_get(&mg->rm.map_res, mid);
        if (mid_elem == NULL) {
            LOG(LOG_ERROR, "list state of map_res is inconsistent");
            return NULL;
        }

        if (mid_elem->client_address > client_address) {
            end = mid-1;
            if (mid == 0) {
                break;
            }
        } else if (mid_elem->client_address < client_address) {
            if (mid_elem->client_address + mid_elem->size > client_address) {
                return mid_elem->cuda_address + (client_address - mid_elem->client_address);
            } else {
                start = mid+1;
            }
        } else /*if (mid_elem->client_address == client_address)*/ {
            return mid_elem->cuda_address;
        }
    }
    LOGE(LOG_DEBUG, "no find: %p", client_address);
    return client_address;
}

void* memory_mg_get(memory_mg *mg, void* client_address)
{
    if (mg->rm.bypass) {
        return client_address;
    } else {
        return memory_mg_search_map(mg, client_address);
    }
    return 0;
}

void memory_mg_print(memory_mg *mg)
{
    size_t i;
    memory_mg_new_elem *new_elem;
    memory_mg_map_elem *map_elem;
    if (mg == NULL) {
        LOGE(LOG_ERROR, "resource manager mg is NULL");
        return;
    }
    LOG(LOG_DEBUG, "new_res:");
    for (i = 0; i < mg->rm.new_res.length; i++) {
        new_elem = list_get(&mg->rm.new_res, i);
        LOG(LOG_DEBUG, "%p (%zu)", new_elem->cuda_address, new_elem->size);
    }
    if (mg->rm.bypass == 0) {
        LOG(LOG_DEBUG, "map_res:");
        for (i = 0; i < mg->rm.map_res.length; i++) {
            map_elem = list_get(&mg->rm.map_res, i);
            LOG(LOG_DEBUG, "%p (%zu) -> %p", map_elem->client_address, map_elem->size, map_elem->cuda_address);
        }
    }
}
