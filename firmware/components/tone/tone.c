#include "tone.h"

#include <math.h>

void tone_init(tone_t *t, uint32_t rate, uint32_t hz, uint32_t ms, uint8_t vol_pct)
{
    if (vol_pct > 100) vol_pct = 100;
    t->rate = rate;
    t->hz = hz;
    t->total = (uint32_t)((uint64_t)rate * ms / 1000);
    t->done = 0;
    t->fade = rate * TONE_FADE_MS / 1000;
    if (t->fade * 2 > t->total) t->fade = t->total / 2;
    t->amp = (int16_t)(TONE_AMP_MAX * vol_pct / 100);
}

static float envelope(const tone_t *t, uint32_t i)
{
    if (!t->fade) return 1;
    if (i < t->fade) return (float)i / (float)t->fade;
    uint32_t left = t->total - 1 - i;
    if (left < t->fade) return (float)left / (float)t->fade;
    return 1;
}

size_t tone_fill(tone_t *t, int16_t *buf, size_t n)
{
    size_t k = 0;
    if (!t->rate) return 0;
    while (k < n && t->done < t->total) {
        uint32_t i = t->done++;
        float phase = 6.28318530718f * (float)(((uint64_t)i * t->hz) % t->rate) / (float)t->rate;
        buf[k++] = (int16_t)lrintf((float)t->amp * envelope(t, i) * sinf(phase));
    }
    return k;
}

bool tone_done(const tone_t *t) { return t->done >= t->total; }
