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

void np_client_forget_art(void) { s_forgets++; }
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

static void test_page(lv_display_t *d, lv_obj_t *scr)
{
    s_forgets = 0;
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

    np.cover_version[0] = 0;
    publish(&np);
    frames(d, 2);
    s_pend = (np_art_set_t){.is_cover = true, .cover = make_cover()};
    strcpy(s_pend.version, "fb43cf74b67a-r1");
    s_has_pend = true;
    int forgets = s_forgets;
    frames(d, 5);
    CHECK(s_frees == 2 && s_forgets == forgets, "an older Ember: a cover set is dropped, no refetch loop");
    rgb(FX + 233, 233 + 198, &r, &g, &b);
    CHECK(r > 100, "the old face's ring at r 198: %d,%d,%d", r, g, b);
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
    npc_adopt(&art);
    CHECK(!art.cover && !art.avatar && npc_has_cover(), "the view takes the pictures");
    npc_frame_t f = {.now = 100, .state = NPF_PLAYING, .ring = NP_RING_PROGRESS, .np = &np, .text_fresh = true,
                     .pos_ms = 134000, .dur_ms = 228000, .vol = 40, .badge_text = ""};
    npc_update(&f);
    refresh(d);
    save("m1-playing");
    int r, g, b;
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
    test_page(d, scr);
    if (failures) {
        printf("np_view: %d failure(s)\n", failures);
        return 1;
    }
    printf("np_view: all tests passed\n");
    return 0;
}
