#pragma once
#include "FreeRTOS.h"

/* Pinned tasks run as threads; the one created "with caps" (the outline's rim task) is the only one notified. */
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
    static int running;
    if (handle) *handle = &running;
    return pdPASS;
}

static inline BaseType_t xTaskCreatePinnedToCoreWithCaps(host_task_fn fn, const char *name, uint32_t stack, void *arg, int prio,
                                                         TaskHandle_t *handle, int core, unsigned caps)
{
    (void)caps;
    return xTaskCreatePinnedToCore(fn, name, stack, arg, prio, handle, core);
}

static pthread_mutex_t host_notify_m = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t host_notify_c = PTHREAD_COND_INITIALIZER;
static uint32_t host_notify_n;

static inline void xTaskNotifyGive(TaskHandle_t t)
{
    (void)t;
    pthread_mutex_lock(&host_notify_m);
    host_notify_n++;
    pthread_cond_signal(&host_notify_c);
    pthread_mutex_unlock(&host_notify_m);
}

static inline uint32_t ulTaskNotifyTake(BaseType_t clear, TickType_t ticks)
{
    (void)ticks;
    pthread_mutex_lock(&host_notify_m);
    while (!host_notify_n) pthread_cond_wait(&host_notify_c, &host_notify_m);
    uint32_t n = host_notify_n;
    host_notify_n = clear ? 0 : n - 1;
    pthread_mutex_unlock(&host_notify_m);
    return n;
}

static inline void vTaskDelay(TickType_t t) { (void)t; }
