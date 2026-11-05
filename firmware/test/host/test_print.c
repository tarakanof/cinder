#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "print.h"

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static const char *dir;

static char *load(const char *name)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "rb");
    if (!f) {
        failures++;
        printf("FAIL: cannot open %s\n", path);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *s = malloc((size_t)n + 1);
    size_t got = s ? fread(s, 1, (size_t)n, f) : 0;
    fclose(f);
    if (!s) return NULL;
    s[got] = 0;
    return s;
}

static bool golden(const char *name, print_block_t *b)
{
    char *j = load(name);
    cJSON *root = j ? cJSON_Parse(j) : NULL;
    bool ok = print_parse(cJSON_GetObjectItemCaseSensitive(root, "print"), b);
    cJSON_Delete(root);
    free(j);
    CHECK(ok, "%s: print block parses", name);
    return ok;
}

static bool parse(const char *json, print_block_t *b)
{
    cJSON *o = cJSON_Parse(json);
    CHECK(o != NULL, "json: %s", json);
    bool ok = print_parse(o, b);
    cJSON_Delete(o);
    return ok;
}

static void test_goldens(void)
{
    print_block_t b;
    if (golden("view_print.json", &b)) {
        CHECK(b.phase == PRINT_PRINTING && b.has_progress && b.progress == 24, "printing 24%%");
        CHECK(b.ends_at == 1782051245LL && b.started_at == 1782043200LL && b.change_at == 1782045665LL, "times");
        CHECK(!b.has_temps, "no temperatures while printing");
        CHECK(strcmp(b.name, "bracket_v3") == 0 && strcmp(b.material, "PETG") == 0 && strcmp(b.printer, "CORE One") == 0,
              "text %s %s %s", b.name, b.material, b.printer);
        CHECK(strcmp(b.thumb_version, "5d73e113fbb7-r1") == 0 && b.thumb_top == 110 && b.thumb_bottom == 300, "thumb");
        CHECK(b.event.id == 1782043205000LL && b.event.kind == PRINT_EV_STARTED && b.event.at == 1782043205LL, "event");
    }
    if (golden("view_print_heating.json", &b)) {
        CHECK(b.phase == PRINT_HEATING && b.has_progress && b.progress == 0, "heating 0%%");
        CHECK(b.has_temps && b.nozzle == 182 && b.nozzle_target == 240 && b.bed == 72 && b.bed_target == 85, "temps");
        CHECK(!b.started_at && !b.change_at && !b.thumb_version[0] && !b.thumb_top && !b.thumb_bottom, "absent keys");
    }
    if (golden("view_print_attention.json", &b)) {
        CHECK(b.phase == PRINT_ATTENTION && b.progress == 24 && !b.change_at, "attention");
        CHECK(b.event.kind == PRINT_EV_ATTENTION && b.event.id == 1782044765000LL, "attention event");
    }
}

static void test_parse_edges(void)
{
    print_block_t b;
    CHECK(!print_parse(NULL, &b) && b.phase == PRINT_UNKNOWN, "NULL");
    CHECK(!parse("[]", &b), "array");
    CHECK(!parse("{}", &b), "no phase");
    CHECK(!parse("{\"phase\":3}", &b), "phase not a string");
    CHECK(parse("{\"phase\":\"warping\"}", &b) && b.phase == PRINT_UNKNOWN, "unknown phase kept as unknown");
    CHECK(print_look(&b) == PRINT_LOOK_IDLE, "unknown phase looks idle");
    CHECK(parse("{\"phase\":\"offline\",\"printer\":\"CORE One\"}", &b) && b.phase == PRINT_OFFLINE && !b.has_progress &&
              !b.ends_at && !b.event.id,
          "offline: only the phase and label");

    static const char *const PH[] = {"offline", "idle", "heating", "preparing", "printing", "finishing",
                                     "paused", "attention", "finished", "stopped", "error"};
    for (int i = 0; i < (int)(sizeof PH / sizeof PH[0]); i++) {
        print_phase_t p = print_phase_from_wire(PH[i]);
        CHECK(p != PRINT_UNKNOWN && strcmp(print_phase_name(p), PH[i]) == 0, "phase %s round trip", PH[i]);
    }

    CHECK(parse("{\"phase\":\"printing\",\"progress\":101}", &b) && !b.has_progress, "progress over 100");
    CHECK(parse("{\"phase\":\"printing\",\"progress\":-1}", &b) && !b.has_progress, "progress under 0");
    CHECK(parse("{\"phase\":\"printing\",\"progress\":24.5}", &b) && !b.has_progress, "progress not whole");
    CHECK(parse("{\"phase\":\"printing\",\"progress\":\"24\"}", &b) && !b.has_progress, "progress a string");
    CHECK(parse("{\"phase\":\"printing\",\"progress\":100}", &b) && b.has_progress && b.progress == 100, "progress 100");
    CHECK(parse("{\"phase\":\"printing\",\"ends_at\":-5,\"started_at\":1.5,\"change_at\":\"x\"}", &b) && !b.ends_at &&
              !b.started_at && !b.change_at,
          "bad times absent");
    CHECK(parse("{\"phase\":\"heating\",\"nozzle\":180,\"nozzle_target\":240,\"bed\":60}", &b) && !b.has_temps,
          "three of four temperatures: none");
    CHECK(parse("{\"phase\":\"heating\",\"nozzle\":180,\"nozzle_target\":240,\"bed\":60,\"bed_target\":501}", &b) &&
              !b.has_temps,
          "a temperature over the cap: none");

    CHECK(parse("{\"phase\":\"printing\",\"thumb_version\":\"abc-r1\"}", &b) && !b.thumb_version[0], "thumb without rows");
    CHECK(parse("{\"phase\":\"printing\",\"thumb_rows\":[110,300]}", &b) && !b.thumb_top && !b.thumb_bottom,
          "rows without thumb");
    CHECK(parse("{\"phase\":\"printing\",\"thumb_version\":\"abc-r1\",\"thumb_rows\":[300,110]}", &b) &&
              !b.thumb_version[0],
          "rows upside down");
    CHECK(parse("{\"phase\":\"printing\",\"thumb_version\":\"abc-r1\",\"thumb_rows\":[0,467]}", &b) && !b.thumb_version[0],
          "rows past the picture");
    CHECK(parse("{\"phase\":\"printing\",\"thumb_version\":\"abc-r1\",\"thumb_rows\":[110]}", &b) && !b.thumb_version[0],
          "one row");
    CHECK(parse("{\"phase\":\"printing\",\"thumb_version\":\"a b&v=1\",\"thumb_rows\":[110,300]}", &b) &&
              !b.thumb_version[0],
          "thumb_version that is not URL-safe");
    CHECK(parse("{\"phase\":\"printing\",\"thumb_version\":\"0123456789012345678901234567890123456789x\","
                "\"thumb_rows\":[110,300]}",
                &b) &&
              !b.thumb_version[0],
          "thumb_version over 40");
    CHECK(parse("{\"phase\":\"printing\",\"thumb_version\":\"0123456789012345678901234567890123456789\","
                "\"thumb_rows\":[0,466]}",
                &b) &&
              strlen(b.thumb_version) == 40 && b.thumb_top == 0 && b.thumb_bottom == 466,
          "thumb_version at 40, rows the whole picture");

    CHECK(parse("{\"phase\":\"idle\",\"event\":{\"kind\":\"finished\",\"at\":5}}", &b) && !b.event.id, "event without id");
    CHECK(parse("{\"phase\":\"idle\",\"event\":{\"id\":7,\"kind\":\"exploded\",\"at\":5}}", &b) && b.event.id == 7 &&
              b.event.kind == PRINT_EV_UNKNOWN && b.event.at == 5,
          "unknown event kind keeps its id");
    CHECK(parse("{\"phase\":\"idle\",\"event\":{\"id\":9007199254740992,\"kind\":\"error\"}}", &b) &&
              b.event.id == 9007199254740992LL && b.event.kind == PRINT_EV_ERROR && !b.event.at,
          "event id at 2^53, no at");
    static const char *const EV[] = {"started", "attention", "finished", "stopped", "error"};
    for (int i = 0; i < 5; i++) CHECK((int)print_ev_kind_from_wire(EV[i]) == PRINT_EV_STARTED + i, "event kind %s", EV[i]);
}

static void test_size_limits(void)
{
    char name[300], mat[120], prn[300], json[1200];
    for (int i = 0; i < 32; i++) memcpy(name + 4 * i, "\xF0\x9F\x96\xA8", 4);
    name[128] = 0;
    for (int i = 0; i < 16; i++) memcpy(mat + 4 * i, "\xF0\x9F\x96\xA8", 4);
    mat[64] = 0;
    for (int i = 0; i < 128; i++) prn[i] = 'p';
    prn[128] = 0;
    print_block_t b;
    snprintf(json, sizeof json, "{\"phase\":\"printing\",\"name\":\"%s\",\"material\":\"%s\",\"printer\":\"%s\"}", name, mat, prn);
    CHECK(parse(json, &b) && strcmp(b.name, name) == 0 && strcmp(b.material, mat) == 0 && strcmp(b.printer, prn) == 0,
          "32 runes, 16 runes and 128 B fit whole");

    snprintf(json, sizeof json, "{\"phase\":\"printing\",\"name\":\"x%s\",\"material\":\"x%s\",\"printer\":\"%sq\"}", name, mat, prn);
    CHECK(parse(json, &b), "over the caps parses");
    CHECK(strlen(b.name) == 125 && b.name[0] == 'x' && memcmp(b.name + 1, name, 124) == 0, "name cut at a rune: %zu B",
          strlen(b.name));
    CHECK(strlen(b.material) == 61 && b.material[0] == 'x', "material cut at a rune: %zu B", strlen(b.material));
    CHECK(strlen(b.printer) == 128 && b.printer[127] == 'p', "printer cut at 128 B");

    snprintf(json, sizeof json, "{\"phase\":\"printing\",\"name\":\"a\\u0000b\"}");
    CHECK(parse(json, &b) && strcmp(b.name, "a") == 0, "NUL inside a name ends it");
}

static void test_left(void)
{
    print_block_t b = {.phase = PRINT_PRINTING, .ends_at = 100000};
    CHECK(print_left_s(&b, 100000 - 6120) == 6120, "102 min left");
    CHECK(print_left_min(&b, 100000 - 6120) == 102, "102 min");
    CHECK(print_left_min(&b, 100000 - 6120 - 0.5) == 103, "a breath over 102 min rounds up");
    CHECK(print_left_min(&b, 100000 - 6061) == 102, "6061 s: 102 min");
    CHECK(print_left_min(&b, 100000 - 6060) == 101, "the minute boundary: 101 min");
    CHECK(print_left_min(&b, 100000 - 1) == 1 && print_left_min(&b, 100000) == 0 && print_left_min(&b, 100500) == 0,
          "end and past it");
    CHECK(print_left_min(&b, 0) == -1, "no server time");
    b.ends_at = 0;
    CHECK(print_left_min(&b, 1000) == -1 && print_left_s(&b, 1000) == -1, "no estimate");

    char v[16];
    const char *u;
    print_left_text(102, v, sizeof v, &u);
    CHECK(strcmp(v, "1:42") == 0 && strcmp(u, "h left") == 0, "1:42 h left");
    print_left_text(60, v, sizeof v, &u);
    CHECK(strcmp(v, "1:00") == 0 && strcmp(u, "h left") == 0, "1:00 h left");
    print_left_text(59, v, sizeof v, &u);
    CHECK(strcmp(v, "59") == 0 && strcmp(u, "min left") == 0, "59 min left");

    char t[8];
    print_hhmm(1782051245LL, 0, t, sizeof t);
    CHECK(strcmp(t, "14:14") == 0, "UTC %s", t);
    print_hhmm(1782051245LL, 3600, t, sizeof t);
    CHECK(strcmp(t, "15:14") == 0, "UTC+1 %s", t);
    print_hhmm(30, -3600, t, sizeof t);
    CHECK(strcmp(t, "23:00") == 0, "before midnight %s", t);

    char d[16];
    print_duration_text(8040, d, sizeof d);
    CHECK(strcmp(d, "2 h 14 m") == 0, "%s", d);
    print_duration_text(840, d, sizeof d);
    CHECK(strcmp(d, "14 min") == 0, "%s", d);
}

static void test_ticks_each_minute(void)
{
    print_block_t b = {.phase = PRINT_PRINTING, .has_progress = true, .progress = 24, .ends_at = 1782051245LL};
    print_text_t t;
    double now = 1782051245.0 - 6061;
    print_text(&b, PRINT_FACE_K1, now, true, 3600, &t);
    CHECK(strcmp(t.val, "1:42") == 0 && strcmp(t.unit, "h left") == 0, "K1 before the boundary: %s %s", t.val, t.unit);
    CHECK(strcmp(t.sub, "done 15:14") == 0 && t.sub_col == PRINT_COL_ACCENT, "ready time %s", t.sub);
    print_text(&b, PRINT_FACE_K1, now + 1, true, 3600, &t);
    CHECK(strcmp(t.val, "1:41") == 0, "one second later, across the minute: %s", t.val);
    print_text(&b, PRINT_FACE_K1, now + 59, true, 3600, &t);
    CHECK(strcmp(t.val, "1:41") == 0, "same minute: %s", t.val);
    print_text(&b, PRINT_FACE_K1, now + 61, true, 3600, &t);
    CHECK(strcmp(t.val, "1:40") == 0, "next minute: %s", t.val);
    print_text(&b, PRINT_FACE_K1, now, false, 0, &t);
    CHECK(strcmp(t.sub, "done in 1 h 42 m") == 0, "no zone: %s", t.sub);
    print_text(&b, PRINT_FACE_K2, now, true, 3600, &t);
    CHECK(strcmp(t.val, "24") == 0 && strcmp(t.unit, "%") == 0 && strcmp(t.sub, "1:42 left \xE2\x80\xA2 15:14") == 0,
          "K2: %s%s %s", t.val, t.unit, t.sub);
    b.change_at = (long long)now + 300;
    print_text(&b, PRINT_FACE_K1, now, true, 3600, &t);
    CHECK(strcmp(t.sub, "colour change: 5 min") == 0 && t.sub_col == PRINT_COL_WAIT, "colour change soon: %s", t.sub);
    print_text(&b, PRINT_FACE_K2, now, true, 3600, &t);
    CHECK(strcmp(t.sub, "change in 5 min") == 0 && t.sub_col == PRINT_COL_WAIT, "K2 colour change soon: %s", t.sub);
    b.change_at = (long long)now + 3600;
    print_text(&b, PRINT_FACE_K1, now, true, 3600, &t);
    CHECK(strcmp(t.sub, "done 15:14") == 0, "colour change later: %s", t.sub);
}

static void test_phase_face(void)
{
    static const struct {
        const char *phase;
        print_look_t look;
        uint32_t ring;
        bool head, pulse;
    } W[] = {
        {"offline", PRINT_LOOK_OFFLINE, PRINT_COL_TRACK, false, false},
        {"idle", PRINT_LOOK_IDLE, PRINT_COL_TRACK, false, false},
        {"heating", PRINT_LOOK_HEATING, PRINT_COL_HEAT, true, false},
        {"preparing", PRINT_LOOK_PROGRESS, PRINT_COL_ACCENT, true, false},
        {"printing", PRINT_LOOK_PROGRESS, PRINT_COL_ACCENT, true, false},
        {"finishing", PRINT_LOOK_FINISHING, PRINT_COL_ACCENT, true, false},
        {"paused", PRINT_LOOK_PAUSED, 0x80451F, false, false},
        {"attention", PRINT_LOOK_ATTENTION, PRINT_COL_WAIT, true, true},
        {"finished", PRINT_LOOK_DONE, PRINT_COL_DONE, false, false},
        {"stopped", PRINT_LOOK_STOPPED, PRINT_COL_GREY, false, false},
        {"error", PRINT_LOOK_ERROR, PRINT_COL_ERR, false, true},
    };
    for (int i = 0; i < (int)(sizeof W / sizeof W[0]); i++) {
        print_block_t b = {.phase = print_phase_from_wire(W[i].phase)};
        print_ring_look_t r;
        print_ring_look(print_look(&b), &r);
        CHECK(print_look(&b) == W[i].look, "%s look %d", W[i].phase, print_look(&b));
        CHECK(r.col == W[i].ring && r.head == W[i].head && r.pulse == W[i].pulse, "%s ring %06X head %d pulse %d",
              W[i].phase, (unsigned)r.col, r.head, r.pulse);
        CHECK(print_face_open(&b) == PRINT_FACE_K1, "%s opens on K1", W[i].phase);
    }
    CHECK(print_look(NULL) == PRINT_LOOK_NONE, "no block");
    print_text_t t;
    print_text(NULL, PRINT_FACE_K1, 0, false, 0, &t);
    CHECK(strcmp(t.word, "Printer off") == 0 && t.word_col == PRINT_COL_DIMWORD && !t.val[0], "no block: dim Printer off");

    print_block_t b = {.phase = PRINT_HEATING, .has_progress = true, .has_temps = true, .nozzle = 182, .nozzle_target = 240,
                       .bed = 72, .bed_target = 85};
    strcpy(b.name, "bracket_v3");
    strcpy(b.material, "PETG");
    print_text(&b, PRINT_FACE_K1, 0, false, 0, &t);
    CHECK(strcmp(t.val, "182\xC2\xB0") == 0 && strcmp(t.unit, "/ 240\xC2\xB0") == 0, "heating value %s %s", t.val, t.unit);
    CHECK(strcmp(t.sub, "heating \xE2\x80\xA2 bed 72\xC2\xB0") == 0 && t.sub_col == PRINT_COL_HEAT, "heating sub %s", t.sub);
    CHECK(strcmp(t.small, "bracket_v3 \xE2\x80\xA2 PETG") == 0, "file and material %s", t.small);
    float k = print_ring_frac(&b);
    CHECK(fabsf(k - (0.6f * 157 / 215 + 0.4f * 50 / 63)) < 1e-4f, "heat-up ring %f", k);
    print_text(&b, PRINT_FACE_K2, 0, false, 0, &t);
    CHECK(strcmp(t.word, "Heating 182\xC2\xB0") == 0 && strcmp(t.sub, "bed 72\xC2\xB0 / 85\xC2\xB0") == 0 && !t.small[0],
          "K2 heating %s / %s", t.word, t.sub);

    b = (print_block_t){.phase = PRINT_ATTENTION, .has_progress = true, .progress = 46};
    print_text(&b, PRINT_FACE_K1, 0, false, 0, &t);
    CHECK(strcmp(t.word, "Check printer") == 0 && t.word_col == PRINT_COL_WAIT &&
              strcmp(t.sub, "paused \xE2\x80\xA2 46% done") == 0,
          "attention %s / %s", t.word, t.sub);
    b.progress = 0;
    print_text(&b, PRINT_FACE_K1, 0, false, 0, &t);
    CHECK(strcmp(t.sub, "see the printer") == 0, "attention after a job %s", t.sub);

    b = (print_block_t){.phase = PRINT_FINISHED, .has_progress = true, .progress = 100, .started_at = 1000,
                        .event = {.id = 1, .kind = PRINT_EV_FINISHED, .at = 1000 + 8040}};
    print_text(&b, PRINT_FACE_K1, 0, false, 0, &t);
    CHECK(strcmp(t.word, "Done") == 0 && t.word_col == PRINT_COL_DONE &&
              strcmp(t.sub, "2 h 14 m \xE2\x80\xA2 take it out") == 0,
          "done %s / %s", t.word, t.sub);
    CHECK(print_ring_frac(&b) == 1, "done: full ring");
    print_text(&b, PRINT_FACE_K2, 0, false, 0, &t);
    CHECK(strcmp(t.sub, "took 2 h 14 m") == 0, "K2 done %s", t.sub);
    b.event.kind = PRINT_EV_STARTED;
    CHECK(print_took_s(&b) == -1, "no finish event: no length");
    print_text(&b, PRINT_FACE_K1, 0, false, 0, &t);
    CHECK(strcmp(t.sub, "take it out") == 0, "done without a length %s", t.sub);

    b = (print_block_t){.phase = PRINT_PREPARING, .has_progress = true};
    print_text(&b, PRINT_FACE_K1, 1e9, false, 0, &t);
    CHECK(strcmp(t.word, "Preparing") == 0 && !t.val[0], "preparing without an estimate %s", t.word);
    b = (print_block_t){.phase = PRINT_STOPPED, .has_progress = true, .progress = 46};
    print_text(&b, PRINT_FACE_K2, 0, false, 0, &t);
    CHECK(strcmp(t.word, "Stopped") == 0 && strcmp(t.sub, "at 46%") == 0, "stopped %s %s", t.word, t.sub);
    CHECK(fabsf(print_ring_frac(&b) - 0.46f) < 1e-6f, "stopped ring at 46%%");
    b = (print_block_t){.phase = PRINT_OFFLINE};
    strcpy(b.printer, "CORE One");
    print_text(&b, PRINT_FACE_K1, 0, false, 0, &t);
    CHECK(strcmp(t.word, "Printer off") == 0 && strcmp(t.sub, "CORE One") == 0 && print_ring_frac(&b) == 0, "offline");
}

static void test_link(void)
{
    print_text_t t;
    print_block_t b = {.phase = PRINT_PRINTING, .has_progress = true, .progress = 24, .ends_at = 100000};
    strcpy(b.name, "bracket_v3");
    print_text(&b, PRINT_FACE_K1, 100000 - 6120, true, 0, &t);
    print_text_link(&t, true, false);
    CHECK(strncmp(t.sub, "done", 4) == 0 && t.small[0], "online: unchanged");
    print_text_link(&t, true, true);
    CHECK(strcmp(t.sub, "offline") == 0 && t.sub_col == PRINT_COL_SUB && !t.small[0] && strcmp(t.val, "1:42") == 0,
          "offline: the last value, the sub-line says offline, no file line");
    print_text_link(&t, false, true);
    CHECK(strcmp(t.word, "Connecting") == 0 && !t.val[0] && !t.sub[0], "before the first view: Connecting");
}

static void test_turns(void)
{
    print_block_t b = {.phase = PRINT_PRINTING};
    print_faces_t f;
    print_faces_open(&f);
    CHECK(print_faces_shown(&f, &b) == PRINT_FACE_K1, "opens on K1");
    CHECK(!print_faces_turn(&f, &b, 0), "no detent");
    CHECK(print_faces_turn(&f, &b, 1) && print_faces_shown(&f, &b) == PRINT_FACE_K2, "K1 -> K2");
    CHECK(print_faces_turn(&f, &b, 1) && print_faces_shown(&f, &b) == PRINT_FACE_K1, "K2 wraps to K1");
    CHECK(print_faces_turn(&f, &b, -1) && print_faces_shown(&f, &b) == PRINT_FACE_K2, "back from K1 wraps to K2");
    CHECK(!print_faces_turn(&f, &b, 2) && print_faces_shown(&f, &b) == PRINT_FACE_K2, "two detents: same face");
    CHECK(print_faces_turn(&f, &b, -3) && print_faces_shown(&f, &b) == PRINT_FACE_K1, "three back");
    print_faces_turn(&f, &b, 1);
    b.phase = PRINT_FINISHED;
    CHECK(print_faces_shown(&f, &b) == PRINT_FACE_K2, "the pick holds over a phase change");
    print_faces_open(&f);
    CHECK(print_faces_shown(&f, &b) == PRINT_FACE_K1, "until the next open");
}

static void test_clip(void)
{
    CHECK(print_clip_row(110, 300, 0) == 300, "0%%: all ghost");
    CHECK(print_clip_row(110, 300, 100) == 110, "100%%: all solid");
    CHECK(print_clip_row(110, 300, 24) == 300 - 190 * 24 / 100, "24%%: %d", print_clip_row(110, 300, 24));
    CHECK(print_clip_row(110, 300, 50) == 205, "50%%");
    CHECK(print_clip_row(110, 300, -5) == 300 && print_clip_row(110, 300, 120) == 110, "clamped");
    int last = 301;
    for (int p = 0; p <= 100; p++) {
        int c = print_clip_row(200, 300, p);
        CHECK(c <= last && c >= 200 && c <= 300, "monotonic at %d", p);
        last = c;
    }
}

#define W 466
static uint16_t s_buf[W * W];

static int lit_rows(int x, int y0, int y1, uint16_t want)
{
    int n = 0;
    for (int y = y0; y < y1; y++) n += s_buf[y * W + x] == want;
    return n;
}

static int first_row(int x, int y0, int y1, uint16_t want)
{
    for (int y = y0; y < y1; y++)
        if (s_buf[y * W + x] == want) return y;
    return -1;
}

static uint16_t c565(uint32_t c) { return (uint16_t)((((c >> 16) & 0xFF) >> 3) << 11 | (((c >> 8) & 0xFF) >> 2) << 5 | ((c & 0xFF) >> 3)); }

static void test_hero(void)
{
    print_hero_t h = {.cx = 233, .cy = 233, .sc = 2};
    print_rect_t box = print_hero_box(&h);
    CHECK(box.x <= 233 - 120 && box.x + box.w >= 233 + 120 && box.y <= 233 - 112 && box.y + box.h >= 233 + 112 &&
              box.x % 2 == 0 && box.y % 2 == 0 && box.w % 2 == 0 && box.h % 2 == 0,
          "hero box %d,%d %dx%d", box.x, box.y, box.w, box.h);
    print_rect_t head = print_rect_union(print_hero_head_at(&h, -20), print_hero_head_at(&h, 20));
    CHECK(head.y >= box.y && head.y + head.h <= box.y + box.h && head.h < box.h / 3, "the head's sweep is a strip: %d+%d",
          head.y, head.h);

    uint16_t bed = c565(0x5A5A5A), head_col = c565(0xD8D8D8);
    int bed_y[3], head_y[3];
    float prog[3] = {0.1f, 0.5f, 1.0f};
    for (int i = 0; i < 3; i++) {
        memset(s_buf, 0, sizeof s_buf);
        print_pose_t p = {.look = PRINT_LOOK_PROGRESS, .progress = prog[i], .head_x = 0, .pulse = 1};
        print_hero_render(&h, &p, s_buf, W, W, box);
        bed_y[i] = first_row(233 + 70, box.y, box.y + box.h, bed);
        head_y[i] = first_row(233, box.y, box.y + box.h, head_col);
    }
    CHECK(bed_y[0] > 0 && bed_y[1] > bed_y[0] && bed_y[2] > bed_y[1], "the bed sinks as the part grows: %d %d %d", bed_y[0],
          bed_y[1], bed_y[2]);
    CHECK(head_y[0] > 0 && head_y[0] == head_y[1] && head_y[1] == head_y[2], "the head stays at the top: %d %d %d", head_y[0],
          head_y[1], head_y[2]);
    CHECK(abs((bed_y[2] - bed_y[0]) - (int)lroundf(48 * 0.9f * 2)) <= 2, "bed travel %d", bed_y[2] - bed_y[0]);
    int part = lit_rows(233, box.y, box.y + box.h, c565(PRINT_COL_FIL)) +
               lit_rows(233, box.y, box.y + box.h, c565(print_scale(PRINT_COL_FIL, 0.85f)));
    CHECK(part >= 90 && part <= 100, "a full part is 96 px tall at scale 2: %d", part);

    memset(s_buf, 0, sizeof s_buf);
    print_pose_t heat = {.look = PRINT_LOOK_HEATING, .progress = 0.5f, .heat = 1, .pulse = 1};
    print_hero_render(&h, &heat, s_buf, W, W, box);
    CHECK(lit_rows(233, box.y, box.y + box.h, c565(PRINT_COL_FIL)) == 0, "no part while heating");
    CHECK(first_row(233 + 70, box.y, box.y + box.h, c565(print_mix(0x555555, PRINT_COL_HEAT, 1))) > 0, "the bed glows");

    for (int k = 0; k < 2; k++) {
        float hx = k ? 20 : -20;
        print_pose_t a = {.look = PRINT_LOOK_PROGRESS, .progress = 0.4f, .head_x = hx, .pulse = 1};
        static uint16_t full[W * W];
        memset(full, 0, sizeof full);
        print_hero_render(&h, &a, full, W, W, box);
        memset(s_buf, 0, sizeof s_buf);
        print_pose_t b0 = a;
        b0.head_x = -hx;
        print_hero_render(&h, &b0, s_buf, W, W, box);
        print_hero_render(&h, &a, s_buf, W, W, head);
        CHECK(memcmp(full, s_buf, sizeof full) == 0, "a head move redraws only the head box (head at %.0f)", hx);
    }
    CHECK(print_head_x(PRINT_PRINTING, 0) == 0 && fabsf(print_head_x(PRINT_PRINTING, 0.6)) > 10,
          "the head sweeps while printing");
    CHECK(print_head_x(PRINT_PREPARING, 0.6) == print_head_x(PRINT_PREPARING, 1.2), "still while preparing");
    CHECK(print_head_x(PRINT_FINISHED, 1) == print_head_x(PRINT_FINISHED, 2), "parked when done");
    CHECK(print_head_x(PRINT_HEATING, 0.6) == 0, "centred while heating");

    for (int k = 0; k < 3; k++) {
        float from = k == 0 ? -20 : k == 1 ? 3 : 19.5f, to = k == 0 ? -16.4f : k == 1 ? 6.2f : 20;
        static uint16_t full[W * W];
        print_pose_t a = {.look = PRINT_LOOK_PROGRESS, .progress = 0.4f, .head_x = to, .pulse = 1};
        memset(full, 0, sizeof full);
        print_hero_render(&h, &a, full, W, W, box);
        print_pose_t b0 = a;
        b0.head_x = from;
        memset(s_buf, 0, sizeof s_buf);
        print_hero_render(&h, &b0, s_buf, W, W, box);
        print_rect_t r = print_rect_union(print_hero_head_at(&h, from), print_hero_head_at(&h, to));
        print_hero_render(&h, &a, s_buf, W, W, r);
        CHECK(memcmp(full, s_buf, sizeof full) == 0 && r.w * r.h < head.w * head.h / 2,
              "a head step redraws only the old and new head (%.1f -> %.1f, %d px)", from, to, r.w * r.h);
    }
    {
        static uint16_t full[W * W];
        print_pose_t cold = {.look = PRINT_LOOK_HEATING, .heat = 0.2f, .pulse = 1}, hot = cold;
        hot.heat = 0.9f;
        memset(full, 0, sizeof full);
        print_hero_render(&h, &hot, full, W, W, box);
        memset(s_buf, 0, sizeof s_buf);
        print_hero_render(&h, &cold, s_buf, W, W, box);
        print_rect_t r = print_hero_heat_box(&h);
        print_hero_render(&h, &hot, s_buf, W, W, r);
        CHECK(memcmp(full, s_buf, sizeof full) == 0 && r.w * r.h < box.w * box.h / 4,
              "a heat change redraws only the bed and nozzle (%d of %d px)", r.w * r.h, box.w * box.h);
    }
    print_rect_t u = print_rect_union((print_rect_t){10, 10, 4, 4}, (print_rect_t){0, 0, 0, 0});
    CHECK(u.x == 10 && u.w == 4, "an empty box adds nothing");
    float dx, dy;
    print_dot_xy(0, 2, 233, 233, &dx, &dy);
    CHECK(hypotf(dx - 233, dy - 233) + PRINT_DOT_SIZE + 1 < 209, "the dots stay inside the ring layer (r 209)");
    CHECK(print_pulse(PRINT_LOOK_PROGRESS, 1) == 1 && print_pulse(PRINT_LOOK_ATTENTION, 0) > 0.6f &&
              print_pulse(PRINT_LOOK_ATTENTION, 0.94) < 0.4f,
          "attention pulses");

    memset(s_buf, 0, sizeof s_buf);
    print_pose_t at = {.look = PRINT_LOOK_ATTENTION, .progress = 0.4f, .head_x = 30, .pulse = 1};
    print_hero_render(&h, &at, s_buf, W, W, box);
    CHECK(lit_rows(233, box.y, box.y + box.h, c565(PRINT_COL_WAIT)) > 30, "attention shows the amber !");
    print_rect_t off = {0, 0, 4, 4};
    s_buf[0] = 0x1234;
    print_hero_render(&h, &at, s_buf, W, W, off);
    CHECK(s_buf[0] == 0, "outside the drawing is black");
}

static void test_picture_rows(void)
{
    uint16_t ghost[16], dst[16];
    for (int x = 0; x < 16; x++) ghost[x] = x >= 4 && x < 12 ? 0x4208 : 0;
    memcpy(dst, ghost, sizeof dst);
    print_hot_line(ghost, dst, 16, 0.5f);
    int lit = 0;
    for (int x = 0; x < 16; x++) lit += dst[x] != ghost[x];
    CHECK(lit == 4 && dst[4] != ghost[4] && dst[7] != ghost[7] && dst[8] == ghost[8], "half the model's row is hot: %d", lit);
    CHECK(dst[0] == 0 && dst[3] == 0, "no hot pixels off the model");
    memcpy(dst, ghost, sizeof dst);
    print_hot_line(ghost, dst, 16, 1);
    lit = 0;
    for (int x = 0; x < 16; x++) lit += dst[x] != ghost[x];
    CHECK(lit == 8, "the whole layer at k 1: %d", lit);
    memcpy(dst, ghost, sizeof dst);
    print_hot_line(ghost, dst, 16, 0);
    CHECK(memcmp(dst, ghost, sizeof dst) == 0, "nothing at k 0");

    uint16_t src[4] = {0, 0x8410, 0xFFFF, 0x0841}, out[4];
    print_glint_row(src, out, 4, 1);
    CHECK(out[0] == 0 && out[1] != src[1] && out[2] == 0xFFFF, "the glint lights the model, not the black");
    print_glint_row(src, out, 4, 0);
    CHECK(memcmp(out, src, sizeof src) == 0, "no glint at 0");
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s <fixture dir>\n", argv[0]);
        return 2;
    }
    dir = argv[1];
    test_goldens();
    test_parse_edges();
    test_size_limits();
    test_left();
    test_ticks_each_minute();
    test_phase_face();
    test_link();
    test_turns();
    test_clip();
    test_hero();
    test_picture_rows();
    if (failures) {
        printf("print: %d failure(s)\n", failures);
        return 1;
    }
    printf("print: all tests passed\n");
    return 0;
}
