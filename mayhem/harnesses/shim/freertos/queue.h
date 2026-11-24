// Host stub for freertos/queue.h (fuzz harnesses only): a single-threaded fixed-size ring queue.
// sim/platform ships FreeRTOS.h and task.h but not the queue/semaphore API NanoPbComm uses.
#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cstdlib>
#include <cstring>

struct GmFuzzQueue {
    UBaseType_t depth, itemSize, head, count;
    unsigned char *buf;
};
typedef GmFuzzQueue *QueueHandle_t;

static inline QueueHandle_t xQueueCreate(UBaseType_t depth, UBaseType_t itemSize) {
    auto *q = static_cast<GmFuzzQueue *>(calloc(1, sizeof(GmFuzzQueue)));
    q->depth = depth;
    q->itemSize = itemSize;
    q->buf = static_cast<unsigned char *>(calloc(depth, itemSize));
    return q;
}
static inline void vQueueDelete(QueueHandle_t q) {
    if (!q)
        return;
    free(q->buf);
    free(q);
}
static inline BaseType_t xQueueSend(QueueHandle_t q, const void *item, TickType_t) {
    if (q->count >= q->depth)
        return pdFALSE;
    memcpy(q->buf + ((q->head + q->count) % q->depth) * q->itemSize, item, q->itemSize);
    q->count++;
    return pdTRUE;
}
static inline BaseType_t xQueueReceive(QueueHandle_t q, void *item, TickType_t) {
    if (q->count == 0)
        return pdFALSE;
    memcpy(item, q->buf + q->head * q->itemSize, q->itemSize);
    q->head = (q->head + 1) % q->depth;
    q->count--;
    return pdTRUE;
}
static inline BaseType_t xQueueReset(QueueHandle_t q) {
    q->head = q->count = 0;
    return pdPASS;
}
static inline UBaseType_t uxQueueSpacesAvailable(QueueHandle_t q) { return q->depth - q->count; }
static inline UBaseType_t uxQueueMessagesWaiting(QueueHandle_t q) { return q->count; }

// The sim task.h has no task notifications; Endpoint only wakes a pump task when one is registered
// (setPumpTask), which the harnesses never do.
#ifndef xTaskNotifyGive
static inline BaseType_t xTaskNotifyGive(TaskHandle_t) { return pdPASS; }
#endif
