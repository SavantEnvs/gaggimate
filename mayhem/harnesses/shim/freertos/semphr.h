// Host stub for freertos/semphr.h (fuzz harnesses only): the harnesses are single-threaded,
// so mutexes are counters that only check balanced use.
#pragma once
#include <freertos/FreeRTOS.h>
#include <cstdlib>

struct GmFuzzSem {
    int depth;
};
typedef GmFuzzSem *SemaphoreHandle_t;

static inline SemaphoreHandle_t xSemaphoreCreateMutex() { return static_cast<SemaphoreHandle_t>(calloc(1, sizeof(GmFuzzSem))); }
static inline SemaphoreHandle_t xSemaphoreCreateRecursiveMutex() { return xSemaphoreCreateMutex(); }
static inline void vSemaphoreDelete(SemaphoreHandle_t s) { free(s); }
static inline BaseType_t xSemaphoreTake(SemaphoreHandle_t s, TickType_t) {
    s->depth++;
    return pdTRUE;
}
static inline BaseType_t xSemaphoreGive(SemaphoreHandle_t s) {
    if (s->depth <= 0)
        abort(); // unbalanced give: a real bug in the caller
    s->depth--;
    return pdTRUE;
}
static inline BaseType_t xSemaphoreTakeRecursive(SemaphoreHandle_t s, TickType_t t) { return xSemaphoreTake(s, t); }
static inline BaseType_t xSemaphoreGiveRecursive(SemaphoreHandle_t s) { return xSemaphoreGive(s); }
