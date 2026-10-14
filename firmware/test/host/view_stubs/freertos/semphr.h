#pragma once
#include "FreeRTOS.h"

typedef struct {
    pthread_mutex_t m;
    pthread_cond_t c;
    int n;
} host_sem_t;
typedef host_sem_t *SemaphoreHandle_t;

static inline SemaphoreHandle_t xSemaphoreCreateBinary(void)
{
    host_sem_t *s = calloc(1, sizeof *s);
    pthread_mutex_init(&s->m, NULL);
    pthread_cond_init(&s->c, NULL);
    return s;
}

static inline BaseType_t xSemaphoreGive(SemaphoreHandle_t s)
{
    pthread_mutex_lock(&s->m);
    s->n = 1;
    pthread_cond_signal(&s->c);
    pthread_mutex_unlock(&s->m);
    return pdTRUE;
}

static inline BaseType_t xSemaphoreTake(SemaphoreHandle_t s, TickType_t ticks)
{
    struct timespec end;
    clock_gettime(CLOCK_REALTIME, &end);
    end.tv_sec += ticks / 1000;
    end.tv_nsec += (long)(ticks % 1000) * 1000000L;
    if (end.tv_nsec >= 1000000000L) end.tv_sec++, end.tv_nsec -= 1000000000L;
    pthread_mutex_lock(&s->m);
    int r = 0;
    while (!s->n && r != ETIMEDOUT) r = ticks == portMAX_DELAY ? pthread_cond_wait(&s->c, &s->m) : pthread_cond_timedwait(&s->c, &s->m, &end);
    BaseType_t got = s->n ? pdTRUE : pdFALSE;
    s->n = 0;
    pthread_mutex_unlock(&s->m);
    return got;
}
