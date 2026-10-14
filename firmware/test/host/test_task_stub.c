#include <stdio.h>
#include <unistd.h>

#include "freertos/task.h"

static pthread_mutex_t s_gate_m = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t s_gate_c = PTHREAD_COND_INITIALIZER;
static int s_open;
static volatile uint32_t s_took[2];

static void taker(void *arg)
{
    int i = (int)(intptr_t)arg;
    pthread_mutex_lock(&s_gate_m);
    while (!s_open) pthread_cond_wait(&s_gate_c, &s_gate_m);
    pthread_mutex_unlock(&s_gate_m);
    s_took[i] = ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
}

int main(void)
{
    int failures = 0;
    TaskHandle_t a, b;
    if (xTaskCreatePinnedToCore(taker, "a", 0, (void *)0, 0, &a, 0) != pdPASS || xTaskCreatePinnedToCore(taker, "b", 0, (void *)1, 0, &b, 0) != pdPASS) {
        printf("FAIL task stub: tasks not created\n");
        return 1;
    }
    xTaskNotifyGive(a);
    xTaskNotifyGive(a);
    xTaskNotifyGive(b);
    pthread_mutex_lock(&s_gate_m);
    s_open = 1;
    pthread_cond_broadcast(&s_gate_c);
    pthread_mutex_unlock(&s_gate_m);
    for (int i = 0; i < 200 && !(s_took[0] && s_took[1]); i++) usleep(10000);
    printf("task stub: task a took %u, task b took %u\n", (unsigned)s_took[0], (unsigned)s_took[1]);
    if (s_took[0] != 2 || s_took[1] != 1) {
        printf("FAIL task stub: each task takes only its own notifications (a 2, b 1)\n");
        failures++;
    }
    xTaskNotifyGive(&host_task_shared);
    uint32_t n = ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    printf("task stub: a thread the stub did not create took %u\n", (unsigned)n);
    if (n != 1) {
        printf("FAIL task stub: a foreign thread takes from the shared slot (%u)\n", (unsigned)n);
        failures++;
    }
    return failures != 0;
}
