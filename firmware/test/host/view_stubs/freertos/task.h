#pragma once
#include "FreeRTOS.h"

/* Pinned tasks run as threads; one created "with caps" (the outline's rim task) never starts, its notifications drop. */
typedef void *TaskHandle_t;
typedef void (*host_task_fn)(void *);
typedef struct {
    host_task_fn fn;
    void *arg;
} host_task_t;

static void *host_task_run(void *p)
{
    host_task_t t = *(host_task_t *)p;
    free(p);
    t.fn(t.arg);
    return NULL;
}

static inline BaseType_t xTaskCreatePinnedToCore(host_task_fn fn, const char *name, uint32_t stack, void *arg, int prio,
                                                 TaskHandle_t *handle, int core)
{
    (void)name, (void)stack, (void)prio, (void)core;
    host_task_t *t = malloc(sizeof *t);
    t->fn = fn;
    t->arg = arg;
    pthread_t th;
    if (pthread_create(&th, NULL, host_task_run, t)) return pdFALSE;
    pthread_detach(th);
    if (handle) *handle = (TaskHandle_t)t;
    return pdPASS;
}

static inline BaseType_t xTaskCreatePinnedToCoreWithCaps(host_task_fn fn, const char *name, uint32_t stack, void *arg, int prio,
                                                         TaskHandle_t *handle, int core, unsigned caps)
{
    (void)fn, (void)name, (void)stack, (void)arg, (void)prio, (void)core, (void)caps;
    static int dummy;
    if (handle) *handle = &dummy;
    return pdPASS;
}

static inline void xTaskNotifyGive(TaskHandle_t t) { (void)t; }
static inline uint32_t ulTaskNotifyTake(BaseType_t clear, TickType_t ticks) { (void)clear, (void)ticks; return 0; }
static inline void vTaskDelay(TickType_t t) { (void)t; }
