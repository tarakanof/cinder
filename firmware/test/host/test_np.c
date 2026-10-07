#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "knob_view.h"
#include "np.h"
#include "np_ctl.h"

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
    CHECK(knob_view_parse("{\"mood\":{},\"nowplaying\":{\"state\":\"none\"}}", &v) && v.has_np &&
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
    if (failures) {
        printf("np: %d failure(s)\n", failures);
        return 1;
    }
    printf("np: all tests passed\n");
    return 0;
}
