#pragma once
/* Single thread: the critical section only counts its depth, so a test can see nothing calls out under the lock. */
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
extern int host_crit_depth;
#define taskENTER_CRITICAL(m) ((void)(m), host_crit_depth++)
#define taskEXIT_CRITICAL(m) ((void)(m), host_crit_depth--)
