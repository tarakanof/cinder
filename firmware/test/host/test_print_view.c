#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "lvgl.h"
#include "page_canvas.h"
#include "page_frame.h"
#include "page_ops.h"
#include "pomo_client.h"
#include "print_client.h"
#include "print_view.h"
#include "src/display/lv_display_private.h"

#define W 472
#define H 466
#define FX 3

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static uint16_t s_fb[W * H];
static int s_areas, s_px;
static const char *s_dir;
static print_snapshot_t s_snap;
static print_pics_t s_pics;
static bool s_has_pics, s_visible;
static double s_now;

double pomo_client_now(void) { return s_now; }
void print_client_get(print_snapshot_t *out) { *out = s_snap; }
void print_client_set_visible(bool on) { s_visible = on; }
bool print_client_take_pics(print_pics_t *out)
{
    if (!s_has_pics) return false;
    *out = s_pics;
    s_has_pics = false;
    return true;
}
static int s_dropped, s_pics_freed;
static size_t s_allocs[64];
static int s_n_allocs;
int host_heap_fail(size_t n)
{
    if (s_n_allocs < 64) s_allocs[s_n_allocs++] = n;
    return 0;
}
static bool allocated(size_t n, int from)
{
    for (int i = from; i < s_n_allocs; i++)
        if (s_allocs[i] == n) return true;
    return false;
}
void print_client_pics_dropped(void) { s_dropped++; }
void print_pics_free(print_pics_t *p)
{
    s_pics_freed += p->ghost != NULL;
    free(p->ghost);
    free(p->solid);
    memset(p, 0, sizeof *p);
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
    for (uint32_t i = 0; i < d->inv_p; i++)
        if (!d->inv_area_joined[i]) {
            s_areas++;
            s_px += lv_area_get_size(&d->inv_areas[i]);
            if (getenv("PV_AREAS"))
                printf("  area %d,%d-%d,%d\n", d->inv_areas[i].x1, d->inv_areas[i].y1, d->inv_areas[i].x2, d->inv_areas[i].y2);
        }
}

static lv_display_t *s_disp;

static bool s_offline;

static void frame(double t)
{
    page_frame_t f = {.t = t, .dt = 1.0 / 60, .offline = s_offline};
    s_now = t;
    page_print_frame(&f);
}

static void refresh(void)
{
    s_areas = s_px = 0;
    lv_refr_now(s_disp);
}

static void settle(double t)
{
    frame(t);
    refresh();
}

static void save(const char *name)
{
    if (!s_dir) return;
    char path[512];
    snprintf(path, sizeof path, "%s/print-%s.ppm", s_dir, name);
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (int i = 0; i < W * H; i++) {
        uint16_t p = s_fb[i];
        uint8_t rgb[3] = {(uint8_t)((p >> 11) * 255 / 31), (uint8_t)(((p >> 5) & 63) * 255 / 63), (uint8_t)((p & 31) * 255 / 31)};
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}

static uint16_t c565(uint32_t c) { return (uint16_t)((((c >> 16) & 0xFF) >> 3) << 11 | (((c >> 8) & 0xFF) >> 2) << 5 | ((c & 0xFF) >> 3)); }

static int count(int x0, int y0, int x1, int y1, uint16_t want)
{
    int n = 0;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) n += s_fb[y * W + x] == want;
    return n;
}

static int ink(int x0, int y0, int x1, int y1)
{
    int n = 0;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) n += s_fb[y * W + x] != 0;
    return n;
}

#define T0 1782043200.0

static void block(print_phase_t ph, int progress)
{
    memset(&s_snap, 0, sizeof s_snap);
    s_snap.fed = s_snap.has = true;
    s_snap.have_srv = s_snap.has_utc_offset = true;
    s_snap.utc_offset = 3600;
    print_block_t *b = &s_snap.b;
    b->phase = ph;
    b->has_progress = true;
    b->progress = progress;
    b->ends_at = (long long)T0 + 6120;
    b->started_at = (long long)T0 - 1800;
    strcpy(b->name, "bracket_v3");
    strcpy(b->material, "PETG");
    strcpy(b->printer, "CORE One");
    strcpy(b->thumb_version, "5d73e113fbb7-r1");
    b->thumb_top = 110;
    b->thumb_bottom = 300;
}

static uint32_t s_solid_col = 0x30A0E0;

static void make_pics(void)
{
    int rows = 190;
    s_pics = (print_pics_t){.top = 110, .bottom = 300};
    strcpy(s_pics.version, "5d73e113fbb7-r1");
    s_pics.ghost = calloc((size_t)PRINT_PIC_W * rows, 2);
    s_pics.solid = calloc((size_t)PRINT_PIC_W * rows, 2);
    for (int y = 0; y < rows; y++) {
        float k = (float)y / rows;
        int half = (int)(40 + 30 * sinf(k * 3.1f));
        for (int x = PRINT_PIC_W / 2 - half; x < PRINT_PIC_W / 2 + half; x++) {
            s_pics.ghost[y * PRINT_PIC_W + x] = c565(0x404040);
            s_pics.solid[y * PRINT_PIC_W + x] = c565(s_solid_col);
        }
    }
    s_has_pics = true;
}

static void input(int detents)
{
    page_input_t in = {.detents = detents};
    page_print_input(&in, s_now);
}

int main(int argc, char **argv)
{
    s_dir = argc > 1 ? argv[1] : NULL;
    lv_init();
    lv_tick_set_cb(tick_ms);
    s_disp = lv_display_create(W, H);
    static uint16_t buf[W * 60];
    lv_display_set_buffers(s_disp, buf, NULL, sizeof buf, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(s_disp, flush_cb);
    lv_display_add_event_cb(s_disp, render_cb, LV_EVENT_RENDER_START, NULL);
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    static uint16_t canvas[PAGE_CANVAS_BYTES / 2];
    page_canvas_init(canvas, sizeof canvas);

    print_view_create(scr);
    page_print_show(true);
    CHECK(s_visible && page_canvas_owner() == PAGE_CANVAS_PRINT, "shown: visible to the client, holds the canvas");

    settle(T0);
    save("connecting");
    CHECK(ink(0, 0, W, H) > 500, "before the first view: Connecting");
    s_snap.fed = true;
    settle(T0);
    save("none");
    CHECK(ink(0, 0, W, H) > 500, "no block: something on screen");
    CHECK(count(0, 0, W, H, c565(PRINT_COL_ACCENT)) > 500 && count(0, 0, W, H, c565(PRINT_COL_FIL)) == 0,
          "no block: the drawn printer, no part");

    block(PRINT_PRINTING, 24);
    settle(T0);
    save("k1-printing");
    CHECK(count(FX + 233 - 10, 0, FX + 233 + 10, 20, 0) < 400, "the ring starts at 12 o'clock");
    CHECK(count(0, 0, W, H, c565(PRINT_COL_FIL)) > 50, "K1 printing: the part in the printer");
    CHECK(ink(80, 210, 390, 300) > 2000 && ink(80, 320, 390, 350) > 500 && ink(80, 360, 390, 385) > 300,
          "K1: value, ready time and file line");
    refresh();
    CHECK(s_areas == 0, "nothing new, nothing drawn: %d", s_areas);
    frame(T0 + 0.01);
    refresh();
    CHECK(s_areas == 0, "inside the ambient step: %d areas", s_areas);
    frame(T0 + 0.2);
    refresh();
    CHECK(s_areas == 1 && s_px < 4000, "an ambient step: one head box, %d areas, %d px", s_areas, s_px);
    frame(T0 + 0.25);
    refresh();
    s_snap.b.progress = 25;
    frame(T0 + 0.26);
    refresh();
    CHECK(s_areas <= 2 && s_px < 40000, "a percent: the ring step and the hero, %d areas, %d px", s_areas, s_px);
    double tick = T0 + 6120 - 6061;
    frame(tick);
    refresh();
    frame(tick + 0.02);
    refresh();
    CHECK(s_areas == 0, "same minute: %d", s_areas);
    frame(tick + 1);
    refresh();
    CHECK(s_areas >= 1, "the minute ticks without a view change: %d", s_areas);

    strcpy(s_snap.b.name, "Desk lamp bracket v3 with clipX");
    strcpy(s_snap.b.material, "PLA/PETG");
    settle(T0);
    save("k1-long-name");
    CHECK(ink(156, 388, 316, 425) == 0, "a 31-character name stays on its line: %d px below it", ink(156, 388, 316, 425));
    CHECK(count(0, 352, W, 386, c565(PRINT_COL_SMALL)) > 0 || ink(60, 352, W - 60, 386) > 300, "and is drawn");
    strcpy(s_snap.b.name, "abcdefghijklmnopqrstuvwxyzabcdef");
    settle(T0 + 0.01);
    CHECK(ink(156, 388, 316, 425) == 0, "32 characters without a space: still one line");
    s_offline = true;
    settle(T0 + 0.02);
    save("k1-offline-link");
    CHECK(ink(156, 352, 316, 425) == 0, "offline: the file line goes");
    s_offline = false;
    block(PRINT_OFFLINE, 0);
    s_snap.b.has_progress = false;
    strcpy(s_snap.b.printer, "Prusa CORE One in the workshop");
    settle(T0 + 0.03);
    save("k1-offline-long-label");
    CHECK(ink(156, 352, 316, 425) == 0, "a 30-character printer label stays on the sub-line");

    block(PRINT_HEATING, 0);
    s_snap.b.has_temps = true;
    s_snap.b.nozzle = 182;
    s_snap.b.nozzle_target = 240;
    s_snap.b.bed = 72;
    s_snap.b.bed_target = 85;
    s_snap.b.thumb_version[0] = 0;
    settle(T0);
    save("k1-heating");
    CHECK(count(0, 0, W, 240, c565(PRINT_COL_HEAT)) > 500, "heating: a red ring");

    block(PRINT_ATTENTION, 46);
    settle(T0);
    save("k1-attention");
    CHECK(count(0, 0, W, H, c565(PRINT_COL_WAIT)) > 300, "attention: amber");
    frame(T0 + 0.1);
    refresh();
    CHECK(s_areas == 1, "attention pulses in the hero box only: %d areas, %d px", s_areas, s_px);

    s_snap.b.thumb_version[0] = 0;
    s_snap.b.thumb_top = s_snap.b.thumb_bottom = 0;
    settle(T0 + 0.5);
    frame(T0 + 0.6);
    refresh();
    print_rect_t k1 = print_hero_box(&(print_hero_t){233 + FX, 122, 0.95f}), k2 = print_hero_box(&(print_hero_t){233 + FX, 205, 1.6f});
    CHECK(allocated((size_t)k1.w * k1.h * 2, 0), "the K1 pulse pair is sized for K1's hero");
    int mark = s_n_allocs;
    input(1);
    for (int i = 1; i <= 4; i++) {
        frame(T0 + 0.6 + i * 0.1);
        refresh();
    }
    CHECK(s_areas == 1, "attention on K2 without a picture pulses its larger hero: %d areas", s_areas);
    CHECK(allocated((size_t)k2.w * k2.h * 2, mark), "a turn to K2 reallocates the pulse pair for its larger hero");
    input(-1);
    settle(T0 + 2);
    frame(T0 + 2.1);
    refresh();
    CHECK(s_areas == 1, "and back on K1: %d areas", s_areas);

    block(PRINT_HEATING, 0);
    s_snap.b.has_temps = true;
    s_snap.b.nozzle = 182;
    s_snap.b.nozzle_target = 240;
    s_snap.b.bed = 72;
    s_snap.b.bed_target = 85;
    s_snap.b.thumb_version[0] = 0;
    settle(T0);
    s_snap.b.nozzle = 183;
    frame(T0 + 0.01);
    refresh();
    CHECK(s_areas <= 3, "a heat degree: the ring step, the bed and nozzle box and the value, %d areas", s_areas);

    block(PRINT_PRINTING, 44);
    settle(T0);
    s_snap.b.progress = 56;
    settle(T0 + 0.01);
    static uint16_t stepped[W * H];
    memcpy(stepped, s_fb, sizeof stepped);
    input(1);
    settle(T0 + 0.02);
    input(-1);
    settle(T0 + 0.03);
    int diff = 0;
    for (int y = 410; y < 450; y++)
        for (int x = 200; x < 272; x++) diff += stepped[y * W + x] != s_fb[y * W + x];
    CHECK(diff == 0, "a ring step past the dots leaves them whole: %d px differ from a fresh draw", diff);

    block(PRINT_FINISHED, 100);
    s_snap.b.event = (print_event_t){.id = 1, .kind = PRINT_EV_FINISHED, .at = (long long)T0};
    settle(T0 + 1500);
    CHECK(count(0, 0, W, H, c565(PRINT_COL_DONE)) == 0, "first shown 25 min after the finish: dimmed already");
    s_snap.b.event = (print_event_t){0};
    settle(T0 + 1801);
    CHECK(count(0, 0, W, H, c565(PRINT_COL_DONE)) == 0, "still dimmed once the finish event ages out of the view");
    page_print_show(false);
    block(PRINT_FINISHED, 100);
    s_snap.b.started_at = (long long)T0 + 5000;
    s_snap.b.event = (print_event_t){.id = 9, .kind = PRINT_EV_FINISHED, .at = (long long)T0 + 9000};
    page_print_show(true);
    settle(T0 + 9030);
    CHECK(count(0, 0, W, H, c565(PRINT_COL_DONE)) > 1000, "the next job's Done, opened 30 s after it finished, is bright");

    block(PRINT_FINISHED, 100);
    s_snap.b.event = (print_event_t){.id = 1, .kind = PRINT_EV_FINISHED, .at = (long long)T0};
    settle(T0 + 5);
    save("k1-done");
    CHECK(count(0, 0, W, H, c565(PRINT_COL_DONE)) > 1000, "done: blue");
    int lit = ink(0, 0, W, H);
    settle(T0 + 601);
    save("k1-done-dim");
    int dim = 0;
    for (int i = 0; i < W * H; i++) dim += s_fb[i] == c565(PRINT_COL_DONE);
    CHECK(dim == 0 && ink(0, 0, W, H) <= lit, "after 10 min the Done face dims");

    block(PRINT_OFFLINE, 0);
    s_snap.b.has_progress = false;
    settle(T0);
    save("k1-offline");

    block(PRINT_PRINTING, 42);
    settle(T0);
    input(1);
    settle(T0 + 0.5);
    save("k2-fallback");
    CHECK(count(0, 0, W, H, c565(PRINT_COL_FIL)) > 200, "K2 without pictures: K1's printer, larger");
    make_pics();
    settle(T0 + 1);
    save("k2-printing");
    CHECK(count(0, 110, W, 300, c565(0x30A0E0)) > 3000 && count(0, 110, W, 300, c565(0x404040)) > 3000,
          "K2: solid below, ghost above");
    int clip = print_clip_row(110, 300, 42);
    CHECK(count(0, clip, W, 300, c565(0x404040)) == 0 && count(0, 110, W, clip - 3, c565(0x30A0E0)) == 0,
          "split at row %d", clip);
    frame(T0 + 1.2);
    refresh();
    CHECK(s_areas == 1 && s_px < 300 * 8, "the hot line is one thin box: %d areas, %d px", s_areas, s_px);
    s_snap.b.progress = 43;
    frame(T0 + 1.21);
    refresh();
    CHECK(s_areas <= 4 && s_px < 50000, "a percent on K2 (ring, rows, value and its unit): %d areas, %d px", s_areas, s_px);

    block(PRINT_FINISHED, 100);
    s_snap.b.event = (print_event_t){.id = 2, .kind = PRINT_EV_FINISHED, .at = (long long)T0};
    settle(T0 + 3.6);
    save("k2-done");
    CHECK(count(0, 110, W, 300, c565(0x404040)) == 0, "done: all solid");
    settle(T0 + 700);
    int glints = 0;
    for (int i = 0; i < 45; i++) {
        frame(T0 + 700 + i * 0.07);
        refresh();
        glints += s_areas;
    }
    CHECK(glints == 0, "no glint under the dim: %d areas in 3 s", glints);

    input(1);
    block(PRINT_PRINTING, 42);
    settle(T0 + 10);
    CHECK(count(0, 110, W, 300, c565(0x30A0E0)) == 0, "a turn back to K1: no picture");
    input(-1);
    settle(T0 + 11);
    CHECK(count(0, 110, W, 300, c565(0x30A0E0)) > 3000, "and back to K2");

    int freed = s_pics_freed, dropped = s_dropped;
    s_snap.b.thumb_version[0] = 0;
    settle(T0 + 13);
    CHECK(s_pics_freed == freed + 1 && s_dropped == dropped + 1, "a block without the picture frees it and tells the client");
    CHECK(count(0, 110, W, 300, c565(0x30A0E0)) == 0, "K2 falls back to the printer");
    make_pics();
    strcpy(s_snap.b.thumb_version, "5d73e113fbb7-r1");
    settle(T0 + 14);
    s_solid_col = 0xE03030;
    make_pics();
    s_solid_col = 0x30A0E0;
    settle(T0 + 15);
    CHECK(count(0, 110, W, 300, c565(0x30A0E0)) == 0 && count(0, 110, W, 300, c565(0xE03030)) > 3000,
          "a replaced pair of the same version is drawn whole");

    static const print_phase_t PH[] = {PRINT_OFFLINE, PRINT_IDLE, PRINT_HEATING, PRINT_PREPARING, PRINT_PRINTING,
                                       PRINT_FINISHING, PRINT_PAUSED, PRINT_ATTENTION, PRINT_FINISHED, PRINT_STOPPED,
                                       PRINT_ERROR};
    lv_obj_t *root = lv_obj_get_child(scr, 0);
    page_print_show(false);
    page_print_show(true);
    for (int face = 0; face < 2; face++) {
        for (int i = 0; i < (int)(sizeof PH / sizeof PH[0]); i++) {
            block(PH[i], 100);
            s_snap.b.thumb_version[0] = 0;
            s_snap.b.has_temps = true;
            s_snap.b.nozzle = s_snap.b.nozzle_target = s_snap.b.bed = s_snap.b.bed_target = 240;
            s_snap.b.ends_at = (long long)T0 + 35999;
            s_snap.b.started_at = (long long)T0 - 35999;
            s_snap.b.event = (print_event_t){.id = 3, .kind = PRINT_EV_FINISHED, .at = (long long)T0};
            settle(T0 + 20 + i);
            for (int k = 3; k <= 4; k++) {
                const char *txt = lv_label_get_text(lv_obj_get_child(root, k));
                CHECK(!strstr(txt, "..."), "face K%d, %s: \"%s\" fits unchanged", face + 1, print_phase_name(PH[i]), txt);
            }
        }
        input(1);
    }

    page_print_show(false);
    CHECK(!s_visible && page_canvas_owner() == PAGE_CANVAS_FREE, "hidden: canvas back, client told");
    page_print_show(true);
    settle(T0 + 12);
    CHECK(count(0, 110, W, 300, c565(0x30A0E0)) == 0, "a new open starts on K1");

    if (failures) {
        printf("print_view: %d failure(s)\n", failures);
        return 1;
    }
    printf("print_view: ok\n");
    return 0;
}
