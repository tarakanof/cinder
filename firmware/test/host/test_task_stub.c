#include <fcntl.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>

#include "freertos/task.h"

static pthread_mutex_t s_gate_m = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t s_gate_c = PTHREAD_COND_INITIALIZER;
static int s_open;
static atomic_uint s_took[2];

static void taker(void *arg)
{
    int i = (int)(intptr_t)arg;
    pthread_mutex_lock(&s_gate_m);
    while (!s_open) pthread_cond_wait(&s_gate_c, &s_gate_m);
    pthread_mutex_unlock(&s_gate_m);
    atomic_store(&s_took[i], ulTaskNotifyTake(pdTRUE, portMAX_DELAY));
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
    for (int i = 0; i < 200 && !(atomic_load(&s_took[0]) && atomic_load(&s_took[1])); i++) usleep(10000);
    unsigned ta = atomic_load(&s_took[0]), tb = atomic_load(&s_took[1]);
    printf("task stub: task a took %u, task b took %u\n", ta, tb);
    if (ta != 2 || tb != 1) {
        printf("FAIL task stub: each task takes only its own notifications (a 2, b 1)\n");
        failures++;
    }
    fflush(stdout);
    pid_t pid = fork();
    if (pid == 0) {
        int null = open("/dev/null", O_WRONLY);
        if (null >= 0) dup2(null, STDERR_FILENO);
        alarm(5);
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        _exit(0);
    }
    int st = 0;
    bool aborted = pid > 0 && waitpid(pid, &st, 0) == pid && WIFSIGNALED(st) && WTERMSIG(st) == SIGABRT;
    printf("task stub: a take on a thread the stub did not create %s\n", aborted ? "aborts" : "does not abort");
    if (!aborted) {
        printf("FAIL task stub: a foreign thread's take aborts instead of waiting (status %d)\n", st);
        failures++;
    }
    return failures != 0;
}
