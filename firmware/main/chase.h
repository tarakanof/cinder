#pragma once

/* Any task. fps 0 unchanged, laps 0 random. Returns 0 started (next frame), 1 busy, 2 not working. */
int app_chase_request(int style, int fps, int laps);

/* kind 0 turn (n detents, clockwise positive), 1 push, 2 long push, 3 touch, 4 push and turn (n pages); any task. */
void app_input_inject(int kind, int n);
