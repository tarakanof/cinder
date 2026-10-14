#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cJSON.h"
#include "knob_view.h"
#include "np.h"
#include "np_ctl.h"
#include "np_cover.h"

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static const char *BLOCK =
    "{\"state\":\"playing\",\"source\":\"plex\",\"title\":\"Hey Jude\",\"artist\":\"The Beatles\","
    "\"album\":\"1\",\"duration_ms\":330000,\"position_ms\":61000,\"position_at\":1782044100000,"
    "\"art_version\":\"fb43cf74b67a\",\"album_art\":true,\"artist_art\":true}";

static bool parse(const char *json, np_info_t *np)
{
    cJSON *o = cJSON_Parse(json);
    bool ok = np_parse(o, np);
    cJSON_Delete(o);
    return ok;
}

static int tick(np_ctl_t *c, double t, np_cmd_t *out) { return np_ctl_tick(c, t, out, 4); }

static void test_ctl(void)
{
    np_ctl_t c;
    np_cmd_t out[4];
    np_ctl_init(&c);
    np_ctl_server(&c, NP_PLAYING, 40, 0);

    np_ctl_push(&c, 1.0);
    CHECK(tick(&c, 1.1, out) == 0, "push waits for a second one");
    CHECK(tick(&c, 1.0 + NP_DOUBLE_S + 0.01, out) == 1 && out[0].kind == NP_CMD_PAUSE, "push while playing = pause");
    CHECK(np_ctl_play_shown(&c, 1.4) == NP_PAUSED, "optimistic: paused at once");
    np_ctl_server(&c, NP_PLAYING, 40, 1.5);
    CHECK(np_ctl_play_shown(&c, 1.5) == NP_PAUSED, "a stale playing answer does not undo it");
    np_ctl_server(&c, NP_PAUSED, 40, 1.8);
    CHECK(!c.play_set && np_ctl_play_shown(&c, 1.8) == NP_PAUSED, "Ember confirms");

    np_ctl_push(&c, 2.0);
    CHECK(tick(&c, 2.4, out) == 1 && np_ctl_play_shown(&c, 2.5) == NP_PLAYING, "optimistic playing");
    CHECK(np_ctl_play_shown(&c, 2.4 + NP_HOLD_S + 0.1) == NP_PAUSED, "unconfirmed: back to Ember's state");

    np_ctl_push(&c, 10.0);
    np_ctl_push(&c, 10.2);
    int n = tick(&c, 10.21, out);
    CHECK(n == 1 && out[0].kind == NP_CMD_NEXT, "double push = next (%d)", n);
    CHECK(tick(&c, 11.0, out) == 0, "and nothing else");
    np_ctl_push(&c, 20.0);
    CHECK(tick(&c, 20.4, out) == 1 && out[0].kind == NP_CMD_PLAY, "slow push 1: play (shown paused)");
    np_ctl_push(&c, 20.5);
    CHECK(tick(&c, 20.9, out) == 1 && out[0].kind == NP_CMD_PAUSE, "slow push 2: pause, not a second play");
    np_ctl_long_push(&c, 30.0);
    CHECK(tick(&c, 30.0, out) == 1 && out[0].kind == NP_CMD_PREVIOUS, "long push = previous");

    np_ctl_init(&c);
    np_ctl_server(&c, NP_PLAYING, 40, 0);
    np_ctl_turn(&c, 1, 1.0);
    CHECK(np_ctl_volume_shown(&c, 1.0) == 42 && np_ctl_volume_active(&c, 1.0), "turn: 42 at once");
    CHECK(tick(&c, 1.0, out) == 1 && out[0].kind == NP_CMD_VOLUME && out[0].delta == 2, "first detent sent");
    np_ctl_turn(&c, 1, 1.05);
    np_ctl_turn(&c, 3, 1.1);
    CHECK(tick(&c, 1.1, out) == 0, "rate: nothing within 200 ms");
    CHECK(tick(&c, 1.2, out) == 0, "rate: nothing within 250 ms");
    CHECK(tick(&c, 1.26, out) == 1 && out[0].delta == 8, "the next send adds them up: +8 (%d)", out[0].delta);
    CHECK(np_ctl_volume_shown(&c, 1.3) == 50, "shown 50");
    np_ctl_server(&c, NP_PLAYING, 44, 1.4);
    CHECK(np_ctl_volume_shown(&c, 1.4) == 50, "a lagging level does not pull it back");
    np_ctl_server(&c, NP_PLAYING, 50, 1.6);
    CHECK(np_ctl_volume_shown(&c, 1.1 + NP_HOLD_S + 0.1) == 50, "then Ember's level");
    CHECK(!np_ctl_volume_active(&c, 1.1 + NP_VOL_SHOW_S + 0.1), "overlay goes");
    np_ctl_server(&c, NP_PLAYING, 30, 9);
    CHECK(np_ctl_volume_shown(&c, 9) == 30, "a change elsewhere shows");
    np_ctl_server(&c, NP_PLAYING, 99, 20);
    np_ctl_turn(&c, 5, 20.0);
    CHECK(np_ctl_volume_shown(&c, 20) == 100 && tick(&c, 20, out) == 1 && out[0].delta == 10,
          "shown clamped at 100, the full +10 sent (the level may be stale; the player clamps) (+%d)", out[0].delta);
    np_ctl_turn(&c, 1, 20.1);
    CHECK(np_ctl_volume_shown(&c, 20.1) == 100 && tick(&c, 20.3, out) == 1 && out[0].delta == 2, "at 100, up still sent");

    np_ctl_init(&c);
    np_ctl_server(&c, NP_PLAYING, 40, 0);
    np_ctl_push(&c, 1.0);
    tick(&c, 1.4, out);
    np_ctl_turn(&c, 2, 1.5);
    CHECK(np_ctl_play_shown(&c, 1.5) == NP_PAUSED && np_ctl_volume_shown(&c, 1.5) == 44, "optimistic");
    np_ctl_failed(&c);
    CHECK(np_ctl_play_shown(&c, 1.6) == NP_PLAYING && np_ctl_volume_shown(&c, 1.6) == 40 && tick(&c, 2.0, out) == 0,
          "failed: Ember's state, nothing more sent");
    np_ctl_init(&c);
    np_ctl_server(&c, NP_PLAYING, -1, 0);
    np_ctl_turn(&c, -2, 1.0);
    CHECK(np_ctl_volume_shown(&c, 1.0) == -1 && c.vol_gesture == -4, "unknown level: gesture -4");
    CHECK(tick(&c, 1.0, out) == 1 && out[0].delta == -4, "unknown level: sent");

    np_ctl_init(&c);
    np_ctl_server(&c, NP_PLAYING, 50, 0);
    int sent = 0;
    for (double t = 100.0; t < 120.0; t += 0.016) {
        np_ctl_turn(&c, 1, t);
        sent += tick(&c, t, out);
    }
    CHECK(sent <= (int)(20.0 / NP_VOL_SEND_S) + 1 && sent >= 70, "20 s of turning: %d commands", sent);

    np_ctl_server(&c, NP_NONE, -1, 2);
    np_ctl_push(&c, 2.0);
    np_ctl_turn(&c, 3, 2.0);
    CHECK(tick(&c, 3.0, out) == 0, "nothing playing: no commands");

    char body[64];
    np_cmd_t v = {NP_CMD_VOLUME, -6}, nx = {NP_CMD_NEXT, 0}, bad = {NP_CMD_NONE, 0};
    char big[256];
    CHECK(np_cmd_body(&v, NULL, NULL, body, sizeof body) > 0 && strcmp(body, "{\"action\":\"volume\",\"delta\":-6}") == 0, "%s", body);
    CHECK(np_cmd_body(&nx, "", "", body, sizeof body) > 0 && strcmp(body, "{\"action\":\"next\"}") == 0, "%s", body);
    CHECK(np_cmd_body(&nx, "music", "FFFE930376EB", big, sizeof big) > 0 &&
              strcmp(big, "{\"action\":\"next\",\"source\":\"music\",\"track_id\":\"FFFE930376EB\"}") == 0,
          "%s", big);
    CHECK(np_cmd_body(&nx, "plex", "a\"b", big, sizeof big) > 0 && strcmp(big, "{\"action\":\"next\",\"source\":\"plex\"}") == 0,
          "unsafe track_id left out: %s", big);
    np_cmd_t pa = {NP_CMD_PAUSE, 0};
    CHECK(np_cmd_body(&pa, NULL, NULL, body, sizeof body) > 0 && strcmp(body, "{\"action\":\"pause\"}") == 0, "%s", body);
    CHECK(np_cmd_body(&bad, NULL, NULL, body, sizeof body) < 0 && np_cmd_body(&v, NULL, NULL, body, 8) < 0, "none / too small");
}

static void test_parse(void)
{
    np_info_t np;
    CHECK(parse(BLOCK, &np), "block parses");
    CHECK(np.state == NP_PLAYING && strcmp(np.source, "plex") == 0, "state, source");
    CHECK(strcmp(np.title, "Hey Jude") == 0 && strcmp(np.artist, "The Beatles") == 0 && strcmp(np.album, "1") == 0,
          "text");
    CHECK(np.duration_ms == 330000 && np.position_ms == 61000 && np.position_at == 1782044100000LL, "times");
    CHECK(strcmp(np.art_version, "fb43cf74b67a") == 0 && np.album_art && np.artist_art, "art");

    CHECK(parse("{\"state\":\"none\"}", &np) && np.state == NP_NONE && !np.title[0], "none");
    CHECK(parse("{\"state\":\"paused\",\"title\":\"x\"}", &np) && np.state == NP_PAUSED && np.duration_ms == 0 &&
              !np.art_version[0] && !np.album_art,
          "paused, zero fields left out");
    CHECK(parse("{\"state\":\"buffering\",\"title\":\"x\"}", &np) && np.state == NP_NONE, "unknown state = none");
    CHECK(parse(BLOCK, &np) && np.volume == -1 && !np.track_id[0], "no volume: unknown; no track_id");
    CHECK(parse("{\"state\":\"playing\",\"track_id\":\"4242\"}", &np) && strcmp(np.track_id, "4242") == 0, "track_id");
    CHECK(parse("{\"state\":\"playing\",\"volume\":40}", &np) && np.volume == 40, "volume");
    CHECK(parse("{\"state\":\"playing\",\"volume\":140}", &np) && np.volume == -1, "volume out of range: unknown");
    CHECK(parse("{\"state\":\"none\"}", &np) && np.volume == -1, "none: volume unknown");
    CHECK(!parse("null", &np) && np.state == NP_NONE, "null");
    CHECK(parse("{\"state\":\"playing\",\"duration_ms\":1e300,\"position_ms\":-5,\"position_at\":1e15}", &np) &&
              np.duration_ms == 1000000000000000LL && np.position_ms == 0 && np.position_at == 1000000000000000LL,
          "huge / negative numbers clamp (no UB cast)");
    CHECK(parse("{\"state\":\"playing\",\"art_version\":\"../x\"}", &np) && !np.art_version[0], "unsafe version dropped");
    CHECK(parse("{\"state\":\"playing\",\"art_version\":\"0123456789012345678901234567890123\"}", &np) &&
              !np.art_version[0],
          "too long version dropped");

    char json[1024] = "{\"state\":\"playing\",\"title\":\"";
    for (int i = 0; i < 300; i++) strcat(json, "\xc3\xa9");
    strcat(json, "\"}");
    CHECK(parse(json, &np), "long title parses");
    CHECK(strlen(np.title) == 254 && (unsigned char)np.title[253] == 0xa9, "cut at a UTF-8 boundary: %zu",
          strlen(np.title));

    np_info_t a, b;
    parse(BLOCK, &a);
    parse(BLOCK, &b);
    b.position_ms = 1;
    CHECK(np_same_track(&a, &b), "seek: same track");
    b.state = NP_PAUSED;
    CHECK(!np_same_track(&a, &b), "state change");
    parse(BLOCK, &b);
    strcpy(b.title, "Let It Be");
    CHECK(!np_same_track(&a, &b), "track change");
}

static void test_view(void)
{
    knob_view_t v;
    char json[1024];
    snprintf(json, sizeof json, "{\"v\":1,\"mood\":{},\"brightness\":{\"level\":9},\"nowplaying\":%s}", BLOCK);
    CHECK(knob_view_parse(json, &v), "view with block parses");
    CHECK(v.has_np && v.np.state == NP_PLAYING && v.np.position_ms == 61000, "block in the view");
    CHECK(knob_view_parse("{\"v\":1,\"mood\":{}}", &v) && !v.has_np, "no block without the page");
    CHECK(knob_view_parse("{\"v\":1,\"mood\":{},\"nowplaying\":{\"state\":\"none\"}}", &v) && v.has_np &&
              v.np.state == NP_NONE,
          "none block");
}

static void test_position(void)
{
    np_info_t np;
    parse(BLOCK, &np);
    double offset = 1782044100.0 - 90.0;
    CHECK(np_position_ms(&np, offset, 100.0) == 71000, "playing extrapolates: %lld", np_position_ms(&np, offset, 100.0));
    CHECK(np_position_ms(&np, offset, 90.0 - 70.0) == 0, "clamped at 0");
    CHECK(np_position_ms(&np, offset, 90.0 + 1000.0) == 330000, "clamped at the duration");
    np.state = NP_PAUSED;
    CHECK(np_position_ms(&np, offset, 100.0) == 61000, "paused holds");
    np.state = NP_PLAYING;
    np.position_at = 0;
    CHECK(np_position_ms(&np, offset, 100.0) == 61000, "no anchor: as reported");

    np_anchor_t a = {0};
    np_anchor_sync(&a, false, true, 61000, 330000, 10.0);
    CHECK(np_anchor_at(&a, 12.5) == 63500, "runs: %lld", np_anchor_at(&a, 12.5));
    np_anchor_sync(&a, true, true, 64200, 330000, 12.5);
    CHECK(a.t == 10.0 && np_anchor_at(&a, 12.5) == 63500, "jitter keeps the anchor");
    np_anchor_sync(&a, true, true, 65500, 330000, 12.5);
    CHECK(np_anchor_at(&a, 12.5) == 65500, "2 s off re-bases");
    np_anchor_sync(&a, true, true, 120000, 330000, 12.5);
    CHECK(np_anchor_at(&a, 12.5) == 120000, "seek re-bases");
    np_anchor_sync(&a, true, false, 120400, 330000, 13.0);
    CHECK(!a.playing && np_anchor_at(&a, 20.0) == 120400, "pause holds");
    np_anchor_sync(&a, false, true, 0, 0, 0);
    CHECK(np_anchor_at(&a, 50.0) == 50000, "no duration: time runs");
}

static void test_format(void)
{
    char b[16];
    np_format_time(61000, b, sizeof b);
    CHECK(strcmp(b, "1:01") == 0, "%s", b);
    np_format_time(999, b, sizeof b);
    CHECK(strcmp(b, "0:00") == 0, "%s", b);
    np_format_time(-5, b, sizeof b);
    CHECK(strcmp(b, "0:00") == 0, "%s", b);
    np_format_time(3723000, b, sizeof b);
    CHECK(strcmp(b, "1:02:03") == 0, "%s", b);
}

static void fold_is(const char *in, const char *want)
{
    char out[128];
    np_text_fold(in, out, sizeof out);
    CHECK(strcmp(out, want) == 0, "fold \"%s\" -> \"%s\", want \"%s\"", in, out, want);
}

static void test_fold(void)
{
    fold_is("Hey Jude", "Hey Jude");
    fold_is("Beyonc\xc3\xa9", "Beyonce");
    fold_is("Sigur R\xc3\xb3s", "Sigur Ros");
    fold_is("Mot\xc3\xb6rhead \xe2\x80\x94 Ace", "Motorhead - Ace");
    fold_is("Stra\xc3\x9f" "e", "Strasse");
    fold_is("\xc5\x81\xc3\xb3" "d\xc5\xba", "Lodz");
    fold_is("\xd0\x9a\xd0\xb8\xd0\xbd\xd0\xbe", "Kino");
    fold_is("\xd0\x93\xd1\x80\xd1\x83\xd0\xbf\xd0\xbf\xd0\xb0 \xd0\xba\xd1\x80\xd0\xbe\xd0\xb2\xd0\xb8",
            "Gruppa krovi");
    fold_is("\xd0\xa9\xd1\x91\xd0\xbb\xd0\xba", "Shchyolk");
    fold_is("\xe2\x80\x9cQuoted\xe2\x80\x9d \xe2\x80\x98it\xe2\x80\x99s\xe2\x80\x99\xe2\x80\xa6", "\"Quoted\" 'it's'...");
    fold_is("Smile \xf0\x9f\x98\x80", "Smile ?");
    fold_is("\xe6\x9d\xb1\xe4\xba\xac", "??");
    fold_is("bad \xff byte", "bad ? byte");
    fold_is("cut \xc3", "cut ?");
    fold_is("e\xcc\x81", "e");
    fold_is("tab\there", "tab here");
    char small[6];
    np_text_fold("\xd0\xa9\xd0\xa9", small, sizeof small);
    CHECK(strcmp(small, "Shch") == 0, "cut on a whole replacement: %s", small);
}

static void test_art(void)
{
    np_info_t np;
    np_art_plan_t p;
    parse(BLOCK, &np);
    np_art_plan(&np, &p);
    CHECK(strcmp(p.version, "fb43cf74b67a") == 0 && p.want[NP_ART_BACKDROP] && p.want[NP_ART_ALBUM] &&
              p.want[NP_ART_ARTIST],
          "all three");
    np.artist_art = false;
    np_art_plan(&np, &p);
    CHECK(p.want[NP_ART_BACKDROP] && p.want[NP_ART_ALBUM] && !p.want[NP_ART_ARTIST], "no artist: backdrop from album");
    np.album_art = false;
    np_art_plan(&np, &p);
    CHECK(!p.version[0] && !p.want[NP_ART_BACKDROP], "no pictures: empty set");
    parse("{\"state\":\"none\"}", &np);
    np_art_plan(&np, &p);
    CHECK(!p.version[0], "nothing playing: empty set");

    char url[160];
    CHECK(np_art_url("http://192.168.0.2:3627", NP_ART_BACKDROP, "fb43", url, sizeof url) &&
              strcmp(url, "http://192.168.0.2:3627/v1/nowplaying/art?kind=backdrop&size=466&v=fb43") == 0,
          "%s", url);
    CHECK(np_art_url("http://h", NP_ART_ALBUM, "v1", url, sizeof url) &&
              strcmp(url, "http://h/v1/nowplaying/art?kind=album&size=240&v=v1") == 0,
          "%s", url);
    CHECK(np_art_url("http://h", NP_ART_ARTIST, "v1", url, sizeof url) && strstr(url, "kind=artist&size=64"), "%s", url);
    CHECK(!np_art_url("http://h", NP_ART_ALBUM, "a&b", url, sizeof url), "unsafe version refused");
    CHECK(!np_art_url("http://h", NP_ART_ALBUM, "v1", url, 20), "too small");
    CHECK(np_art_version_ok("aZ09_-") && !np_art_version_ok("") && !np_art_version_ok("a b"), "version charset");
    CHECK(np_art_retry_ms(0) == 0 && np_art_retry_ms(1) == 5000 && np_art_retry_ms(2) == 10000 &&
              np_art_retry_ms(4) == 40000 && np_art_retry_ms(9) == 60000,
          "retry backoff");
}

#define W 466
#define H 466
static const np_ring_t RING = {.cx = 233, .cy = 233, .r = 198, .hw = 3};

static void test_ring(void)
{
    uint16_t *full = malloc(W * H * 2), *part = malloc(W * H * 2), *other = malloc(W * H * 2);
    const uint16_t SENT = 0x1234;
    for (int i = 0; i < W * H; i++) full[i] = part[i] = other[i] = SENT;
    np_ring_render(&RING, full, W, H, (np_rect_t){0, 0, W, H}, 0.37f, 0xFFFFFF, 0x404040);

    int written = 0, outside = 0;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            if (full[y * W + x] == SENT) continue;
            written++;
            float d = hypotf(x + 0.5f - 233, y + 0.5f - 233);
            if (fabsf(d - 198) > 3 + 1.5f) outside++;
        }
    CHECK(written > 8000 && written < 14000, "band pixels %d", written);
    CHECK(outside == 0, "%d pixels written outside the band", outside);
    CHECK(full[233 * W + 233] == SENT && full[0] == SENT, "centre and corner untouched");

    uint16_t top = full[(int)(233 - 198) * W + 233], bottom = full[(int)(233 + 198) * W + 233];
    CHECK(top == 0xFFFF, "arc at the start: %04x", top);
    CHECK(bottom == ((0x40 >> 3) << 11 | (0x40 >> 2) << 5 | (0x40 >> 3)), "track at 6 o'clock: %04x", bottom);

    np_ring_render(&RING, part, W, H, (np_rect_t){0, 0, W, H}, 0.36f, 0xFFFFFF, 0x404040);
    np_rect_t d;
    CHECK(np_ring_dirty(&RING, 0.36f, 0.37f, W, H, &d), "dirty");
    CHECK(d.w <= 40 && d.h <= 40 && d.x % 2 == 0 && d.y % 2 == 0 && d.w % 2 == 0 && d.h % 2 == 0,
          "small, even-aligned: %d,%d %dx%d", d.x, d.y, d.w, d.h);
    np_ring_render(&RING, part, W, H, d, 0.37f, 0xFFFFFF, 0x404040);
    int diff = 0;
    for (int i = 0; i < W * H; i++) diff += part[i] != full[i];
    CHECK(diff == 0, "partial redraw differs in %d pixels", diff);

    const float fr[][2] = {{0, 0.01f}, {0.24f, 0.26f}, {0.49f, 0.52f}, {0.99f, 1}, {0.1f, 0.9f}};
    for (size_t k = 0; k < sizeof fr / sizeof fr[0]; k++) {
        for (int i = 0; i < W * H; i++) part[i] = other[i] = SENT;
        np_ring_render(&RING, part, W, H, (np_rect_t){0, 0, W, H}, fr[k][0], 0xFF6A3D, 0x303030);
        np_ring_render(&RING, other, W, H, (np_rect_t){0, 0, W, H}, fr[k][1], 0xFF6A3D, 0x303030);
        CHECK(np_ring_dirty(&RING, fr[k][0], fr[k][1], W, H, &d), "dirty %zu", k);
        int out = 0;
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++)
                if (part[y * W + x] != other[y * W + x] && (x < d.x || x >= d.x + d.w || y < d.y || y >= d.y + d.h))
                    out++;
        CHECK(out == 0, "case %zu: %d changed pixels outside the dirty rect", k, out);
    }
    CHECK(!np_ring_dirty(&RING, 0.5f, 0.5f, W, H, &d), "no change, no rect");

    for (int i = 0; i < W * H; i++) part[i] = SENT;
    np_ring_render(&RING, part, W, H, (np_rect_t){0, 0, W, H}, 0, 0xFFFFFF, 0x404040);
    CHECK(part[(int)(233 - 198) * W + 233] == bottom, "frac 0: track at 12 o'clock");

    float x, y;
    np_ring_point(&RING, 0.25f, &x, &y);
    CHECK(fabsf(x - 431) < 1e-3 && fabsf(y - 233) < 1e-3, "quarter at 3 o'clock: %f,%f", x, y);
    free(full);
    free(part);
    free(other);
}

static void test_masks(void)
{
    uint16_t *buf = malloc(W * H * 2);
    for (int i = 0; i < W * H; i++) buf[i] = 0xFFFF;
    np_face_mask(buf, W, H, 233, 233, 188);
    CHECK(buf[233 * W + 233] == 0xFFFF && buf[233 * W + 233 - 186] == 0xFFFF, "inside kept");
    CHECK(buf[0] == 0 && buf[233 * W + 233 - 190] == 0 && buf[(233 + 190) * W + 233] == 0, "outside black");
    int bad = 0, partial = 0;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            float d = hypotf(x + 0.5f - 233, y + 0.5f - 233);
            uint16_t p = buf[y * W + x];
            if (d < 187.4f && p != 0xFFFF) bad++;
            if (d > 188.6f && p != 0) bad++;
            if (p != 0 && p != 0xFFFF) partial++;
        }
    CHECK(bad == 0, "%d pixels on the wrong side of the edge", bad);
    CHECK(partial > 500, "anti-aliased edge: %d", partial);
    CHECK(buf[(233 - 198) * W + 233] == 0, "band black");
    free(buf);

    uint8_t a[64 * 64];
    np_circle_alpha(a, 64);
    CHECK(a[32 * 64 + 32] == 255 && a[0] == 0 && a[63] == 0 && a[63 * 64 + 63] == 0, "disk alpha");
    CHECK(a[32 * 64 + 0] > 0 && a[32 * 64 + 0] < 255, "edge pixel partial: %d", a[32 * 64]);
    int asym = 0;
    for (int y = 0; y < 64; y++)
        for (int x = 0; x < 64; x++) asym += a[y * 64 + x] != a[(63 - y) * 64 + (63 - x)] || a[y * 64 + x] != a[x * 64 + y];
    CHECK(asym == 0, "symmetric: %d", asym);
}

static const char *COVER_BLOCK =
    "{\"state\":\"playing\",\"source\":\"plex\",\"title\":\"Example Song\",\"artist\":\"Example Artist\","
    "\"album\":\"Example Album\",\"track_id\":\"4242\",\"duration_ms\":330000,\"position_ms\":61000,"
    "\"position_at\":1782043198000,\"art_version\":\"fb43cf74b67a\",\"album_art\":true,\"volume\":40,"
    "\"accent\":\"#6C22E6\",\"cover_version\":\"fb43cf74b67a-r1\"}";

static void test_cover_parse(void)
{
    np_info_t np;
    CHECK(parse(COVER_BLOCK, &np) && np.has_accent && np.accent == 0x6C22E6 && strcmp(np.cover_version, "fb43cf74b67a-r1") == 0,
          "accent and cover_version");
    CHECK(np_face_wants_cover(&np), "cover face");
    parse(BLOCK, &np);
    CHECK(!np.has_accent && !np.cover_version[0] && !np_face_wants_cover(&np), "an older Ember: no cover data");
    uint32_t c = 7;
    CHECK(np_accent_parse("#a0b1C2", &c) && c == 0xA0B1C2, "lower and upper hex");
    static const char *const BAD[] = {"", "#", "#12345", "#1234567", "123456", "#12345g", "#-12345", " #123456"};
    for (size_t i = 0; i < sizeof BAD / sizeof BAD[0]; i++) {
        c = 7;
        CHECK(!np_accent_parse(BAD[i], &c) && c == 7, "bad accent '%s'", BAD[i]);
    }
    CHECK(!np_accent_parse(NULL, &c), "NULL accent");
    CHECK(parse("{\"state\":\"playing\",\"accent\":\"red\",\"cover_version\":\"a b\"}", &np) && !np.has_accent &&
              !np.cover_version[0],
          "bad values dropped");
    char longv[128];
    snprintf(longv, sizeof longv, "{\"state\":\"playing\",\"cover_version\":\"%0*d\"}", NP_COVERV_MAX + 1, 0);
    CHECK(parse(longv, &np) && !np.cover_version[0], "an over-long cover_version is dropped, not cut");
    snprintf(longv, sizeof longv, "{\"state\":\"playing\",\"cover_version\":\"%0*d\"}", NP_COVERV_MAX, 0);
    CHECK(parse(longv, &np) && strlen(np.cover_version) == NP_COVERV_MAX, "the longest cover_version fits");
    parse("{\"state\":\"none\",\"accent\":\"#6C22E6\",\"cover_version\":\"x-r1\"}", &np);
    CHECK(!np.has_accent && !np_face_wants_cover(&np), "nothing playing: no cover");

    CHECK(parse(COVER_BLOCK, &np) && !np.cover_light, "no cover_light: a dark text band");
    static const char *const LIGHT =
        "{\"state\":\"playing\",\"title\":\"White Album\",\"accent\":\"#6C22E6\",\"cover_version\":\"c0ffee-r1\","
        "\"cover_light\":true}";
    CHECK(parse(LIGHT, &np) && np.cover_light && np.has_accent && strcmp(np.cover_version, "c0ffee-r1") == 0,
          "cover_light next to accent and cover_version");
    CHECK(parse("{\"state\":\"playing\",\"cover_version\":\"c-r1\",\"cover_light\":false}", &np) && !np.cover_light,
          "cover_light false");
    CHECK(parse("{\"state\":\"playing\",\"cover_version\":\"c-r1\",\"cover_light\":\"true\"}", &np) && !np.cover_light &&
              parse("{\"state\":\"playing\",\"cover_version\":\"c-r1\",\"cover_light\":1}", &np) && !np.cover_light,
          "only a JSON true is light");
    CHECK(parse("{\"state\":\"none\",\"cover_light\":true}", &np) && !np.cover_light, "nothing playing: no cover_light");
}

static void test_cover_plan(void)
{
    np_info_t np;
    np_art_plan_t p;
    char url[160];
    parse(COVER_BLOCK, &np);
    np_art_plan(&np, &p);
    CHECK(p.cover && strcmp(p.version, "fb43cf74b67a-r1") == 0 && p.want[NP_ART_COVER] && !p.want[NP_ART_BACKDROP] &&
              !p.want[NP_ART_ALBUM] && !p.want[NP_ART_ARTIST],
          "the cover only");
    CHECK(np_art_url("http://h", NP_ART_COVER, np_art_plan_version(&p, NP_ART_COVER), url, sizeof url) &&
              strcmp(url, "http://h/v1/nowplaying/art?kind=cover&size=466&v=fb43cf74b67a-r1") == 0,
          "%s", url);
    np.artist_art = true;
    np_art_plan(&np, &p);
    CHECK(p.want[NP_ART_COVER] && p.want[NP_ART_ARTIST] && strcmp(np_art_plan_version(&p, NP_ART_ARTIST), "fb43cf74b67a") == 0,
          "the artist picture at art_version");
    CHECK(np_art_url("http://h", NP_ART_ARTIST, np_art_plan_version(&p, NP_ART_ARTIST), url, sizeof url) &&
              strcmp(url, "http://h/v1/nowplaying/art?kind=artist&size=64&v=fb43cf74b67a") == 0,
          "%s", url);
    np.album_art = false;
    np_art_plan(&np, &p);
    CHECK(p.want[NP_ART_COVER], "a cover from the artist picture");
    np.cover_version[0] = 0;
    np.album_art = true;
    np_art_plan(&np, &p);
    CHECK(!p.cover && p.want[NP_ART_BACKDROP] && !p.want[NP_ART_COVER] && strcmp(p.version, "fb43cf74b67a") == 0,
          "without cover_version: today's pictures");
    CHECK(np_art_px(NP_ART_COVER) == 466 && strcmp(np_art_kind_name(NP_ART_COVER), "cover") == 0, "cover kind");
}

static const np_ring_t EDGE = {.cx = 233, .cy = 233, .r = NP_EDGE_R, .hw = NP_EDGE_HW};

static int changed_outside(const uint16_t *a, const uint16_t *b, np_rect_t d)
{
    int out = 0;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            if (a[y * W + x] != b[y * W + x] && (x < d.x || x >= d.x + d.w || y < d.y || y >= d.y + d.h)) out++;
    return out;
}

static uint16_t c565(uint32_t c) { return (uint16_t)((((c >> 16) & 0xFF) >> 3) << 11 | (((c >> 8) & 0xFF) >> 2) << 5 | ((c & 0xFF) >> 3)); }

static void test_edge(void)
{
    static uint8_t glow[NP_GLOW_PX * NP_GLOW_PX];
    np_glow_a8(glow);
    int c = NP_GLOW_PX / 2;
    CHECK(glow[c * NP_GLOW_PX + c] >= 250 && glow[0] < 3, "glow: centre %d, corner %d", glow[c * NP_GLOW_PX + c], glow[0]);
    int e = glow[c * NP_GLOW_PX + c + NP_GLOW_R];
    CHECK(e > 100 && e < 155, "glow at r %d: %d", NP_GLOW_R, e);

    uint16_t *full = malloc(W * H * 2), *part = malloc(W * H * 2), *other = malloc(W * H * 2);
    const uint16_t SENT = 0x1234;
    np_edge_style_t st;
    const uint32_t ACC = 0x6C22E6;
    np_edge_style(&st, NP_RING_PROGRESS, ACC);
    for (int i = 0; i < W * H; i++) full[i] = SENT;
    np_edge_render(&EDGE, full, W, H, (np_rect_t){0, 0, W, H}, 0.6f, &st, glow);
    int inside = 0;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            if (hypotf(x + 0.5f - 233, y + 0.5f - 233) < NP_EDGE_IN - 0.01f && full[y * W + x] != SENT) inside++;
    CHECK(inside == 0, "%d cover pixels written", inside);
    CHECK(full[233 * W + 233] == SENT && full[(233 - NP_DISC_R + 1) * W + 233] == SENT, "the cover stays");
    CHECK(full[0] == 0 && full[233 * W + 233 + NP_EDGE_IN + 1] != SENT, "the band is ours, black outside the ring");
    CHECK(full[(233 + NP_EDGE_R) * W + 233] == c565(ACC), "arc at 6 o'clock in the accent: %04x", full[(233 + NP_EDGE_R) * W + 233]);
    CHECK(full[233 * W + 233 - NP_EDGE_R] == c565(NP_COL_TRACK), "track at 9 o'clock");
    float hx, hy;
    np_ring_point(&EDGE, 0.6f, &hx, &hy);
    CHECK(full[(int)hy * W + (int)hx] == c565(NP_COL_HEAD), "head dot at the progress point");
    int gx = (int)hx, gy = (int)hy, glowed = 0;
    for (int y = gy - 20; y <= gy + 20; y++)
        for (int x = gx - 20; x <= gx + 20; x++) {
            float d = hypotf(x + 0.5f - hx, y + 0.5f - hy), r = hypotf(x + 0.5f - 233, y + 0.5f - 233);
            if (d > NP_HEAD_R + 2 && d < 15 && r > NP_EDGE_R + NP_EDGE_HW + 2 && full[y * W + x] != 0) glowed++;
        }
    CHECK(glowed > 20, "accent glow around the head outside the ring: %d px", glowed);

    float steps[][2] = {{0.6f, 0.605f}, {0, 0.004f}, {0.995f, 1}, {0.249f, 0.251f}, {0.3f, 0.7f}};
    for (size_t k = 0; k < sizeof steps / sizeof steps[0]; k++)
        for (int m = 0; m < 2; m++) {
            np_edge_style(&st, m ? NP_RING_VOLUME : NP_RING_PROGRESS, ACC);
            for (int i = 0; i < W * H; i++) part[i] = other[i] = SENT;
            np_edge_render(&EDGE, part, W, H, (np_rect_t){0, 0, W, H}, steps[k][0], &st, glow);
            np_edge_render(&EDGE, other, W, H, (np_rect_t){0, 0, W, H}, steps[k][1], &st, glow);
            np_rect_t d;
            CHECK(np_edge_dirty(&EDGE, steps[k][0], steps[k][1], W, H, &d), "dirty %zu", k);
            CHECK(changed_outside(part, other, d) == 0, "step %zu mode %d: change outside the box", k, m);
            np_edge_render(&EDGE, part, W, H, d, steps[k][1], &st, glow);
            int diff = 0;
            for (int i = 0; i < W * H; i++) diff += part[i] != other[i];
            CHECK(diff == 0, "step %zu mode %d: box redraw differs from a full one in %d px", k, m, diff);
            CHECK(d.x % 2 == 0 && d.y % 2 == 0 && d.w % 2 == 0 && d.h % 2 == 0, "even box");
        }
    np_rect_t d;
    np_edge_dirty(&EDGE, 0.6f, 0.6f + 1.0f / 214, W, H, &d);
    CHECK(d.w <= 70 && d.h <= 70, "a 1 s step on a 3:34 track is one small box: %dx%d", d.w, d.h);
    int step_px = 0, detent_px = 0;
    np_rect_t sb = d, db = d;
    for (int i = 0; i < 1000; i++) {
        float f = i / 1000.0f;
        if (np_edge_dirty(&EDGE, f, f + 1.0f / 120, W, H, &d) && d.w * d.h > step_px) step_px = d.w * d.h, sb = d;
        if (np_edge_dirty(&EDGE, f, f + NP_VOL_STEP / 100.0f, W, H, &d) && d.w * d.h > detent_px) detent_px = d.w * d.h, db = d;
    }
    CHECK(step_px <= 72 * 72, "a 1 s step on a 2:00 track, worst box %dx%d", sb.w, sb.h);
    CHECK(detent_px <= 96 * 96, "a 2-point volume detent, worst box %dx%d", db.w, db.h);
    printf("np: worst boxes: 1 s step on a 2:00 track %dx%d (%d px), volume detent %dx%d (%d px)\n", sb.w, sb.h, step_px,
           db.w, db.h, detent_px);

    np_edge_style(&st, NP_RING_VOLUME, ACC);
    for (int i = 0; i < W * H; i++) part[i] = SENT;
    np_edge_render(&EDGE, part, W, H, (np_rect_t){0, 0, W, H}, 0.9f, &st, glow);
    uint16_t start = part[(233 - NP_EDGE_R) * W + 233 + 14], late = part[233 * W + 233 - NP_EDGE_R];
    CHECK(((start >> 11) & 31) == 31 && ((start >> 5) & 63) >= 60, "volume starts white: %04x", start);
    CHECK(((late >> 5) & 63) < ((start >> 5) & 63) - 10, "and tints toward the accent: %04x", late);

    np_edge_style(&st, NP_RING_PROGRESS, ACC);
    for (int i = 0; i < W * H; i++) part[i] = SENT;
    int64_t reps = 0;
    clock_t t0 = clock();
    for (; reps < 20; reps++) np_edge_render(&EDGE, part, W, H, (np_rect_t){0, 0, W, H}, 0.6f, &st, glow);
    printf("np: full edge render %.2f ms (host)\n", (double)(clock() - t0) * 1000.0 / CLOCKS_PER_SEC / reps);
    np_edge_dirty(&EDGE, 0.6f, 0.6f + 1.0f / 214, W, H, &d);
    t0 = clock();
    for (reps = 0; reps < 2000; reps++) np_edge_render(&EDGE, part, W, H, d, 0.6f + 1.0f / 214, &st, glow);
    printf("np: 1 s step box %dx%d, %.3f ms (host)\n", d.w, d.h, (double)(clock() - t0) * 1000.0 / CLOCKS_PER_SEC / reps);
    free(full);
    free(part);
    free(other);
}

static void test_ring_mode(void)
{
    np_ctl_t c;
    np_ctl_init(&c);
    np_ctl_server(&c, NP_PLAYING, 40, 0);
    CHECK(np_ring_mode(&c, 10) == NP_RING_PROGRESS, "progress before a turn");
    np_ctl_turn(&c, 1, 10);
    CHECK(np_ring_mode(&c, 10) == NP_RING_VOLUME, "a detent switches at once");
    CHECK(np_ring_mode(&c, 10 + NP_VOL_SHOW_S - 0.01) == NP_RING_VOLUME, "held");
    np_ctl_turn(&c, 2, 11);
    CHECK(np_ring_mode(&c, 11 + NP_VOL_SHOW_S - 0.01) == NP_RING_VOLUME, "each detent restarts the 1.5 s");
    CHECK(np_ring_mode(&c, 11 + NP_VOL_SHOW_S) == NP_RING_PROGRESS, "back to progress 1.5 s after the last detent");
    CHECK(NP_VOL_SHOW_S == 1.5, "1.5 s");
    np_ctl_init(&c);
    np_ctl_server(&c, NP_PLAYING, -1, 0);
    np_ctl_turn(&c, 1, 20);
    CHECK(np_ring_mode(&c, 20) == NP_RING_PROGRESS, "an unknown level keeps the progress ring (the badge shows +N)");
}

static void test_face_state(void)
{
    CHECK(np_face_state(false, NP_NONE, false, false) == NPF_WAITING, "waiting for Ember");
    CHECK(np_face_state(true, NP_NONE, false, false) == NPF_NOTHING, "nothing playing");
    CHECK(np_face_state(true, NP_PLAYING, false, true) == NPF_PLAYING, "playing");
    CHECK(np_face_state(true, NP_PAUSED, false, true) == NPF_PAUSED, "paused");
    CHECK(np_face_state(true, NP_PLAYING, true, true) == NPF_OFFLINE, "offline keeps the last cover");
    CHECK(np_face_state(true, NP_PLAYING, true, false) == NPF_OFFLINE_EMPTY, "offline without a cover");
    CHECK(np_face_live(NPF_PLAYING) && np_face_live(NPF_PAUSED) && !np_face_live(NPF_OFFLINE) && !np_face_live(NPF_NOTHING),
          "live states");
    np_face_look_t l;
    np_face_look(NPF_PLAYING, NP_RING_PROGRESS, 0x6C22E6, &l);
    CHECK(l.shade == 0 && l.ring == 0x6C22E6 && !l.pause_icon && !l.offline_badge, "playing: full cover, accent ring");
    np_face_look(NPF_PAUSED, NP_RING_PROGRESS, 0x6C22E6, &l);
    CHECK(l.shade > 100 && l.ring == NP_COL_PAUSED && l.pause_icon, "paused: dimmed, grey ring, pause icon");
    np_face_look(NPF_PAUSED, NP_RING_VOLUME, 0x6C22E6, &l);
    CHECK(l.shade == 153 && l.ring == 0x6C22E6 && !l.pause_icon, "volume over paused");
    np_face_look(NPF_PLAYING, NP_RING_VOLUME, 0x6C22E6, &l);
    CHECK(l.shade == 153, "volume dims the cover to 40 %%");
    np_face_look(NPF_OFFLINE, NP_RING_VOLUME, 0x6C22E6, &l);
    CHECK(l.ring == NP_COL_OFFLINE && l.offline_badge && l.time_col == NP_COL_PAUSED && l.shade > 0 && l.shade < 115,
          "offline: grey ring, badge, no volume");
}

static void test_vinyl(void)
{
    uint16_t *b = malloc(W * H * 2);
    for (int i = 0; i < W * H; i++) b[i] = 0x1234;
    np_vinyl_render(b, W, H, 233, 233 + NP_VINYL_DY);
    int cy = 233 + NP_VINYL_DY, out = 0;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            if (b[y * W + x] != 0x1234 && (abs(x - 233) > NP_VINYL_BOX / 2 || abs(y - cy) > NP_VINYL_BOX / 2)) out++;
    CHECK(out == 0, "%d vinyl pixels outside its box", out);
    CHECK(b[(cy - NP_VINYL_R) * W + 300] == 0 && b[(cy - NP_VINYL_R) * W + 233] == c565(0x2A2A2A), "outer rim");
    CHECK(b[cy * W + 233] == c565(0x2A2A2A) && b[cy * W + 233 + 30] == 0, "spindle and label");
    CHECK(b[(cy - 100) * W + 240] == c565(0x3A3A3A) && b[(cy + 100) * W + 226] == c565(0x2E2E2E) &&
              b[(cy - 100) * W + 226] != c565(0x3A3A3A),
          "the two highlight arcs");
    free(b);
}

static void test_downscale(void)
{
    uint16_t src[64 * 64], dst[NP_AVATAR_PX * NP_AVATAR_PX];
    for (int i = 0; i < 64 * 64; i++) src[i] = 0x7BEF;
    np_downscale565(src, 64, dst, NP_AVATAR_PX);
    int bad = 0;
    for (int i = 0; i < NP_AVATAR_PX * NP_AVATAR_PX; i++) bad += dst[i] != 0x7BEF;
    CHECK(bad == 0, "flat stays flat: %d", bad);
    for (int y = 0; y < 64; y++)
        for (int x = 0; x < 64; x++) src[y * 64 + x] = x < 31 ? 0 : 0xFFFF;
    np_downscale565(src, 64, dst, NP_AVATAR_PX);
    CHECK(dst[0] == 0 && dst[NP_AVATAR_PX - 1] == 0xFFFF && dst[21] != 0 && dst[21] != 0xFFFF && dst[20] == 0 && dst[22] == 0xFFFF,
          "edge averaged: %04x", dst[21]);
}

static void test_review_fixes(void)
{
    np_info_t a, b;
    parse(COVER_BLOCK, &a);
    b = a;
    CHECK(!np_info_changed(&a, &b), "same block");
    strcpy(b.cover_version, "fb43cf74b67a-r2");
    CHECK(np_info_changed(&a, &b), "a cover recipe bump is a change");
    b = a;
    b.accent = 0x123456;
    CHECK(np_info_changed(&a, &b), "another accent is a change");
    b = a;
    b.has_accent = false;
    CHECK(np_info_changed(&a, &b), "accent gone is a change");
    b = a;
    b.cover_light = true;
    CHECK(np_info_changed(&a, &b), "cover_light is a change");
    b = a;
    b.position_ms += 5000;
    CHECK(!np_info_changed(&a, &b), "the position alone is the anchor's business");

    CHECK(np_art_cache_fresh("public, max-age=86400, immutable") && np_art_cache_fresh("Immutable") &&
              np_art_cache_fresh("immutable,public"),
          "immutable answers");
    CHECK(!np_art_cache_fresh("no-cache") && !np_art_cache_fresh("") && !np_art_cache_fresh(NULL) &&
              !np_art_cache_fresh("x-immutable-ish"),
          "anything else is not the asked version");

    np_art_plan_t cp, lp;
    np_art_plan(&a, &cp);
    a.cover_version[0] = 0;
    np_art_plan(&a, &lp);
    CHECK(np_art_step(&cp, NP_ART_COVER, 200, true, true, false) == NP_ART_OK, "cover ok");
    CHECK(np_art_step(&cp, NP_ART_COVER, 404, false, false, false) == NP_ART_FAIL, "cover 404 retries");
    CHECK(np_art_step(&cp, NP_ART_COVER, 200, true, false, true) == NP_ART_FAIL, "cover not decoded (no PSRAM) retries");
    CHECK(np_art_step(&cp, NP_ART_COVER, 200, true, false, false) == NP_ART_BAD, "cover over the cap or undecodable: bad");
    CHECK(np_art_step(&cp, NP_ART_ARTIST, 200, true, false, false) == NP_ART_SKIP &&
              np_art_step(&lp, NP_ART_BACKDROP, 200, false, false, false) == NP_ART_SKIP,
          "only the cover counts as bad");
    CHECK(np_art_step(&cp, NP_ART_COVER, 200, false, true, false) == NP_ART_FAIL, "a no-cache cover is another version: retry");
    CHECK(np_art_step(&cp, NP_ART_ARTIST, 200, false, true, false) == NP_ART_FAIL, "a no-cache artist too");
    CHECK(np_art_step(&cp, NP_ART_ARTIST, 404, false, false, false) == NP_ART_SKIP, "no artist picture: no avatar");
    CHECK(np_art_step(&cp, NP_ART_ARTIST, 200, true, false, false) == NP_ART_SKIP, "avatar not decoded: none");
    CHECK(np_art_step(&cp, NP_ART_COVER, 500, true, true, false) == NP_ART_FAIL && np_art_step(&cp, NP_ART_COVER, -1, false, false, false) == NP_ART_FAIL,
          "errors retry");
    CHECK(np_art_step(&lp, NP_ART_BACKDROP, 200, false, true, false) == NP_ART_OK && np_art_step(&lp, NP_ART_ALBUM, 404, false, false, false) == NP_ART_SKIP &&
              np_art_step(&lp, NP_ART_BACKDROP, 200, false, false, false) == NP_ART_SKIP,
          "the old face as before");

    np_art_retry_t r = {0};
    CHECK(!np_art_retry_fail(&r, "v1-r1", true) && np_art_retry_ms(r.fails) == 5000, "first bad cover: retry in 5 s");
    CHECK(!np_art_retry_fail(&r, "v1-r1", false) && r.bad == 1, "a network error does not count as bad");
    CHECK(!np_art_retry_fail(&r, "v1-r1", true) && np_art_retry_ms(r.fails) == 20000, "second bad: the backoff goes on");
    CHECK(np_art_retry_fail(&r, "v1-r1", true) && r.bad == NP_ART_BAD_TRIES, "third bad: give up on v1-r1");
    CHECK(np_art_retry_fail(&r, "v1-r1", false), "and stay given up for that version");
    CHECK(!np_art_retry_fail(&r, "v2-r1", true) && r.fails == 1 && r.bad == 1 && strcmp(r.version, "v2-r1") == 0,
          "a new cover_version starts again");
    np_art_retry_t e = {0};
    CHECK(np_art_end(&e, "v1-r1", true, true, true) == NP_ART_END_DROP && np_art_end(&e, "v1-r1", true, true, true) == NP_ART_END_DROP,
          "two bad: drop the set");
    CHECK(np_art_end(&e, "", false, false, false) == NP_ART_END_PUBLISH && e.bad == 2 && strcmp(e.version, "v1-r1") == 0,
          "the empty plan publishes and keeps the count");
    CHECK(np_art_end(&e, "fb43cf74b67a", false, false, false) == NP_ART_END_PUBLISH && e.bad == 2, "so does another version's success");
    CHECK(np_art_end(&e, "v1-r1", true, true, true) == NP_ART_END_NO_COVER && np_art_gave_up(&e, "v1-r1") && !np_art_gave_up(&e, "v2-r1"),
          "third bad: no cover");
    CHECK(np_art_end(&e, "v1-r1", true, false, false) == NP_ART_END_NO_COVER && e.bad == NP_ART_BAD_TRIES,
          "given up, the cover skipped: still no cover, the count kept");
    CHECK(e.fails == 0, "a no-cover publish leaves no backoff");
    CHECK(np_art_end(&e, "v1-r1", true, true, false) == NP_ART_END_DROP && e.fails == 1 && np_art_retry_ms(e.fails) == 5000 &&
              np_art_gave_up(&e, "v1-r1"),
          "given up, a failed avatar: drop and retry from a 5 s backoff");
    CHECK(np_art_end(&e, "v1-r1", true, true, false) == NP_ART_END_DROP && np_art_retry_ms(e.fails) == 10000, "and back off further");
    CHECK(np_art_end(&e, "v1-r1", true, false, false) == NP_ART_END_NO_COVER && e.fails == 0 && e.bad == NP_ART_BAD_TRIES,
          "the avatar comes: no cover, the backoff cleared, still given up");
    np_art_retry_t gu = {0};
    np_art_retry_fail(&gu, "v5-r1", true);
    np_art_retry_fail(&gu, "v5-r1", false);
    np_art_retry_fail(&gu, "v5-r1", true);
    CHECK(np_art_retry_fail(&gu, "v5-r1", true) && gu.fails == 0,
          "giving up zeroes the backoff");
    np_art_retry_t q = {0};
    np_art_end(&q, "v4-r1", true, true, true);
    np_art_end(&q, "v4-r1", true, true, true);
    CHECK(np_art_end(&q, "v4-r1", true, false, false) == NP_ART_END_PUBLISH && q.bad == 0 && q.fails == 0, "a success of the version clears it");
    np_art_retry_t h = {0};
    for (int i = 0; i < 4; i++) np_art_end(&h, "fb43cf74b67a", false, true, false);
    CHECK(np_art_end(&h, "fb43cf74b67a", false, true, false) == NP_ART_END_DROP, "the old face never gets a no-cover set");
    np_art_retry_t t = {0};
    for (int i = 0; i < 20; i++) CHECK(!np_art_retry_fail(&t, "v3-r1", false), "transient errors never give up (%d)", i);

    np_edge_style_t x, y;
    memset(&x, 0xAA, sizeof x);
    memset(&y, 0x55, sizeof y);
    np_edge_style(&x, NP_RING_PROGRESS, 0x6C22E6);
    np_edge_style(&y, NP_RING_PROGRESS, 0x6C22E6);
    CHECK(np_edge_style_eq(&x, &y) && memcmp(&x, &y, sizeof x) == 0, "equal styles compare equal whatever was on the stack");
    y.head = false;
    CHECK(!np_edge_style_eq(&x, &y), "head differs");
    np_edge_style(&y, NP_RING_VOLUME, 0x6C22E6);
    CHECK(!np_edge_style_eq(&x, &y), "volume differs");

    uint16_t px[2] = {0xF800, 0xFFFF}, g[2];
    np_grey565(px, g, 2, 0.75f);
    int gr = (g[0] >> 11) & 31, gg = (g[0] >> 5) & 63, gb = g[0] & 31;
    CHECK(abs(gr * 2 - gg) <= 1 && gr == gb && gr > 3 && gr < 10, "red to dim grey: %04x", g[0]);
    CHECK(((g[1] >> 11) & 31) >= 22 && ((g[1] >> 11) & 31) <= 24, "white to 75 %%: %04x", g[1]);

    parse(COVER_BLOCK, &a);
    CHECK(np_art_take("fb43cf74b67a-r1", true, &a, true) == NP_TAKE_ADOPT, "current cover");
    CHECK(np_art_take("old-r1", true, &a, true) == NP_TAKE_DROP, "older cover dropped");
    CHECK(np_art_take("fb43cf74b67a-r1", true, &a, false) == NP_TAKE_DROP, "a cover set on the old face");
    np_info_t none;
    parse("{\"state\":\"none\"}", &none);
    CHECK(np_art_take("", false, &none, true) == NP_TAKE_DROP, "nothing playing: the empty set on the cover face");
    CHECK(np_art_take("", false, &none, false) == NP_TAKE_ADOPT, "and on the old face, as before");
    np_info_t leg;
    parse(BLOCK, &leg);
    CHECK(np_art_take("fb43cf74b67a", false, &leg, false) == NP_TAKE_ADOPT,
          "the old face: an empty set (every picture 404) for the shown art_version clears the pictures");
    CHECK(np_art_take("0ld0ld0ld0ld", false, &leg, false) == NP_TAKE_DROP, "an empty set for an older art_version is dropped");
    CHECK(np_art_take("fb43cf74b67a-r1", true, &a, true) == NP_TAKE_ADOPT && np_art_take("old-r1", true, &a, true) == NP_TAKE_DROP,
          "a no-cover set (no pictures) goes by its version like any other");

    CHECK(np_scrim_opa(false, 0) == NP_SCRIM_LIGHT && np_scrim_opa(true, 0) == NP_SCRIM_STRONG, "playing: light and strong scrim");
    static const uint8_t SHADES[] = {77, 109, 115, 153, 191, 230};
    for (size_t i = 0; i < sizeof SHADES / sizeof SHADES[0]; i++)
        for (int light = 0; light <= 1; light++) {
            int sh = SHADES[i], want = light ? NP_SCRIM_STRONG : NP_SCRIM_LIGHT, sc = np_scrim_opa(light, (uint8_t)sh);
            int both = 255 - (255 - sh) * (255 - sc) / 255;
            int top = sh > want ? sh : want;
            CHECK(abs(both - top) <= 1, "shade %d, light %d: scrim %d stacks to %d, want %d", sh, light, sc, both, top);
        }
    CHECK(np_scrim_opa(false, 115) == 0 && np_scrim_opa(false, 153) == 0, "paused or volume: the light scrim adds nothing");
    CHECK(np_scrim_opa(true, 115) > 0 && np_scrim_opa(true, 115) < NP_SCRIM_STRONG, "paused on a light cover: less on top");

    uint16_t *nc = malloc((size_t)NP_COVER_PX * NP_COVER_PX * 2);
    memset(nc, 0xFF, (size_t)NP_COVER_PX * NP_COVER_PX * 2);
    np_no_cover_render(nc, NP_COVER_PX);
    const uint16_t grey = (uint16_t)((0x1E >> 3) << 11 | (0x1E >> 2) << 5 | (0x1E >> 3));
    CHECK(nc[233 * NP_COVER_PX + 233] == grey && nc[(233 + NP_DISC_R - 2) * NP_COVER_PX + 233] == grey, "grey disc: %04x",
          nc[233 * NP_COVER_PX + 233]);
    CHECK(nc[(233 + NP_DISC_R + 2) * NP_COVER_PX + 233] == 0 && nc[0] == 0 && nc[233 * NP_COVER_PX + 233 + NP_EDGE_R] == 0,
          "black from the disc out, the ring band too");
    free(nc);
    CHECK(np_face_kind_cover(true, &a, 0), "known: by cover_version");
    np_info_t old;
    parse(BLOCK, &old);
    CHECK(!np_face_kind_cover(true, &old, 1), "an older Ember: the old face");
    CHECK(np_face_kind_cover(false, &none, -1) && np_face_kind_cover(false, &none, 1) && !np_face_kind_cover(false, &none, 0),
          "not playing: keep the face, the cover face first");
    np_face_look_t l;
    np_face_look(NPF_PAUSED, NP_RING_PROGRESS, 0, &l);
    CHECK(l.avatar_grey, "paused: grey avatar");
    np_face_look(NPF_OFFLINE, NP_RING_PROGRESS, 0, &l);
    CHECK(l.avatar_grey, "offline: grey avatar");
    np_face_look(NPF_PLAYING, NP_RING_PROGRESS, 0, &l);
    CHECK(!l.avatar_grey, "playing: colour");
}

int main(void)
{
    test_parse();
    test_view();
    test_position();
    test_format();
    test_fold();
    test_art();
    test_ring();
    test_masks();
    test_ctl();
    test_cover_parse();
    test_cover_plan();
    test_edge();
    test_ring_mode();
    test_face_state();
    test_downscale();
    test_vinyl();
    test_review_fixes();
    if (failures) {
        printf("np: %d failure(s)\n", failures);
        return 1;
    }
    printf("np: all tests passed\n");
    return 0;
}
