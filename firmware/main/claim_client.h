#pragma once

#include <stdbool.h>

#include "http_conn.h"

/* Ember task only, every loop turn: one claim step (requests only while online). true when a token from an approved claim was stored (check in at once). */
bool claim_client_service(http_conn_t *conn, const char *base, bool online, bool unauthorized, char *buf, int cap);
/* Any task: the code on screen, 0..999999, or -1. */
int claim_client_code(void);
/* Any task: a push, turn or touch. */
void claim_client_input(void);
/* Any task: a USB set_ember drops the claim. */
void claim_client_cancel(void);
