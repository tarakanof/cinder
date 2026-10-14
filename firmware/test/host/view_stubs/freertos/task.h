#pragma once
#include "FreeRTOS.h"

/* Pinned tasks run as threads, each with its own notification count. */
typedef struct host_task {
    void (*fn)(void *);
    void *arg;
    pthread_mutex_t m;
    pthread_cond_t c;
    uint32_t n;
} host_task_t;
typedef host_task_t *TaskHandle_t;
typedef void (*host_task_fn)(void *);

__attribute__((weak)) _Thread_local host_task_t *host_task_self;
__attribute__((weak)) host_task_t host_task_shared = {.m = PTHREAD_MUTEX_INITIALIZER, .c = PTHREAD_COND_INITIALIZER};

static void *host_task_run(void *p)
{
    host_task_t *t = p;
    host_task_self = t;
    t->fn(t->arg);
    return NULL;
}

static inline BaseType_t xTaskCreatePinnedToCore(host_task_fn fn, const char *name, uint32_t stack, void *arg, int prio,
                                                 TaskHandle_t *handle, int core)
{
    (void)name, (void)stack, (void)prio, (void)core;
    host_task_t *t = calloc(1, sizeof *t);
    t->fn = fn;
    t->arg = arg;
    pthread_mutex_init(&t->m, NULL);
    pthread_cond_init(&t->c, NULL);
    if (handle) *handle = t;
    pthread_t th;
    if (pthread_create(&th, NULL, host_task_run, t)) return pdFALSE;
    pthread_detach(th);
    return pdPASS;
}

static inline BaseType_t xTaskCreatePinnedToCoreWithCaps(host_task_fn fn, const char *name, uint32_t stack, void *arg, int prio,
                                                         TaskHandle_t *handle, int core, unsigned caps)
{
    (void)caps;
    return xTaskCreatePinnedToCore(fn, name, stack, arg, prio, handle, core);
}

static inline void xTaskNotifyGive(TaskHandle_t t)
{
    pthread_mutex_lock(&t->m);
    t->n++;
    pthread_cond_signal(&t->c);
    pthread_mutex_unlock(&t->m);
}

static inline uint32_t ulTaskNotifyTake(BaseType_t clear, TickType_t ticks)
{
    (void)ticks;
    host_task_t *t = host_task_self ? host_task_self : &host_task_shared;
    pthread_mutex_lock(&t->m);
    while (!t->n) pthread_cond_wait(&t->c, &t->m);
    uint32_t n = t->n;
    t->n = clear ? 0 : n - 1;
    pthread_mutex_unlock(&t->m);
    return n;
}

static inline void vTaskDelay(TickType_t t) { (void)t; }
