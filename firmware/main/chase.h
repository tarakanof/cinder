#pragma once

/* Any task. fps 0 unchanged, laps 0 random. Returns 0 started (next frame), 1 busy, 2 not working. */
int app_chase_request(int style, int fps, int laps);
