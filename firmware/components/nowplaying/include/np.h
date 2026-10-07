#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NP_SOURCE_MAX 32
#define NP_TEXT_MAX 255
#define NP_ARTV_MAX 32
#define NP_TRACK_ID_MAX 128

typedef enum { NP_NONE, NP_PLAYING, NP_PAUSED } np_play_t;

typedef struct {
    np_play_t state;
    char source[NP_SOURCE_MAX + 1];
    char title[NP_TEXT_MAX + 1];
    char artist[NP_TEXT_MAX + 1];
    char album[NP_TEXT_MAX + 1];
    long long duration_ms;
    long long position_ms;
    long long position_at;   /* server Unix ms (not s like ends_at); 0 = absent */
    char art_version[NP_ARTV_MAX + 1];
    bool album_art, artist_art;
    int volume;
    char track_id[NP_TRACK_ID_MAX + 1];
} np_info_t;

struct cJSON;
bool np_parse(const struct cJSON *o, np_info_t *out);
bool np_same_track(const np_info_t *a, const np_info_t *b);

long long np_position_ms(const np_info_t *np, double offset_s, double now_s);

typedef struct {
    bool valid;
    bool playing;
    double t;
    long long pos_ms;
    long long dur_ms;
} np_anchor_t;
void np_anchor_sync(np_anchor_t *a, bool same_track, bool playing, long long pos_ms, long long dur_ms, double now);
long long np_anchor_at(const np_anchor_t *a, double now);

int np_format_time(long long ms, char *buf, size_t cap);

/* ASCII-only output for Montserrat; out is NUL-terminated, cut at cap - 1. */
void np_text_fold(const char *in, char *out, size_t cap);

typedef enum { NP_ART_BACKDROP, NP_ART_ALBUM, NP_ART_ARTIST, NP_ART_KINDS } np_art_kind_t;
#define NP_BACKDROP_PX 466
#define NP_ALBUM_PX 240
#define NP_ARTIST_PX 64
int np_art_px(np_art_kind_t k);
const char *np_art_kind_name(np_art_kind_t k);

typedef struct {
    char version[NP_ARTV_MAX + 1];
    bool want[NP_ART_KINDS];
} np_art_plan_t;
void np_art_plan(const np_info_t *np, np_art_plan_t *out);

bool np_art_version_ok(const char *v);
bool np_art_url(const char *base, np_art_kind_t k, const char *version, char *out, size_t cap);

int np_art_retry_ms(int failures);

typedef struct {
    float cx, cy;
    float r;
    float hw;
} np_ring_t;

typedef struct {
    int x, y, w, h;
} np_rect_t;

void np_ring_render(const np_ring_t *ring, uint16_t *buf, int w, int h, np_rect_t rect, float frac, uint32_t arc_rgb,
                    uint32_t track_rgb);
bool np_ring_dirty(const np_ring_t *ring, float f0, float f1, int w, int h, np_rect_t *out);
void np_ring_point(const np_ring_t *ring, float frac, float *x, float *y);

void np_face_mask(uint16_t *buf, int w, int h, float cx, float cy, float r);

void np_circle_alpha(uint8_t *a, int size);

#ifdef __cplusplus
}
#endif
