#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "lvgl.h"
#include "np_cover_view.h"
#include "nowplaying_view.h"
#include "src/display/lv_display_private.h"

#define W 472
#define H 466
#define FX 3

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static uint16_t s_fb[W * H];
static int s_areas, s_forgets;
static long s_px;
static lv_area_t s_box;
static const char *s_dir;

static np_snapshot_t s_snap;
static uint32_t s_snap_gen = 1;
static np_art_set_t s_pend;
static bool s_has_pend;
static int s_takes, s_frees;
static int64_t s_now_us = 1000000;
static bool s_no_face_buf;

int host_heap_fail(size_t n) { return s_no_face_buf && n >= (size_t)NP_COVER_PX * NP_COVER_PX * 2; }

#define TRACK_MAX 64
static void *s_tracked[TRACK_MAX];
static int s_freed[TRACK_MAX], s_ntracked;
static void *s_orig[TRACK_MAX];

static void *track(void *p)
{
    if (s_ntracked < TRACK_MAX) {
        s_tracked[s_ntracked] = s_orig[s_ntracked] = p;
        s_freed[s_ntracked++] = 0;
    }
    return p;
}

static void note_freed(void *p)
{
    if (!p) return;
    for (int i = s_ntracked - 1; i >= 0; i--)
        if (s_tracked[i] == p) {
            s_freed[i]++;
            return;
        }
}

static int freed(void *p)
{
    for (int i = s_ntracked - 1; i >= 0; i--)
        if (s_orig[i] == p) return s_freed[i];
    return -1;
}

void host_heap_freed(void *p) { note_freed(p); }

void *host_heap_alloced(void *p)
{
    for (int i = 0; i < s_ntracked; i++)
        if (s_tracked[i] == p) s_tracked[i] = NULL;
    return p;
}

void np_client_forget_art(void) { s_forgets++; }
static int s_reoffers;
static char s_reoffer_v[NP_COVERV_MAX + 1];
void np_client_forget_dropped(const char *version)
{
    s_reoffers++;
    strcpy(s_reoffer_v, version);
}
int64_t esp_timer_get_time(void) { return s_now_us; }
void np_client_set_visible(bool on) { (void)on; }
void np_client_control(const np_cmd_t *cmd, const char *source, const char *track_id) { (void)cmd, (void)source, (void)track_id; }
void np_client_control_result(np_ctl_result_t *out) { memset(out, 0, sizeof *out); }

bool np_client_get(np_snapshot_t *out, uint32_t *gen)
{
    if (*gen == s_snap_gen) return false;
    *out = s_snap;
    *gen = s_snap_gen;
    return true;
}

bool np_client_take_art(np_art_set_t *out)
{
    if (!s_has_pend) return false;
    *out = s_pend;
    memset(&s_pend, 0, sizeof s_pend);
    s_has_pend = false;
    s_takes++;
    return true;
}

void np_art_set_free(np_art_set_t *a)
{
    if (a->backdrop || a->album || a->artist || a->cover || a->avatar) s_frees++;
    note_freed(a->cover);
    note_freed(a->avatar);
    free(a->backdrop);
    free(a->album);
    free(a->artist);
    free(a->cover);
    free(a->avatar);
    memset(a, 0, sizeof *a);
}

static uint32_t tick_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint32_t)(tv.tv_sec * 1000 + tv.tv_usec / 1000);
}

static void flush_cb(lv_display_t *d, const lv_area_t *a, uint8_t *px)
{
    const uint16_t *src = (const uint16_t *)px;
    int w = lv_area_get_width(a);
    for (int y = a->y1; y <= a->y2; y++) memcpy(&s_fb[y * W + a->x1], &src[(y - a->y1) * w], (size_t)w * 2);
    lv_display_flush_ready(d);
}

static void render_cb(lv_event_t *e)
{
    lv_display_t *d = lv_event_get_target(e);
    for (uint32_t i = 0; i < d->inv_p; i++) {
        if (d->inv_area_joined[i]) continue;
        s_areas++;
        s_px += (long)lv_area_get_width(&d->inv_areas[i]) * lv_area_get_height(&d->inv_areas[i]);
        s_box = d->inv_areas[i];
    }
}

static void refresh(lv_display_t *d)
{
    s_areas = 0;
    s_px = 0;
    lv_refr_now(d);
}

static void rgb(int x, int y, int *r, int *g, int *b)
{
    uint16_t p = s_fb[y * W + x];
    *r = (p >> 11) * 255 / 31;
    *g = ((p >> 5) & 63) * 255 / 63;
    *b = (p & 31) * 255 / 31;
}

static int luma(int x, int y)
{
    int r, g, b;
    rgb(x, y, &r, &g, &b);
    return (r * 3 + g * 6 + b) / 10;
}

static void save(const char *name)
{
    if (!s_dir) return;
    char path[512];
    snprintf(path, sizeof path, "%s/%s.ppm", s_dir, name);
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            int r, g, b;
            rgb(x, y, &r, &g, &b);
            fputc(r, f);
            fputc(g, f);
            fputc(b, f);
        }
    fclose(f);
}

static uint16_t *make_cover(void)
{
    uint16_t *c = malloc(NP_COVER_PX * NP_COVER_PX * 2);
    for (int y = 0; y < NP_COVER_PX; y++)
        for (int x = 0; x < NP_COVER_PX; x++) {
            float d = hypotf(x + 0.5f - 233, y + 0.5f - 233), k = y / 466.0f;
            int r = (int)(0x2B + (0xFF - 0x2B) * k), g = (int)(0x1B + (0x8A - 0x1B) * k), b = (int)(0x5A + (0x5C - 0x5A) * k);
            if (d > NP_DISC_R) r = g = b = 0;
            c[y * NP_COVER_PX + x] = (uint16_t)((r >> 3) << 11 | (g >> 2) << 5 | (b >> 3));
        }
    return c;
}

static uint8_t *make_avatar(void)
{
    const int n = NP_AVATAR_PX * NP_AVATAR_PX;
    uint8_t *a = malloc((size_t)n * 3);
    uint16_t *p = (uint16_t *)a;
    for (int i = 0; i < n; i++) p[i] = 0xF5B1;
    np_circle_alpha(a + n * 2, NP_AVATAR_PX);
    return a;
}

static void publish(const np_info_t *np)
{
    s_snap.have = true;
    s_snap.info = *np;
    s_snap.anchor = (np_anchor_t){.valid = true, .playing = np->state == NP_PLAYING, .t = 1, .pos_ms = np->position_ms,
                                  .dur_ms = np->duration_ms};
    s_snap_gen++;
}

static void frames(lv_display_t *d, int n)
{
    for (int i = 0; i < n; i++) {
        s_now_us += 100000;
        np_view_update(s_now_us / 1e6, false);
        refresh(d);
    }
}

static uint16_t *make_white(void)
{
    uint16_t *c = malloc(NP_COVER_PX * NP_COVER_PX * 2);
    for (int y = 0; y < NP_COVER_PX; y++)
        for (int x = 0; x < NP_COVER_PX; x++)
            c[y * NP_COVER_PX + x] = hypotf(x + 0.5f - 233, y + 0.5f - 233) <= NP_DISC_R ? 0xFFFF : 0;
    return c;
}

static int ink(int y0, int y1, int x0, int x1, int min)
{
    int n = 0;
    for (int y = y0; y < y1; y++)
        for (int x = FX + x0; x < FX + x1; x++) n += luma(x, y) > min;
    return n;
}

static bool outside_same(const uint16_t *a)
{
    for (int y = 0; y < H; y++)
        for (int x = FX; x < FX + NP_COVER_PX; x++)
            if (hypotf(x - FX + 0.5f - 233, y + 0.5f - 233) >= NP_DISC_R + 2 && a[y * W + x] != s_fb[y * W + x]) return false;
    return true;
}

typedef struct {
    int frames, max_areas, full;
    long max_px, px;
} run_t;

static run_t run(lv_display_t *d, npc_frame_t *f, double secs, double step)
{
    run_t r = {0};
    for (double t = 0; t < secs; t += step) {
        f->now += step;
        npc_update(f);
        refresh(d);
        r.frames += s_areas > 0;
        if (s_areas > r.max_areas) r.max_areas = s_areas;
        if (s_px > r.max_px) r.max_px = s_px;
        if (s_px >= 200000) r.full++;
        r.px += s_px;
    }
    return r;
}

static int v15_label(void) { return luma(FX + NP_V15_CX - 30, NP_V15_CY + 30); }

static void test_scrim(lv_display_t *d, lv_obj_t *root)
{
    static uint16_t before[W * H];
    npc_build(root);
    np_info_t np = {.state = NP_PLAYING, .has_accent = true, .accent = 0xFF8A5C, .duration_ms = 228000};
    strcpy(np.title, "White Album");
    strcpy(np.artist, "Northern Static");
    strcpy(np.cover_version, "c0ffee-r1");
    np_art_set_t art = {.is_cover = true, .cover = make_white()};
    strcpy(art.version, np.cover_version);
    npc_adopt(&art);
    npc_frame_t f = {.now = 100, .state = NPF_PLAYING, .ring = NP_RING_PROGRESS, .np = &np, .text_fresh = true,
                     .pos_ms = 160000, .dur_ms = 228000, .vol = 40, .badge_text = ""};
    npc_update(&f);
    f.now += 2;
    npc_update(&f);
    refresh(d);
    save("m1-white-no-hint");
    int top = luma(FX + 233, 180), band = luma(FX + 73, 300), low = luma(FX + 233, 420);
    printf("np_view: white cover, no cover_light: above %d, band %d, bottom %d\n", top, band, low);
    CHECK(top > 245, "above the band the cover is untouched: %d", top);
    CHECK(abs(band - 255 * (255 - NP_SCRIM_LIGHT) / 255) <= 6 && abs(low - band) <= 4, "the light scrim at the text rows: %d", band);
    int title_light = ink(285, 316, 140, 326, band + 60);
    memcpy(before, s_fb, sizeof before);

    strcpy(np.cover_version, "next0-r1");
    np.cover_light = true;
    npc_update(&f);
    refresh(d);
    CHECK(abs(luma(FX + 73, 300) - band) <= 2, "a hint for the next cover_version does not touch the cover on screen: %d",
          luma(FX + 73, 300));
    strcpy(np.cover_version, "c0ffee-r1");

    np.cover_light = true;
    npc_update(&f);
    refresh(d);
    save("m1-white-light");
    band = luma(FX + 73, 300);
    int strong = band, title = ink(285, 316, 140, 326, band + 120), time = ink(356, 381, 160, 306, band + 120);
    printf("np_view: white cover, cover_light: band %d, title %d px, time %d px; %d areas, %ld px\n", band, title, time,
           s_areas, s_px);
    CHECK(abs(band - 255 * (255 - NP_SCRIM_STRONG) / 255) <= 6, "the strong scrim at the text rows: %d", band);
    CHECK(luma(FX + 233, 180) > 245, "still untouched above: %d", luma(FX + 233, 180));
    CHECK(title > 300 && title > title_light && time > 150, "white text reads on a white cover: title %d, time %d px", title, time);
    CHECK(outside_same(before), "the scrim stays inside the disc: the ring and the black band are unchanged");

    f.dim = true;
    f.dx = 8;
    npc_update(&f);
    refresh(d);
    save("m1-white-dim");
    band = luma(FX + 73, 300);
    title = ink(285, 316, 140 + 8, 326 + 8, band + 60);
    printf("np_view: white cover dimmed: band %d, title %d px\n", band, title);
    CHECK(band < strong && title > 300, "dimmed: band %d darker, title still %d px", band, title);
    f.dim = false;
    f.dx = 0;

    f.state = NPF_PAUSED;
    npc_update(&f);
    refresh(d);
    save("m2-white-light");
    band = luma(FX + 73, 300);
    top = luma(FX + 73, 200);
    printf("np_view: white cover paused: above %d, band %d\n", top, band);
    CHECK(abs(band - strong) <= 6, "paused: shade and scrim stack to the scrim's level, not darker: %d vs %d", band, strong);
    CHECK(abs(top - 255 * (255 - 115) / 255) <= 6, "paused: the shade above the band: %d", top);
    CHECK(luma(FX + 233 - 12, 168) > 200, "the pause bar stays clear of the scrim");
    np.cover_light = false;
    npc_update(&f);
    refresh(d);
    band = luma(FX + 73, 300);
    CHECK(abs(band - top) <= 4, "paused, no hint: the light scrim adds nothing to the shade: %d vs %d", band, top);

    f.state = NPF_OFFLINE;
    np.cover_light = true;
    npc_update(&f);
    refresh(d);
    band = luma(FX + 73, 300);
    CHECK(abs(band - strong) <= 6, "offline: no darker than the scrim: %d", band);
    CHECK(luma(FX + 233, 106) < luma(FX + 233, 80) / 3 && ink(130, 200, 180, 286, 200) > 100, "the offline badge sits above the scrim");

    f.state = NPF_PLAYING;
    f.ring = NP_RING_VOLUME;
    f.badge = NPC_BADGE_NUMBER;
    f.badge_text = "62";
    npc_update(&f);
    refresh(d);
    save("m7-white-light");
    band = luma(FX + 73, 300);
    CHECK(abs(band - strong) <= 6, "volume: no darker than the scrim: %d", band);
    CHECK(ink(120, 210, 150, 316, 200) > 300, "the volume number stays clear");
    f.ring = NP_RING_PROGRESS;
    f.badge = NPC_BADGE_NONE;
    f.badge_text = "";

    art = (np_art_set_t){.is_cover = true, .no_cover = true};
    strcpy(art.version, np.cover_version);
    npc_adopt(&art);
    run(d, &f, 1.6, 1 / 60.0);
    save("m15-bad-cover");
    int label = v15_label(), rec = luma(FX + NP_V15_CX, NP_V15_CY - 100);
    band = luma(FX + 73, 300);
    printf("np_view: M15 for a bad cover: label %d, record %d, band %d\n", label, rec, band);
    CHECK(npc_has_cover() && label > 180 && rec < 40, "a bad cover: M15's vinyl, through black: label %d, record %d", label, rec);
    CHECK(band < 10, "no cover: no scrim, the text is on black: %d", band);
    CHECK(ink(285, 316, 140, 326, 200) < 40, "the straight title is hidden: %d px", ink(285, 316, 140, 326, 200));
    CHECK(ink(350, 425, 100, 366, 180) > 300, "the curved title and artist: %d px", ink(350, 425, 100, 366, 180));
    rgb(FX + 233, 233 + NP_EDGE_R, &top, &band, &low);
    CHECK(top > 240 && band > 120 && band < 150, "the ring regrows in the accent: %d,%d", top, band);
    f.state = NPF_OFFLINE;
    npc_update(&f);
    refresh(d);
    CHECK(npc_has_cover() && luma(FX + 233, 106) < 20, "offline keeps M15 with the badge");

    f.state = NPF_PLAYING;
    f.pos_ms = 170000;
    npc_update(&f);
    CHECK(strcmp(npc_time_text(), "-0:58") == 0, "M15 counts down: %s", npc_time_text());
    f.state = NPF_PAUSED;
    npc_update(&f);
    CHECK(strcmp(npc_time_text(), "Paused") == 0, "paused in the same second: %s", npc_time_text());
    f.state = NPF_PLAYING;
    npc_update(&f);
    CHECK(strcmp(npc_time_text(), "-0:58") == 0, "playing again in the same second: the relayout redraws the time: %s",
          npc_time_text());
    f.state = NPF_OFFLINE;
    f.pos_ms = 171000;
    npc_update(&f);
    CHECK(strcmp(npc_time_text(), "-0:58") == 0, "offline from playing freezes: %s", npc_time_text());
    f.state = NPF_PAUSED;
    npc_update(&f);
    f.state = NPF_OFFLINE;
    f.pos_ms = 172000;
    npc_update(&f);
    CHECK(strcmp(npc_time_text(), "-0:56") == 0, "offline from paused shows the position once: %s", npc_time_text());
    f.pos_ms = 175000;
    npc_update(&f);
    CHECK(strcmp(npc_time_text(), "-0:56") == 0, "then freezes: %s", npc_time_text());

    f.state = NPF_NOTHING;
    int forgets = s_forgets;
    npc_update(&f);
    refresh(d);
    CHECK(!npc_has_cover() && s_forgets == forgets + 1, "idle drops the no-cover look and asks again later");
    npc_destroy();
    lv_obj_clean(root);
}

static uint16_t *make_flat(uint16_t c565)
{
    uint16_t *c = malloc(NP_COVER_PX * NP_COVER_PX * 2);
    for (int y = 0; y < NP_COVER_PX; y++)
        for (int x = 0; x < NP_COVER_PX; x++)
            c[y * NP_COVER_PX + x] = hypotf(x + 0.5f - 233, y + 0.5f - 233) <= NP_DISC_R ? c565 : 0;
    return c;
}

typedef struct {
    npc_frame_t f;
    np_info_t np;
    uint16_t *a;
} rig_t;

static void rig_set(np_art_set_t *art, uint16_t *cover, const char *ver, bool no_cover)
{
    *art = (np_art_set_t){.is_cover = true, .cover = cover, .no_cover = no_cover};
    strcpy(art->version, ver);
}

static void rig_start(lv_display_t *d, lv_obj_t *root, rig_t *r, bool vinyl)
{
    npc_build(root);
    memset(r, 0, sizeof *r);
    r->np = (np_info_t){.state = NP_PLAYING, .has_accent = true, .accent = 0xFF8A5C, .duration_ms = 228000};
    strcpy(r->np.title, "Low Tide Radio");
    strcpy(r->np.artist, "Glass Harbor");
    strcpy(r->np.cover_version, "a0-r1");
    np_art_set_t art;
    r->a = vinyl ? NULL : track(make_flat(0xFFFF));
    rig_set(&art, r->a, "a0-r1", vinyl);
    npc_adopt(&art);
    r->f = (npc_frame_t){.now = 200, .state = NPF_PLAYING, .ring = NP_RING_PROGRESS, .np = &r->np, .text_fresh = true,
                         .pos_ms = 134000, .dur_ms = 228000, .vol = 40, .badge_text = ""};
    npc_update(&r->f);
    run(d, &r->f, 1.5, 0.1);
    r->f.text_fresh = false;
}

static void rig_skip(lv_display_t *d, rig_t *r, const char *ver, uint32_t accent, long long pos)
{
    snprintf(r->np.title, sizeof r->np.title, "Track %s", ver);
    strcpy(r->np.cover_version, ver);
    r->np.accent = accent;
    r->f.pos_ms = pos;
    r->f.text_fresh = true;
    r->f.now += 1 / 60.0;
    npc_update(&r->f);
    refresh(d);
    r->f.text_fresh = false;
}

static uint16_t *rig_adopt(rig_t *r, uint16_t c565, const char *ver)
{
    uint16_t *c = track(make_flat(c565));
    np_art_set_t art;
    rig_set(&art, c, ver, false);
    npc_adopt(&art);
    (void)r;
    return c;
}

static void rig_end(lv_obj_t *root)
{
    npc_destroy();
    lv_obj_clean(root);
}

static int disc(void) { return luma(FX + 233, 150); }

static bool ring_at(float frac, int *r, int *g, int *b)
{
    float x, y;
    np_ring_point(&(np_ring_t){.cx = 233, .cy = 233, .r = NP_EDGE_R, .hw = NP_EDGE_HW}, frac, &x, &y);
    rgb(FX + (int)x, (int)y, r, g, b);
    return *r + *g + *b > 200;
}

static void test_change(lv_display_t *d, lv_obj_t *root)
{
    rig_t rg;
    rig_start(d, root, &rg, false);
    int r, g, b;
    rig_skip(d, &rg, "b0-r1", 0x7FA8FF, 1000);
    run(d, &rg.f, 0.6, 1 / 60.0);
    bool lit = ring_at(0.4f, &r, &g, &b);
    CHECK(lit && r > 240 && b < 110, "a real skip (new track at 0:01): while the cover loads the ring stays where it was, "
          "in the old accent: %d,%d,%d", r, g, b);
    CHECK(disc() > 240, "and the old cover stays");

    uint16_t *bc = rig_adopt(&rg, 0x001F, "b0-r1");
    int lum[64], n = 0, black = -1, full = 0, max_areas = 0, unwound = -1, lit_before = 0;
    long px = 0;
    for (int i = 0; i < 64; i++) {
        rg.f.now += 1 / 60.0;
        rg.f.pos_ms = 1600 + (long long)(i * 1000 / 60);
        npc_update(&rg.f);
        refresh(d);
        lum[n] = disc();
        rgb(FX + 233, 150, &r, &g, &b);
        if (s_areas > max_areas) max_areas = s_areas;
        px += s_px;
        full += s_px >= 200000;
        bool on = ring_at(0.3f, &r, &g, &b);
        if (black < 0) {
            lit_before += on;
            if (!on && unwound < 0) unwound = n;
        }
        rgb(FX + 233, 150, &r, &g, &b);
        if (black < 0 && r < 8 && b < 8) black = n;
        n++;
    }
    int levels = 0, last = -1;
    for (int i = 0; i < n; i++)
        if (abs(lum[i] - last) > 3) {
            levels++;
            last = lum[i];
        }
    rgb(FX + 233, 150, &r, &g, &b);
    printf("np_view: M8 real skip: %d levels, ring unwound past 0.3 at frame %d, black at frame %d (%.0f ms), %d full-image "
           "areas, at most %d areas, %ld px in %d frames\n",
           levels, unwound, black, black * 1000 / 60.0, full, max_areas, px, n);
    CHECK(lum[10] > 240, "the old cover holds ~0.25 s: %d", lum[10]);
    CHECK(black >= 20 && black <= 24, "fades to black in 3 steps after the hold: frame %d", black);
    CHECK(lit_before >= 15 && unwound > 15 && unwound <= black, "the ring unwinds from where it was with the fade: lit %d frames, "
          "gone at %d", lit_before, unwound);
    CHECK(levels >= 6 && levels <= 8, "3 steps out, black, 3 steps in: %d levels", levels);
    CHECK(b > 240 && r < 10, "the new cover is in: %d,%d,%d", r, g, b);
    CHECK(full <= 1 && max_areas <= 3, "one full-image area at the swap, else the shade and the ring: %d full, %d areas", full,
          max_areas);
    CHECK(freed(rg.a) == 1 && freed(bc) == 0, "the old cover is freed once at the swap, the new one kept: %d %d", freed(rg.a),
          freed(bc));
    run(d, &rg.f, 1.0, 1 / 30.0);
    CHECK(!ring_at(0.3f, &r, &g, &b), "regrown to the new track's 0:03, not the old position");
    CHECK(ring_at(0.005f, &r, &g, &b) && b > 200, "in the new accent: %d,%d,%d", r, g, b);
    rig_end(root);
    CHECK(freed(bc) == 1, "hiding frees the shown cover once: %d", freed(bc));
}

static void test_interrupt(lv_display_t *d, lv_obj_t *root)
{
    rig_t rg;
    int r, g, b;
    uint16_t *bc, *cc;

    rig_start(d, root, &rg, false);
    rig_skip(d, &rg, "b0-r1", 0x7FA8FF, 1000);
    bc = rig_adopt(&rg, 0x001F, "b0-r1");
    run(d, &rg.f, 0.3, 1 / 60.0);
    rig_end(root);
    CHECK(freed(rg.a) == 1 && freed(bc) == 1, "hidden mid-fade: both covers freed once: %d %d", freed(rg.a), freed(bc));

    rig_start(d, root, &rg, false);
    rig_skip(d, &rg, "b0-r1", 0x7FA8FF, 1000);
    bc = rig_adopt(&rg, 0x001F, "b0-r1");
    run(d, &rg.f, 0.3, 1 / 60.0);
    rg.f.state = NPF_NOTHING;
    npc_update(&rg.f);
    CHECK(freed(rg.a) == 1 && freed(bc) == 1, "nothing playing mid-fade: both freed once: %d %d", freed(rg.a), freed(bc));
    rig_end(root);
    CHECK(freed(rg.a) == 1 && freed(bc) == 1, "and not again on hide: %d %d", freed(rg.a), freed(bc));

    rig_start(d, root, &rg, false);
    rig_skip(d, &rg, "b0-r1", 0x7FA8FF, 1000);
    bc = rig_adopt(&rg, 0x001F, "b0-r1");
    run(d, &rg.f, 0.2, 1 / 60.0);
    rig_skip(d, &rg, "c0-r1", 0x7FFF7F, 1000);
    CHECK(freed(bc) == 1, "a newer block drops the held cover at once, before the next one decodes: %d", freed(bc));
    CHECK(disc() > 240, "the old cover is back: %d", disc());
    cc = rig_adopt(&rg, 0x07E0, "c0-r1");
    run(d, &rg.f, 1.0, 1 / 60.0);
    rgb(FX + 233, 150, &r, &g, &b);
    CHECK(g > 240 && r < 10 && freed(rg.a) == 1 && freed(cc) == 0, "C replaces A: %d,%d,%d, A freed %d", r, g, b, freed(rg.a));
    rig_end(root);

    rig_start(d, root, &rg, false);
    rig_skip(d, &rg, "b0-r1", 0x7FA8FF, 1000);
    bc = rig_adopt(&rg, 0x001F, "b0-r1");
    run(d, &rg.f, 0.45, 1 / 60.0);
    CHECK(freed(rg.a) == 1, "swapped: A freed");
    rig_skip(d, &rg, "c0-r1", 0x7FFF7F, 1000);
    cc = rig_adopt(&rg, 0x07E0, "c0-r1");
    run(d, &rg.f, 1.0, 1 / 60.0);
    rgb(FX + 233, 150, &r, &g, &b);
    CHECK(g > 240 && freed(bc) == 1 && freed(cc) == 0, "a set after the swap runs its own fade: B freed %d", freed(bc));
    rig_end(root);

    rig_start(d, root, &rg, false);
    rig_skip(d, &rg, "b0-r1", 0x7FA8FF, 1000);
    bc = rig_adopt(&rg, 0x001F, "b0-r1");
    run(d, &rg.f, 0.1, 1 / 60.0);
    rig_skip(d, &rg, "", 0x7FA8FF, 500);
    CHECK(freed(bc) == 1 && disc() > 240, "a skip to a block without art drops the held cover, the old one stays: %d",
          freed(bc));
    run(d, &rg.f, NP_NOPIC_GRACE_S - 0.5, 1 / 30.0);
    CHECK(disc() > 240 && !npc_vinyl(), "within the grace no vinyl: Ember's art is on its way");
    run(d, &rg.f, 1.5, 1 / 30.0);
    CHECK(npc_vinyl() && v15_label() > 180 && freed(rg.a) == 0, "after %.0f s without a picture: M15, painted into A's buffer %d",
          NP_NOPIC_GRACE_S, freed(rg.a));
    rig_end(root);
    CHECK(freed(rg.a) == 1, "freed once on hide");

    rig_start(d, root, &rg, false);
    rig_skip(d, &rg, "b0-r1", 0x7FA8FF, 1000);
    bc = rig_adopt(&rg, 0x001F, "b0-r1");
    run(d, &rg.f, 0.27, 1 / 60.0);
    int step = luma(FX + 113, 233);
    rg.f.state = NPF_PAUSED;
    npc_update(&rg.f);
    refresh(d);
    int paused = luma(FX + 113, 233);
    rg.f.state = NPF_PLAYING;
    rg.f.ring = NP_RING_VOLUME;
    rg.f.vol = 50;
    npc_update(&rg.f);
    refresh(d);
    int vol = luma(FX + 113, 233);
    bool vring = ring_at(0.45f, &r, &g, &b);
    printf("np_view: mid-fade: step %d, paused %d, volume %d\n", step, paused, vol);
    CHECK(step < 240 && paused < step && vol < paused, "the fade stacks with the paused and volume shades: %d %d %d", step,
          paused, vol);
    CHECK(vring && r > 200 && g > 200, "volume mid-fade shows the level on the ring: %d,%d,%d", r, g, b);
    rg.f.ring = NP_RING_PROGRESS;
    run(d, &rg.f, 1.0, 1 / 60.0);
    rgb(FX + 233, 150, &r, &g, &b);
    CHECK(b > 240 && freed(rg.a) == 1, "the change still lands");
    rig_end(root);

    rig_start(d, root, &rg, false);
    rig_skip(d, &rg, "b0-r1", 0x7FA8FF, 1000);
    bc = rig_adopt(&rg, 0x001F, "b0-r1");
    run(d, &rg.f, 0.2, 1 / 60.0);
    np_art_set_t empty;
    rig_set(&empty, NULL, "b0-r1", false);
    npc_adopt(&empty);
    CHECK(freed(bc) == 1 && freed(rg.a) == 0, "an empty set mid-fade drops the held cover: %d", freed(bc));
    run(d, &rg.f, 0.3, 1 / 60.0);
    rig_end(root);
    CHECK(freed(rg.a) == 1, "A once, on hide");
}

static void test_details(lv_display_t *d, lv_obj_t *root)
{
    rig_t rg;
    int r, g, b;

    rig_start(d, root, &rg, true);
    int label = v15_label();
    uint8_t *av = track(make_avatar());
    np_art_set_t again;
    rig_set(&again, NULL, "a0-r1", true);
    again.avatar = av;
    npc_adopt(&again);
    int dark = 255, maxa = 0;
    for (int i = 0; i < 36; i++) {
        rg.f.now += 1 / 60.0;
        npc_update(&rg.f);
        refresh(d);
        int l = luma(FX + NP_V15_CX - 40, NP_V15_CY - 10);
        if (l < dark) dark = l;
        if (s_areas > maxa) maxa = s_areas;
    }
    rgb(FX + NP_V15_CX, NP_V15_CY, &r, &g, &b);
    printf("np_view: same-version no_cover republish: label min %d (was %d), at most %d areas, avatar %d,%d,%d\n", dark, label,
           maxa, r, g, b);
    CHECK(dark > 150 && maxa <= 2, "a set for the version on screen applies in place, no fade: label min %d", dark);
    CHECK(r > 230 && b < 160, "M15 shows the avatar on the label: %d,%d,%d", r, g, b);
    int text = ink(350, 425, 100, 366, 180), rec = luma(FX + NP_V15_CX, NP_V15_CY - 100);
    rg.f.state = NPF_PAUSED;
    npc_update(&rg.f);
    refresh(d);
    int rec_p = luma(FX + NP_V15_CX, NP_V15_CY - 100), lab_p = v15_label(), text_p = ink(350, 425, 100, 366, 180);
    printf("np_view: M15 paused: label %d -> %d, curved text %d -> %d px\n", label, lab_p, text, text_p);
    CHECK(lab_p < label - 60 && rec_p <= rec, "paused darkens the record");
    CHECK(abs(text_p - text) < 40, "the small shade leaves the curved text alone: %d vs %d", text_p, text);
    rig_end(root);
    CHECK(freed(av) == 1, "the avatar freed once");

    rig_start(d, root, &rg, false);
    rg.f.pos_ms = 134500;
    rg.f.dur_ms = 240000;
    npc_update(&rg.f);
    CHECK(strcmp(npc_time_text(), "2:14 / 4:00") == 0, "a new duration in the same second: %s", npc_time_text());
    rig_end(root);

    npc_build(root);
    np_info_t np = {.state = NP_PLAYING, .has_accent = true, .accent = 0xFF8A5C, .duration_ms = 228000};
    strcpy(np.cover_version, "a0-r1");
    np_art_set_t art;
    rig_set(&art, track(make_flat(0xFFFF)), "a0-r1", false);
    npc_adopt(&art);
    npc_frame_t f = {.now = 300, .state = NPF_PLAYING, .ring = NP_RING_PROGRESS, .np = &np, .text_fresh = true,
                     .pos_ms = 134000, .dur_ms = 228000, .vol = 50, .badge_text = ""};
    npc_update(&f);
    f.now += 0.1;
    f.ring = NP_RING_VOLUME;
    npc_update(&f);
    f.now += 0.05;
    f.ring = NP_RING_PROGRESS;
    npc_update(&f);
    refresh(d);
    CHECK(ring_at(0.55f, &r, &g, &b), "a turn ends the arrival: the ring is at the position at once");
    rig_end(root);

    npc_build(root);
    rig_set(&art, track(make_flat(0xFFFF)), "a0-r1", false);
    npc_adopt(&art);
    f.now = 400;
    f.ring = NP_RING_PROGRESS;
    f.text_fresh = true;
    npc_update(&f);
    f.now += 0.1;
    f.state = NPF_OFFLINE;
    npc_update(&f);
    refresh(d);
    CHECK(ring_at(0.55f, &r, &g, &b) && strcmp(npc_time_text(), "2:14 / 3:48") == 0,
          "offline ends the arrival on the last known position: %s", npc_time_text());
    rig_end(root);
}

static void test_no_cover_oom(lv_display_t *d, lv_obj_t *root)
{
    npc_build(root);
    np_info_t np = {.state = NP_PLAYING, .duration_ms = 228000};
    strcpy(np.title, "Bad Cover");
    strcpy(np.cover_version, "bad0-r1");
    np_art_set_t art = {.is_cover = true, .no_cover = true};
    strcpy(art.version, np.cover_version);
    s_no_face_buf = true;
    npc_adopt(&art);
    npc_frame_t f = {.now = 100, .state = NPF_PLAYING, .ring = NP_RING_PROGRESS, .np = &np, .text_fresh = true,
                     .pos_ms = 10000, .dur_ms = 228000, .vol = 40, .badge_text = ""};
    npc_update(&f);
    refresh(d);
    CHECK(npc_has_cover(), "no buffer yet: the no-cover look is still pending");
    s_no_face_buf = false;
    f.pos_ms = 11000;
    npc_update(&f);
    refresh(d);
    CHECK(v15_label() > 180, "the buffer comes later: M15's vinyl, not black: %d", v15_label());
    npc_destroy();
    lv_obj_clean(root);
}

static void test_page(lv_display_t *d, lv_obj_t *scr)
{
    s_forgets = 0;
    s_frees = 0;
    np_view_create(scr);
    np_view_show(true);
    np_info_t none = {.state = NP_NONE};
    publish(&none);
    frames(d, 3);
    s_pend = (np_art_set_t){0};
    s_has_pend = true;
    frames(d, 20);
    CHECK(s_takes == 1 && s_forgets == 0, "nothing playing: the empty set is taken once and asks for no refetch (%d forgets)",
          s_forgets);
    int r, g, b;
    rgb(FX + 233, 233 + NP_VINYL_DY - NP_VINYL_R, &r, &g, &b);
    CHECK(r > 30 && r < 60, "nothing playing on the cover face: vinyl %d", r);

    np_info_t np = {.state = NP_PLAYING, .has_accent = true, .accent = 0xFF8A5C, .duration_ms = 228000, .position_ms = 134000};
    strcpy(np.title, "Glass Harbor");
    strcpy(np.art_version, "fb43cf74b67a");
    strcpy(np.cover_version, "fb43cf74b67a-r1");
    np.album_art = true;
    publish(&np);
    frames(d, 2);
    s_pend = (np_art_set_t){.is_cover = true, .cover = make_cover()};
    strcpy(s_pend.version, "old0-r1");
    s_has_pend = true;
    frames(d, 2);
    CHECK(s_frees == 1 && luma(FX + 233, 150) < 10, "a set for an older cover_version is dropped");
    s_pend = (np_art_set_t){.is_cover = true, .cover = make_cover()};
    strcpy(s_pend.version, np.cover_version);
    s_has_pend = true;
    frames(d, 2);
    CHECK(s_frees == 1 && luma(FX + 233, 150) > 30, "the current cover is shown");
    rgb(FX + 233, 233 + NP_EDGE_R, &r, &g, &b);
    CHECK(r > 240 && g > 120 && g < 150, "accent ring: %d,%d,%d", r, g, b);

    np_info_t skip = np;
    strcpy(skip.title, "Paper Moons");
    skip.cover_version[0] = skip.art_version[0] = 0;
    skip.album_art = skip.artist_art = false;
    skip.has_accent = false;
    skip.position_ms = 0;
    publish(&skip);
    int forgets0 = s_forgets, label_max = 0;
    for (int i = 0; i < 5; i++) {
        frames(d, 1);
        int l = v15_label();
        if (l > label_max) label_max = l;
    }
    CHECK(s_forgets == forgets0 && luma(FX + 233, 150) > 30, "a real skip: the art-less block keeps the cover face and cover");
    strcpy(skip.cover_version, "b1-r1");
    strcpy(skip.art_version, "b1");
    skip.album_art = true;
    skip.has_accent = true;
    skip.accent = 0x7FA8FF;
    publish(&skip);
    frames(d, 2);
    uint16_t *bcov = track(make_flat(0x001F));
    s_pend = (np_art_set_t){.is_cover = true, .cover = bcov};
    strcpy(s_pend.version, "b1-r1");
    s_has_pend = true;
    for (int i = 0; i < 12; i++) {
        frames(d, 1);
        int l = v15_label();
        if (l > label_max) label_max = l;
    }
    rgb(FX + 233, 150, &r, &g, &b);
    CHECK(b > 240 && label_max < 150 && s_forgets == forgets0, "then its cover fades in, never through the vinyl: label max %d",
          label_max);

    np_info_t bare = {.state = NP_PLAYING, .duration_ms = 160000, .position_ms = 62000};
    strcpy(bare.title, "Untitled demo");
    strcpy(bare.artist, "Home recording");
    publish(&bare);
    frames(d, 65);
    save("m15-no-picture");
    int label = luma(FX + NP_V15_CX - 30, NP_V15_CY + 30);
    rgb(FX + 233 + NP_EDGE_R, 233, &r, &g, &b);
    printf("np_view: M15 for a track with no picture: label %d, ring %d,%d,%d\n", label, r, g, b);
    CHECK(label > 180, "a track that stays without a picture: M15 after the grace: %d", label);
    CHECK(r > 240 && g > 160 && g < 200 && b < 100, "the ring in the vinyl amber: %d,%d,%d", r, g, b);
    long spin_px = 0;
    int spin = 0, spin_max = 0;
    for (int i = 0; i < 60; i++) {
        s_now_us += 1000000 / 60;
        np_view_update(s_now_us / 1e6, false);
        refresh(d);
        spin += s_areas;
        spin_px += s_px;
        if (s_areas > spin_max) spin_max = s_areas;
    }
    printf("np_view: M15 playing, 1 s: %d areas, %ld px (label at %d fps, ring and time once)\n", spin, spin_px, NP_V15_FPS);
    CHECK(spin >= 4 && spin <= 7 && spin_max <= 3, "M15: the label 4 times a second plus the ring and time: %d areas", spin);
    np_view_show(false);
}

static void test_recheck(lv_display_t *d, lv_obj_t *root)
{
    rig_t rg;
    int r, g, b;

    rig_start(d, root, &rg, true);
    np_art_set_t again;
    rig_set(&again, NULL, "a0-r1", true);
    again.avatar = track(make_avatar());
    npc_adopt(&again);
    run(d, &rg.f, 0.2, 1 / 60.0);
    rgb(FX + NP_V15_CX, NP_V15_CY, &r, &g, &b);
    CHECK(r > 230, "a given-up cover's M15 with its avatar: %d", r);
    rg.np.has_accent = false;
    rig_skip(d, &rg, "", 0, 1000);
    run(d, &rg.f, NP_NOPIC_GRACE_S + 0.5, 1 / 30.0);
    rgb(FX + NP_V15_CX, NP_V15_CY, &r, &g, &b);
    int ar, ag, ab;
    rgb(FX + NP_V15_CX + 46, NP_V15_CY, &ar, &ag, &ab);
    printf("np_view: art-less track after a given-up one: label centre %d,%d,%d, label ring %d,%d,%d\n", r, g, b, ar, ag, ab);
    CHECK(r < 100 && freed(again.avatar) <= 1, "after the grace the old track's avatar is gone: %d", r);
    CHECK(ag > 170, "and the vinyl takes the new block's accent (amber without one): %d,%d,%d", ar, ag, ab);
    rig_end(root);

    rig_start(d, root, &rg, false);
    rg.f.ring = NP_RING_VOLUME;
    rg.f.vol = 90;
    rg.f.now += 0.1;
    npc_update(&rg.f);
    refresh(d);
    rig_skip(d, &rg, "b0-r1", 0x7FA8FF, 1000);
    rg.f.ring = NP_RING_PROGRESS;
    run(d, &rg.f, 0.5, 1 / 60.0);
    bool high = ring_at(0.8f, &r, &g, &b), mid = ring_at(0.4f, &r, &g, &b);
    CHECK(!high && mid, "a skip while turning holds the last progress (0.59), not the volume (0.90): 0.8 %d, 0.4 %d", high, mid);
    rig_end(root);

    rig_start(d, root, &rg, false);
    rg.np.cover_light = true;
    rg.f.now += 0.1;
    npc_update(&rg.f);
    refresh(d);
    int strong = luma(FX + 73, 300);
    rg.np.cover_light = false;
    rig_skip(d, &rg, "b0-r1", 0x7FA8FF, 1000);
    run(d, &rg.f, 0.5, 1 / 60.0);
    int loading = luma(FX + 73, 300);
    printf("np_view: skip from a light cover: band %d, while the next loads %d\n", strong, loading);
    CHECK(strong < 80 && abs(loading - strong) <= 4, "the light cover keeps its strong scrim until the swap: %d vs %d", loading,
          strong);
    rig_end(root);

    rig_start(d, root, &rg, false);
    rig_skip(d, &rg, "", 0x7FA8FF, 1000);
    run(d, &rg.f, NP_NOPIC_GRACE_S + 0.1, 1 / 30.0);
    rig_skip(d, &rg, "b0-r1", 0x7FA8FF, 1100);
    int vinyl_seen = 0;
    for (int i = 0; i < 30; i++) {
        run(d, &rg.f, 1 / 60.0, 1 / 60.0);
        vinyl_seen += npc_vinyl();
    }
    CHECK(!npc_vinyl() && disc() > 240, "a cover_version back before the swap cancels the armed vinyl: the old cover stays");
    uint16_t *bc = rig_adopt(&rg, 0x001F, "b0-r1");
    for (int i = 0; i < 60; i++) {
        run(d, &rg.f, 1 / 60.0, 1 / 60.0);
        vinyl_seen += npc_vinyl();
    }
    rgb(FX + 233, 150, &r, &g, &b);
    CHECK(b > 240 && !vinyl_seen && freed(rg.a) == 1 && freed(bc) == 0, "then A fades to B, never the vinyl: %d frames", vinyl_seen);
    rig_end(root);
}

static void test_boot(lv_display_t *d, lv_obj_t *scr)
{
    np_view_create(scr);
    np_view_show(true);
    int f0 = s_forgets, label_max = 0;
    np_info_t none = {.state = NP_NONE};
    publish(&none);
    frames(d, 3);
    for (int round = 0; round < 2; round++) {
        np_info_t np = {.state = NP_PLAYING, .duration_ms = 228000, .position_ms = 0};
        snprintf(np.title, sizeof np.title, "Session %d", round);
        publish(&np);
        for (int i = 0; i < 5; i++) {
            frames(d, 1);
            if (v15_label() > label_max) label_max = v15_label();
        }
        CHECK(!npc_vinyl(), "a session's first, art-less block: no vinyl within the grace (round %d)", round);
        snprintf(np.cover_version, sizeof np.cover_version, "s%d-r1", round);
        snprintf(np.art_version, sizeof np.art_version, "s%d", round);
        np.album_art = true;
        np.has_accent = true;
        np.accent = 0x7FA8FF;
        np.position_ms = 500;
        publish(&np);
        frames(d, 2);
        s_pend = (np_art_set_t){.is_cover = true, .cover = make_flat(0x001F)};
        strcpy(s_pend.version, np.cover_version);
        s_has_pend = true;
        for (int i = 0; i < 12; i++) {
            frames(d, 1);
            if (v15_label() > label_max) label_max = v15_label();
        }
        int r, g, b;
        rgb(FX + 233, 150, &r, &g, &b);
        CHECK(b > 240, "then the cover (round %d): %d,%d,%d", round, r, g, b);
        publish(&none);
        frames(d, 3);
    }
    printf("np_view: fresh page, nothing -> art-less -> cover twice: label max %d, %d forgets\n", label_max, s_forgets - f0);
    CHECK(label_max < 150, "starting playback never flashes the vinyl: %d", label_max);
    CHECK(s_forgets - f0 == 2, "a fresh page never flips the face: only the two idles forget (%d)", s_forgets - f0);

    np_info_t bare = {.state = NP_PLAYING, .duration_ms = 160000, .position_ms = 1000};
    strcpy(bare.title, "Untitled demo");
    publish(&bare);
    frames(d, 3);
    CHECK(!npc_vinyl(), "a track without a picture right after a fresh start: grace first");
    frames(d, 50);
    CHECK(npc_vinyl() && v15_label() > 180, "then M15 on the cover face, without any art seen before");

    int f1 = s_forgets;
    np_info_t old = {.state = NP_PLAYING, .duration_ms = 228000, .position_ms = 134000, .album_art = true};
    strcpy(old.title, "Glass Harbor");
    strcpy(old.art_version, "fb43cf74b67a");
    publish(&old);
    frames(d, 3);
    CHECK(luma(FX + 233 - 198, 233) > 30 && luma(FX + 233 - NP_EDGE_R, 233) < 10 && s_forgets - f1 == 1,
          "switched to an older Ember (art, no cover_version): the old face at once (%d forgets)", s_forgets - f1);
    np_view_show(false);
}

static void test_old_ember(lv_display_t *d, lv_obj_t *scr)
{
    np_view_create(scr);
    np_view_show(true);
    np_info_t np = {.state = NP_PLAYING, .duration_ms = 228000, .position_ms = 134000, .album_art = true};
    strcpy(np.title, "Glass Harbor");
    strcpy(np.art_version, "fb43cf74b67a");
    publish(&np);
    frames(d, 3);
    int r, g, b, forgets = s_forgets;
    rgb(FX + 233, 233 + 198, &r, &g, &b);
    CHECK(r > 100, "an older Ember: the old face's ring at r 198: %d,%d,%d", r, g, b);
    np_info_t skip = np;
    strcpy(skip.title, "Paper Moons");
    skip.art_version[0] = 0;
    skip.album_art = false;
    skip.position_ms = 0;
    publish(&skip);
    frames(d, 3);
    strcpy(skip.art_version, "b1");
    skip.album_art = true;
    publish(&skip);
    frames(d, 3);
    CHECK(s_forgets == forgets && luma(FX + 233 - 198, 233) > 30 && luma(FX + 233 - NP_EDGE_R, 233) < 10, "an older Ember's skip (art-less, then art) never leaves the "
          "old face: %d forgets", s_forgets - forgets);
    s_pend = (np_art_set_t){.is_cover = true, .cover = make_cover()};
    strcpy(s_pend.version, "fb43cf74b67a-r1");
    s_has_pend = true;
    int frees = s_frees, reoffers = s_reoffers;
    frames(d, 5);
    CHECK(s_frees == frees + 1 && s_forgets == forgets && s_reoffers == reoffers, "a cover set on the old face is dropped, no refetch loop");
    np_view_show(false);
}

static void pend(const char *version, bool no_cover, bool avatar, bool cover)
{
    s_pend = (np_art_set_t){.is_cover = true, .no_cover = no_cover};
    if (avatar) s_pend.avatar = make_avatar();
    if (cover) s_pend.cover = make_cover();
    strcpy(s_pend.version, version);
    s_has_pend = true;
}

static void test_dropped_set(lv_display_t *d, lv_obj_t *scr)
{
    np_view_create(scr);
    np_view_show(true);
    np_info_t v = {.state = NP_PLAYING, .duration_ms = 228000, .position_ms = 1000, .album_art = true};
    strcpy(v.title, "Held");
    strcpy(v.art_version, "a1b2c3d4e5f6");
    strcpy(v.cover_version, "held0-r1");
    np_info_t x = v;
    strcpy(x.title, "Other");
    strcpy(x.cover_version, "other0-r1");
    publish(&v);
    frames(d, 3);
    int f0 = s_forgets, r0 = s_reoffers;
    publish(&x);
    frames(d, 1);
    pend(v.cover_version, true, false, false);
    frames(d, 3);
    CHECK(!s_has_pend && s_reoffers == r0, "V's held no_cover set while X shows: dropped, no forget yet (%d)", s_reoffers - r0);
    frames(d, 20);
    CHECK(s_reoffers == r0, "X stays: no forget for V's dropped set (%d)", s_reoffers - r0);
    publish(&v);
    frames(d, 3);
    CHECK(s_reoffers == r0 + 1 && strcmp(s_reoffer_v, v.cover_version) == 0,
          "back to V: one conditional forget for V so the client hands it over again (%d, %s)", s_reoffers - r0, s_reoffer_v);
    pend(v.cover_version, true, false, false);
    frames(d, 20);
    CHECK(!s_has_pend && npc_has_cover() && s_reoffers == r0 + 1, "the republished set is shown, no further forget (%d)",
          s_reoffers - r0);
    for (int i = 0; i < 5; i++) {
        pend(v.cover_version, true, false, false);
        frames(d, 4);
    }
    CHECK(s_reoffers == r0 + 1, "sets for the version on screen: never a forget (%d)", s_reoffers - r0);

    int r1 = s_reoffers;
    publish(&x);
    frames(d, 1);
    pend(v.cover_version, true, false, false);
    frames(d, 3);
    publish(&v);
    pend(v.cover_version, true, true, false);
    frames(d, 20);
    CHECK(!s_has_pend && s_reoffers == r1, "back to V with V's finished set already pending: adopted, no forget after it (%d)",
          s_reoffers - r1);

    publish(&x);
    frames(d, 1);
    pend(v.cover_version, false, true, true);
    frames(d, 3);
    CHECK(s_reoffers == r1, "V's cover set while X shows: dropped (%d)", s_reoffers - r1);
    publish(&v);
    frames(d, 10);
    CHECK(s_reoffers == r1 + 1, "U -> V -> X -> V: back to V after its finished cover was dropped, one forget (%d)",
          s_reoffers - r1);
    publish(&x);
    frames(d, 1);
    pend(v.cover_version, true, true, false);
    frames(d, 3);
    publish(&v);
    frames(d, 10);
    CHECK(s_reoffers == r1 + 2, "and after its finished no_cover set with the avatar was dropped (%d)", s_reoffers - r1);

    int r2 = s_reoffers;
    s_pend = (np_art_set_t){0};
    s_has_pend = true;
    frames(d, 20);
    np_info_t none = {.state = NP_NONE};
    publish(&none);
    frames(d, 3);
    s_pend = (np_art_set_t){0};
    s_has_pend = true;
    frames(d, 3);
    publish(&v);
    frames(d, 20);
    CHECK(s_reoffers == r2, "an empty set dropped, on screen or around idle: never a forget (%d)", s_reoffers - r2);
    CHECK(s_forgets == f0 + 1, "only the idle's own forget (%d)", s_forgets - f0);

    publish(&x);
    frames(d, 1);
    pend(v.cover_version, true, false, false);
    frames(d, 3);
    np_view_show(false);
    np_view_show(true);
    publish(&v);
    int r3 = s_reoffers;
    frames(d, 10);
    CHECK(s_reoffers == r3, "a hide forgets already: no second forget after it (%d)", s_reoffers - r3);
    np_view_show(false);
}

int main(int argc, char **argv)
{
    s_dir = argc > 1 ? argv[1] : NULL;
    lv_init();
    lv_tick_set_cb(tick_ms);
    lv_display_t *d = lv_display_create(W, H);
    static uint16_t buf[W * 60];
    lv_display_set_buffers(d, buf, NULL, sizeof buf, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(d, flush_cb);
    lv_display_add_event_cb(d, render_cb, LV_EVENT_RENDER_START, NULL);
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_t *root = lv_obj_create(scr);
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, W, H);
    CHECK(npc_init(), "init");
    npc_build(root);

    np_info_t np = {.state = NP_PLAYING, .has_accent = true, .accent = 0xFF8A5C, .duration_ms = 228000};
    strcpy(np.title, "Glass Harbor");
    strcpy(np.artist, "Northern Static");
    strcpy(np.cover_version, "fb43cf74b67a-r1");
    np_art_set_t art = {.is_cover = true, .cover = make_cover(), .avatar = make_avatar()};
    strcpy(art.version, np.cover_version);
    npc_adopt(&art);
    CHECK(!art.cover && !art.avatar && npc_has_cover(), "the view takes the pictures");
    npc_frame_t f = {.now = 100, .state = NPF_PLAYING, .ring = NP_RING_PROGRESS, .np = &np, .text_fresh = true,
                     .pos_ms = 134000, .dur_ms = 228000, .vol = 40, .badge_text = ""};
    npc_update(&f);
    refresh(d);
    save("m1-arrival-start");
    CHECK(strcmp(npc_time_text(), "0:00 / 3:48") == 0, "the arrival counts up from 0: %s", npc_time_text());
    int r, g, b;
    rgb(FX + 233 + 40, 233 - NP_EDGE_R + 2, &r, &g, &b);
    CHECK(r < 60, "the ring starts empty: %d,%d,%d", r, g, b);
    run_t ar = run(d, &f, NP_ARRIVE_S + 0.2, 1 / 60.0);
    printf("np_view: arrival sweep: %d frames drawn, at most %d areas and %ld px a frame, %ld px in all\n", ar.frames,
           ar.max_areas, ar.max_px, ar.px);
    CHECK(ar.frames >= 30 && ar.frames <= 42 && ar.max_areas <= 2 && ar.full == 0, "arrival: %d frames, %d areas, %d full",
          ar.frames, ar.max_areas, ar.full);
    CHECK(strcmp(npc_time_text(), "2:14 / 3:48") == 0, "counted up to the position: %s", npc_time_text());
    f.now += 1;
    npc_update(&f);
    refresh(d);
    CHECK(s_areas == 0, "then still: %d areas", s_areas);
    save("m1-playing");
    rgb(FX + 233, 233 + NP_EDGE_R, &r, &g, &b);
    CHECK(r > 240 && g > 120 && g < 150 && b < 110, "accent arc at 6 o'clock: %d,%d,%d", r, g, b);
    rgb(FX + 233 - NP_EDGE_R, 233, &r, &g, &b);
    CHECK(r < 50 && g < 50 && b < 50 && r > 25, "track at 9 o'clock: %d,%d,%d", r, g, b);
    int text = 0;
    for (int y = 285; y < 316; y++)
        for (int x = FX + 140; x < FX + 326; x++) text += luma(x, y) > 200;
    CHECK(text > 300, "title drawn: %d px", text);
    int lum_play = luma(FX + 233, 150);

    f.text_fresh = false;
    f.pos_ms = 135000;
    npc_update(&f);
    refresh(d);
    CHECK(s_areas <= 2, "a second of playing: %d areas, %ld px", s_areas, s_px);
    printf("np_view: playing second: %d areas, %ld px\n", s_areas, s_px);

    f.ring = NP_RING_VOLUME;
    f.vol = 62;
    f.badge = NPC_BADGE_NUMBER;
    f.badge_text = "62";
    npc_update(&f);
    refresh(d);
    printf("np_view: into volume mode: %d areas, %ld px\n", s_areas, s_px);
    save("m7-volume");
    CHECK(luma(FX + 233, 150) < lum_play * 0.7, "volume dims the cover");
    rgb(FX + 233 + 30, 233 - NP_EDGE_R, &r, &g, &b);
    CHECK(r > 240 && g > 230, "volume ring starts white: %d,%d,%d", r, g, b);
    f.vol = 64;
    f.badge_text = "64";
    npc_update(&f);
    refresh(d);
    CHECK(s_areas <= 2, "a detent: %d areas, %ld px", s_areas, s_px);
    printf("np_view: volume detent: %d areas, %ld px\n", s_areas, s_px);
    f.ring = NP_RING_PROGRESS;
    f.badge = NPC_BADGE_NONE;
    f.badge_text = "";
    npc_update(&f);
    refresh(d);
    printf("np_view: back to progress: %d areas, %ld px\n", s_areas, s_px);
    CHECK(abs(luma(FX + 233, 150) - lum_play) <= 2, "back to the full cover");

    f.state = NPF_PAUSED;
    npc_update(&f);
    refresh(d);
    save("m2-paused");
    CHECK(luma(FX + 233, 150) < lum_play * 0.8, "paused darkens the cover");
    rgb(FX + 233, 233 + NP_EDGE_R, &r, &g, &b);
    CHECK(abs(r - 0x6A) < 8 && abs(g - 0x6A) < 8, "grey ring: %d,%d,%d", r, g, b);
    CHECK(luma(FX + 233 - 12, 168) > 200, "pause bar");
    rgb(FX + 233, 233 + 21, &r, &g, &b);
    CHECK(abs(r - g) < 12 && abs(g - b) < 12 && r < 200, "paused: the avatar goes grey: %d,%d,%d", r, g, b);
    npc_update(&f);
    refresh(d);
    CHECK(s_areas == 0, "paused: still (%d areas)", s_areas);

    f.state = NPF_OFFLINE;
    npc_update(&f);
    refresh(d);
    save("m10-offline");
    rgb(FX + 233, 233 + NP_EDGE_R, &r, &g, &b);
    CHECK(abs(r - 0x5A) < 8, "offline ring: %d,%d,%d", r, g, b);
    rgb(FX + 233, 106, &r, &g, &b);
    CHECK(r < 20 && g < 20, "badge box: %d,%d,%d", r, g, b);
    f.pos_ms = 150000;
    npc_update(&f);
    refresh(d);
    CHECK(s_areas == 0, "offline: frozen (%d areas)", s_areas);
    rgb(FX + 233, 233 + 21, &r, &g, &b);
    CHECK(abs(r - g) < 12 && abs(g - b) < 12, "offline: grey avatar: %d,%d,%d", r, g, b);

    f.state = NPF_PLAYING;
    f.pos_ms = 150000;
    npc_update(&f);
    refresh(d);
    rgb(FX + 233, 233 + 21, &r, &g, &b);
    CHECK(r > 230 && b < 160, "playing: the avatar in colour: %d,%d,%d", r, g, b);
    CHECK(strcmp(npc_time_text(), "2:30 / 3:48") == 0, "playing at 2:30: %s", npc_time_text());
    f.state = NPF_OFFLINE;
    f.pos_ms = 152000;
    npc_update(&f);
    refresh(d);
    CHECK(strcmp(npc_time_text(), "2:30 / 3:48") == 0, "offline freezes the last shown second: %s", npc_time_text());
    float hx, hy;
    np_ring_point(&(np_ring_t){.cx = 233, .cy = 233, .r = NP_EDGE_R, .hw = NP_EDGE_HW}, 150.0f / 228, &hx, &hy);
    CHECK(luma(FX + (int)hx, (int)hy) > 200, "the head stays at 2:30");

    f.state = NPF_NOTHING;
    npc_update(&f);
    refresh(d);
    save("m3-nothing");
    CHECK(!npc_has_cover() && s_forgets == 1, "the cover is dropped and refetched later");
    rgb(FX + 233, 233 + NP_VINYL_DY - NP_VINYL_R, &r, &g, &b);
    CHECK(abs(r - g) < 6 && r > 30 && r < 60, "vinyl rim: %d,%d", r, g);
    CHECK(luma(FX + 233, 233 + NP_EDGE_R) == 0, "no ring");
    f.state = NPF_NOTHING;
    npc_update(&f);
    refresh(d);
    CHECK(s_areas == 0, "nothing playing: still");

    npc_destroy();
    lv_obj_clean(root);
    test_scrim(d, root);
    test_no_cover_oom(d, root);
    test_change(d, root);
    test_interrupt(d, root);
    test_details(d, root);
    test_recheck(d, root);
    test_page(d, scr);
    test_boot(d, scr);
    test_old_ember(d, scr);
    test_dropped_set(d, scr);
    if (failures) {
        printf("np_view: %d failure(s)\n", failures);
        return 1;
    }
    printf("np_view: all tests passed\n");
    return 0;
}
