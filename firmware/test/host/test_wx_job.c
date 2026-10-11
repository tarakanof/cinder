#include <math.h>
#include <stdio.h>
#include <string.h>

#include "wx_job.h"
#include "wx_temp.h"

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

enum { RACE_NONE, RACE_TAKE, RACE_STEP };

static int s_race_at, s_race_mode, s_race_fired, s_race_acts;
static bool s_race_took;
static wx_temp_t s_race_in, s_race_step_t;

void wj_race_point(wx_job_t *j, int at);
void wj_race_point(wx_job_t *j, int at)
{
    if (at != s_race_at || s_race_fired) return;
    s_race_fired = at;
    if (s_race_mode == RACE_TAKE) {
        const wx_temp_t *in = wx_job_take(j);
        s_race_took = in != NULL;
        if (in) s_race_in = *in;
    } else if (s_race_mode == RACE_STEP) {
        s_race_acts = wx_job_step(j, &s_race_step_t, 1.0 / 30);
    }
}

static wx_temp_t key(int lo, int hi, int band)
{
    wx_temp_t t;
    memset(&t, 0, sizeof t);
    t.have = t.has_range = true;
    t.value = lo;
    t.lo = lo;
    t.hi = hi;
    t.cloud_band = band;
    t.hero = WX_HERO_SUN;
    return t;
}

static wx_job_t J;
static wx_job_io_t IO;

static void shown(void)
{
    wx_job_init(&J, &IO);
    CHECK(wx_job_show(&J, true) == WX_DO_ACQUIRE, "show allocates");
    wx_job_acquired(&J, true);
}

static void bake(void)
{
    const wx_temp_t *in = wx_job_take(&J);
    CHECK(in != NULL, "a queued job is taken");
    if (in) IO.out = (wx_bake_out_t){.hero = {0, 0, in->lo + 100, in->hi + 100}};
    wx_job_done(&J, 7);
}

static void baked(wx_temp_t t)
{
    shown();
    CHECK(wx_job_step(&J, &t, 0.03) == WX_DO_BAKE, "the first bake starts without a detach");
    bake();
    CHECK(wx_job_step(&J, &t, 0.03) == (WX_DO_APPLY | WX_DO_PAINT), "a finished bake applies and paints");
}

static int fade_out(const wx_temp_t *t)
{
    int last = 255, frames = 0, f;
    bool down = true;
    while ((f = wx_job_step(&J, t, 1.0 / 30)) == WX_DO_PAINT && frames < 20) {
        if (J.opa > last || J.opa % 85) down = false;
        last = J.opa;
        frames++;
    }
    CHECK(down && last == 0 && frames >= 5 && frames <= 7, "fade out in 3 steps of 85 to one black step (%d frames, last opa %d)",
          frames, last);
    return f;
}

static void test_first_bake(void)
{
    wx_temp_t a = key(3, 12, 1);
    shown();
    CHECK(wx_job_step(&J, &a, 0.03) == WX_DO_BAKE, "first step queues the bake");
    CHECK(wx_job_step(&J, &a, 0.03) == 0, "no paint while queued");
    CHECK(wx_job_take(&J) != NULL && wx_job_take(&J) == NULL, "a job is taken once");
    CHECK(wx_job_step(&J, &a, 0.03) == 0, "no paint while running");
    wx_job_done(&J, 7);
    CHECK(wx_job_step(&J, &a, 0.03) == (WX_DO_APPLY | WX_DO_PAINT), "done: apply and paint");
    CHECK(IO.key.lo == 3 && IO.key.hi == 12 && IO.ms == 7, "the applied key and time are the job's");
    CHECK(wx_job_step(&J, &a, 0.03) == WX_DO_PAINT, "same key: paint, no bake");
    wx_temp_t v = a;
    v.value = 9;
    v.frac = 0.5f;
    CHECK(wx_job_step(&J, &v, 0.03) == WX_DO_PAINT, "a new value alone does not re-bake");
}

static void test_rebake_detaches(void)
{
    wx_temp_t a = key(3, 12, 1), b = key(-2, 12, 3);
    baked(a);
    CHECK(fade_out(&b) == (WX_DO_BAKE | WX_DO_DETACH), "a scene re-bake on a shown face fades out, then detaches");
    CHECK(wx_job_step(&J, &b, 0.03) == 0, "no paint while a re-bake is queued");
    const wx_temp_t *in = wx_job_take(&J);
    CHECK(in && in->lo == -2 && in->cloud_band == 3, "the re-bake carries the new key");
    CHECK(wx_job_step(&J, &b, 0.03) == 0, "no paint while a re-bake runs");
    CHECK(wx_job_step(&J, &a, 0.03) == 0, "no new job or paint while one runs");
    wx_job_done(&J, 5);
    CHECK(wx_job_step(&J, &b, 1.0 / 30) == (WX_DO_APPLY | WX_DO_PAINT) && J.opa == 85, "a re-bake applies, then fades in");
    CHECK(IO.key.lo == -2 && IO.kind == WX_BAKE_FULL, "the re-bake's key is applied");
    int frames = 0, last = J.opa;
    bool up = true;
    while (J.opa < 255 && frames < 20) {
        CHECK(wx_job_step(&J, &b, 1.0 / 30) == WX_DO_PAINT, "paint while fading in");
        if (J.opa < last) up = false;
        last = J.opa;
        frames++;
    }
    CHECK(up && J.opa == 255 && frames >= 3 && frames <= 5, "fade in over 3 steps (%d frames)", frames);
}

static void test_rain_face_switch(void)
{
    wx_temp_t a = key(3, 12, 1), r = a;
    r.rain = (wx_rain_t){.on = true, .kind = WX_PRECIP_RAIN, .intensity = 2, .has_pop = true, .pop = 40};
    baked(a);
    CHECK(fade_out(&r) == (WX_DO_BAKE | WX_DO_DETACH), "rain starts: fade out, detach, bake the Rain now face");
    const wx_temp_t *in = wx_job_take(&J);
    CHECK(in && in->rain.on && IO.kind == WX_BAKE_FULL, "the job is the full rain scene");
    wx_job_done(&J, 9);
    CHECK(wx_job_step(&J, &r, 0.03) == (WX_DO_APPLY | WX_DO_PAINT) && J.opa == 85, "applied, fading in");
    while (J.opa < 255) wx_job_step(&J, &r, 1.0 / 15);
    wx_temp_t p = r;
    p.rain.pop = 90;
    CHECK(wx_job_step(&J, &p, 0.03) == WX_DO_PAINT, "a new chance only moves the marker");
    p.rain.has_pop = false;
    CHECK(wx_job_step(&J, &p, 0.03) == (WX_DO_BAKE | WX_DO_PAINT), "the chance goes: ring bake, still painting");
    wx_job_take(&J);
    wx_job_done(&J, 1);
    CHECK(wx_job_step(&J, &p, 0.03) == (WX_DO_APPLY | WX_DO_PAINT) && IO.kind == WX_BAKE_RING, "ring applied");
    CHECK(fade_out(&a) == (WX_DO_BAKE | WX_DO_DETACH), "rain stops: back to the Temperature face through black");
}

static wx_temp_t rain_face(wx_temp_t t)
{
    t.face = WXF_RAIN;
    t.rain = (wx_rain_t){.on = true, .dry = true, .kind = WX_PRECIP_RAIN, .intensity = 1, .has_pop = true, .pop = 20};
    return t;
}

static void test_user_switch_cuts(void)
{
    wx_temp_t a = key(3, 12, 1), r = rain_face(a), b = key(3, 12, 3);
    baked(a);
    wx_job_cut(&J);
    CHECK(wx_job_step(&J, &r, 1.0 / 30) == (WX_DO_BAKE | WX_DO_DETACH | WX_DO_ARRIVE) && J.opa == 255,
          "a turn to another face: detach at once, no fade out, replay the arrival");
    const wx_temp_t *in = wx_job_take(&J);
    CHECK(in && in->face == WXF_RAIN && in->rain.dry && IO.kind == WX_BAKE_FULL, "the job is the full picked face");
    CHECK(wx_job_step(&J, &r, 1.0 / 30) == 0, "black while it bakes");
    wx_job_done(&J, 4);
    CHECK(wx_job_step(&J, &r, 1.0 / 30) == (WX_DO_APPLY | WX_DO_PAINT) && J.opa == 255, "lands at full opacity, no fade in");
    CHECK(wx_job_step(&J, &r, 1.0 / 30) == WX_DO_PAINT && J.opa == 255, "then paints");
    CHECK(fade_out(&b) == (WX_DO_BAKE | WX_DO_DETACH), "a data change after it fades through black again");

    baked(a);
    for (int i = 0; i < 2; i++) CHECK(wx_job_step(&J, &b, 1.0 / 30) == WX_DO_PAINT, "a data scene change fades out");
    CHECK(J.opa < 255 && J.out, "mid fade out (%d)", J.opa);
    wx_job_cut(&J);
    CHECK(wx_job_step(&J, &r, 1.0 / 30) == (WX_DO_BAKE | WX_DO_DETACH | WX_DO_ARRIVE) && J.opa == 255 && !J.out,
          "a turn mid fade out cuts the rest");
    bake();
    CHECK(wx_job_step(&J, &r, 1.0 / 30) == (WX_DO_APPLY | WX_DO_PAINT) && J.opa == 255, "and lands without a fade in");

    baked(a);
    wx_temp_t c = key(-2, 12, 1);
    CHECK(wx_job_step(&J, &c, 1.0 / 30) == (WX_DO_BAKE | WX_DO_PAINT), "a ring bake starts");
    CHECK(wx_job_take(&J) != NULL, "and runs");
    wx_job_cut(&J);
    wx_temp_t rc = rain_face(c);
    CHECK(wx_job_step(&J, &rc, 1.0 / 30) == WX_DO_PAINT, "a turn while it runs waits for it");
    wx_job_done(&J, 1);
    CHECK(wx_job_step(&J, &rc, 1.0 / 30) == WX_DO_APPLY, "the ring lands without a paint");
    CHECK(wx_job_step(&J, &rc, 1.0 / 30) == (WX_DO_BAKE | WX_DO_DETACH | WX_DO_ARRIVE), "then the cut");

    baked(a);
    CHECK(fade_out(&b) == (WX_DO_BAKE | WX_DO_DETACH), "a weather scene change goes black");
    CHECK(wx_job_take(&J) != NULL, "its bake runs");
    wx_job_cut(&J);
    wx_job_done(&J, 2);
    CHECK(wx_job_step(&J, &r, 1.0 / 30) == WX_DO_APPLY, "a turn while a scene bake runs: it lands without a paint");
    CHECK(IO.key.cloud_band == 3 && J.cut, "its key is applied, the cut still pending");
    CHECK(wx_job_step(&J, &r, 1.0 / 30) == (WX_DO_BAKE | WX_DO_DETACH | WX_DO_ARRIVE), "then the cut, never a stale frame");

    baked(a);
    wx_job_cut(&J);
    CHECK(wx_job_step(&J, &r, 1.0 / 30) == (WX_DO_BAKE | WX_DO_DETACH | WX_DO_ARRIVE), "a turn: cut to Rain now");
    CHECK(wx_job_take(&J) != NULL, "its bake runs");
    wx_job_cut(&J);
    wx_job_done(&J, 2);
    CHECK(wx_job_step(&J, &r, 1.0 / 30) == (WX_DO_APPLY | WX_DO_PAINT),
          "turns during the bake that end on the face that lands: it lands painted, no extra black frame");

    baked(a);
    CHECK(fade_out(&b) == (WX_DO_BAKE | WX_DO_DETACH), "a scene re-bake");
    bake();
    CHECK(wx_job_step(&J, &b, 1.0 / 30) == (WX_DO_APPLY | WX_DO_PAINT) && J.opa == 85, "lands fading in");
    wx_job_cut(&J);
    wx_temp_t bc = key(-2, 12, 3);
    CHECK((wx_job_step(&J, &bc, 1.0 / 30) & ~WX_DO_PAINT) == WX_DO_BAKE && IO.kind == WX_BAKE_RING && !J.cut,
          "a pending cut meeting a ring-only change is dropped");
    CHECK(J.opa < 255, "and the fade in keeps stepping (%d)", J.opa);

    shown();
    CHECK(wx_job_step(&J, &a, 1.0 / 30) == WX_DO_BAKE, "page open: the first bake");
    CHECK(wx_job_take(&J) != NULL, "runs");
    wx_job_cut(&J);
    CHECK(wx_job_step(&J, &r, 1.0 / 30) == 0, "a turn before it lands: still black");
    wx_job_done(&J, 3);
    CHECK(wx_job_step(&J, &r, 1.0 / 30) == WX_DO_APPLY, "the first bake lands unseen");
    CHECK(wx_job_step(&J, &r, 1.0 / 30) == (WX_DO_BAKE | WX_DO_DETACH | WX_DO_ARRIVE), "then the picked face cuts in");

    baked(a);
    wx_job_cut(&J);
    CHECK(wx_job_step(&J, &a, 1.0 / 30) == WX_DO_PAINT && !J.cut, "a turn back before the next step: nothing to bake, the cut is dropped");
    CHECK(fade_out(&b) == (WX_DO_BAKE | WX_DO_DETACH), "so a later data change still fades");

    baked(a);
    wx_job_cut(&J);
    CHECK(wx_job_show(&J, false) == WX_DO_RELEASE, "hide");
    wx_job_released(&J);
    CHECK(wx_job_show(&J, true) == WX_DO_ACQUIRE, "show again");
    wx_job_acquired(&J, true);
    CHECK(wx_job_step(&J, &a, 1.0 / 30) == WX_DO_BAKE, "a page open forgets a pending cut");
}

static void test_turn_face(void)
{
    wx_temp_t a = key(3, 12, 1), r = rain_face(a);
    shown();
    CHECK(wx_job_face(&J) == -1, "nothing landed yet: no face on screen");
    CHECK(wx_job_step(&J, &a, 1.0 / 30) == WX_DO_BAKE && wx_job_face(&J) == -1, "black until the first bake lands");
    bake();
    wx_job_step(&J, &a, 1.0 / 30);
    CHECK(wx_job_face(&J) == WXF_TEMP, "the landed face");
    CHECK(fade_out(&r) == (WX_DO_BAKE | WX_DO_DETACH), "rain starts: fade out, black for the Rain now bake");
    CHECK(wx_job_face(&J) == WXF_TEMP, "while it bakes black, a turn steps from the face last shown, not the one coming");
    bake();
    CHECK(wx_job_step(&J, &r, 1.0 / 30) == (WX_DO_APPLY | WX_DO_PAINT) && wx_job_face(&J) == WXF_RAIN, "then Rain now");
    CHECK(wx_job_show(&J, false) == WX_DO_RELEASE, "hide");
    wx_job_released(&J);
    CHECK(wx_job_face(&J) == -1, "released: nothing on screen");
}

static void test_partial_during_fade_in(void)
{
    wx_temp_t a = key(3, 12, 1), b = key(3, 12, 3), c = key(-2, 12, 3);
    baked(a);
    CHECK(fade_out(&b) == (WX_DO_BAKE | WX_DO_DETACH), "a scene re-bake fades out and detaches");
    bake();
    CHECK(wx_job_step(&J, &b, 1.0 / 30) == (WX_DO_APPLY | WX_DO_PAINT) && J.opa == 85, "the scene lands at 85");
    int f = 0;
    while (J.opa == 85 && f++ < 20) wx_job_step(&J, &b, 1.0 / 30);
    CHECK(J.opa == 170, "one fade-in step (%d)", J.opa);
    CHECK((wx_job_step(&J, &c, 1.0 / 30) & ~WX_DO_PAINT) == WX_DO_BAKE && IO.kind == WX_BAKE_RING, "a range change mid fade-in bakes the ring");
    int before = J.opa;
    bake();
    CHECK(wx_job_step(&J, &c, 1.0 / 30) == (WX_DO_APPLY | WX_DO_PAINT) && J.opa >= before,
          "the ring lands without dimming the fade-in (%d -> %d)", before, J.opa);
    f = 0;
    while (J.opa < 255 && f++ < 20) wx_job_step(&J, &c, 1.0 / 30);
    CHECK(J.opa == 255 && f <= 3, "and the fade-in finishes on time (%d more frames)", f);
}

static void test_partial_rebake(void)
{
    wx_temp_t a = key(3, 12, 1), c = key(-2, 12, 1);
    baked(a);
    CHECK(wx_job_step(&J, &c, 0.03) == (WX_DO_BAKE | WX_DO_PAINT) && J.opa == 255, "a range change bakes the ring, no detach, keeps painting");
    CHECK(IO.kind == WX_BAKE_RING, "ring only");
    CHECK(wx_job_step(&J, &c, 0.03) == WX_DO_PAINT, "paint while the ring bake is queued");
    const wx_temp_t *in = wx_job_take(&J);
    CHECK(in && in->lo == -2, "the ring bake carries the new range");
    CHECK(wx_job_step(&J, &a, 0.03) == WX_DO_PAINT, "paint while it runs, no second job");
    wx_job_done(&J, 1);
    CHECK(wx_job_step(&J, &c, 0.03) == (WX_DO_APPLY | WX_DO_PAINT) && J.opa == 255, "applied without a fade");
    CHECK(wx_job_step(&J, &c, 0.03) == WX_DO_PAINT, "then paint");

    wx_temp_t m0 = c, m;
    m0.night = true;
    m0.hero = WX_HERO_MOON;
    m0.moon_illum = 40;
    baked(m0);
    m = m0;
    m.moon_illum = 45;
    CHECK(wx_job_step(&J, &m, 0.03) == (WX_DO_BAKE | WX_DO_PAINT) && IO.kind == WX_BAKE_HERO, "a hero change bakes the hero only");
    CHECK(wx_job_hold(&J, true) == 0 && wx_job_take(&J) == NULL, "held while shown: the hero bake is parked");
    wx_job_hold(&J, false);
    CHECK(wx_job_step(&J, &m, 0.03) == (WX_DO_BAKE | WX_DO_PAINT) && IO.kind == WX_BAKE_HERO,
          "a parked partial bake comes back as a partial bake: the shown buffers stay attached");
    bake();
    wx_temp_t sc = key(-2, 12, 3);
    CHECK(wx_job_step(&J, &sc, 0.03) == (WX_DO_APPLY | WX_DO_PAINT), "a scene change while the hero bake ran: the hero lands first");
    CHECK(fade_out(&sc) == (WX_DO_BAKE | WX_DO_DETACH), "then the scene fades out and detaches");

    baked(a);
    wx_job_step(&J, &sc, 1.0 / 30);
    wx_job_step(&J, &sc, 1.0 / 30);
    CHECK(J.opa < 255, "fading out");
    CHECK(wx_job_step(&J, &a, 1.0 / 30) == WX_DO_PAINT && J.opa == 255 && atomic_load(&J.state) == WX_JOB_IDLE,
          "the data goes back during the fade: full opacity, no bake");
    wx_job_step(&J, &sc, 1.0 / 30);
    CHECK(J.opa < 255 && wx_job_show(&J, false) == WX_DO_RELEASE, "hidden mid-fade: freed");
    wx_job_released(&J);
    CHECK(J.opa == 255 && !J.out, "opacity reset with the buffers");
}

static void test_hide_show_drops_a_done_job(void)
{
    wx_temp_t a = key(3, 12, 1);
    shown();
    wx_job_step(&J, &a, 0.03);
    bake();
    CHECK(wx_job_show(&J, false) == WX_DO_RELEASE, "hide with a done job frees at once");
    wx_job_released(&J);
    CHECK(wx_job_show(&J, true) == WX_DO_ACQUIRE, "show again allocates");
    wx_job_acquired(&J, true);
    CHECK(wx_job_step(&J, &a, 0.03) == WX_DO_BAKE, "the stale done job is dropped: new buffers bake again");
    CHECK(atomic_load(&J.state) == WX_JOB_QUEUED, "and a job is queued");
}

static void test_hide_during_bake(void)
{
    wx_temp_t a = key(3, 12, 1);
    shown();
    wx_job_step(&J, &a, 0.03);
    CHECK(wx_job_show(&J, false) == WX_DO_RELEASE, "hide cancels a queued job and frees");
    CHECK(wx_job_take(&J) == NULL, "a cancelled job is not taken");
    wx_job_released(&J);

    shown();
    wx_job_step(&J, &a, 0.03);
    CHECK(wx_job_take(&J) != NULL, "taken");
    CHECK(wx_job_show(&J, false) == 0, "hide during a running bake keeps the buffers");
    CHECK(wx_job_step(&J, &a, 0.03) == 0, "hidden, still running: nothing");
    wx_job_done(&J, 4);
    CHECK(wx_job_step(&J, &a, 0.03) == WX_DO_RELEASE, "hidden, the bake ended: free");
    wx_job_released(&J);
    CHECK(wx_job_step(&J, &a, 0.03) == 0, "hidden, freed: nothing");

    shown();
    wx_job_step(&J, &a, 0.03);
    wx_job_take(&J);
    wx_job_show(&J, false);
    CHECK(wx_job_show(&J, true) == 0, "shown again before the bake ended: the buffers are still held");
    wx_job_done(&J, 4);
    CHECK(wx_job_step(&J, &a, 0.03) == (WX_DO_APPLY | WX_DO_PAINT), "and the bake lands");
}

static void test_ota_hold(void)
{
    wx_temp_t a = key(3, 12, 1);
    shown();
    wx_job_step(&J, &a, 0.03);
    CHECK(wx_job_show(&J, false) == WX_DO_RELEASE, "pause with a queued job frees");
    wx_job_released(&J);
    CHECK(wx_job_hold(&J, true) == 0 && wx_job_take(&J) == NULL, "held after the free: nothing queued");

    shown();
    wx_job_step(&J, &a, 0.03);
    wx_job_take(&J);
    CHECK(wx_job_show(&J, false) == 0, "pause during a running bake keeps the buffers");
    CHECK(wx_job_hold(&J, true) == 0, "held, running: nothing to free yet");
    wx_job_done(&J, 4);
    CHECK(wx_job_hold(&J, true) == WX_DO_RELEASE, "held, the bake ended: the next OTA frame frees");
    wx_job_released(&J);
    CHECK(wx_job_hold(&J, true) == 0, "held, freed: nothing");
    CHECK(wx_job_step(&J, &a, 30) == 0, "no allocation or bake while held");
    CHECK(wx_job_hold(&J, false) == 0 && wx_job_show(&J, true) == WX_DO_ACQUIRE, "resume: the next show allocates");

    baked(a);
    wx_job_hold(&J, true);
    wx_temp_t b = key(-5, 0, 4);
    CHECK(wx_job_step(&J, &b, 0.03) == 0 && atomic_load(&J.state) == WX_JOB_IDLE, "held and shown: no bake starts");
    wx_job_hold(&J, false);
    CHECK(fade_out(&b) == (WX_DO_BAKE | WX_DO_DETACH), "released: the bake starts");
    CHECK(wx_job_hold(&J, true) == 0 && wx_job_take(&J) == NULL, "a hold parks a queued job");
    wx_job_hold(&J, false);
    CHECK(wx_job_step(&J, &a, 0.03) == WX_DO_BAKE, "back to the old key after a parked re-bake: bake again, no paint on detached canvases");
    bake();
    CHECK(wx_job_step(&J, &a, 0.03) == (WX_DO_APPLY | WX_DO_PAINT) && J.opa == 85, "the re-bake re-attaches and fades in");
}

static void test_alloc_retry(void)
{
    wx_temp_t a = key(3, 12, 1);
    wx_job_init(&J, &IO);
    CHECK(wx_job_show(&J, true) == WX_DO_ACQUIRE, "show allocates");
    wx_job_acquired(&J, false);
    int acq = 0, frames = 0;
    for (; frames < (int)(WX_JOB_RETRY_S * 30) + 5 && !acq; frames++) {
        int f = wx_job_step(&J, &a, frames == 3 ? NAN : 1.0 / 30);
        CHECK(!(f & ~WX_DO_ACQUIRE), "no bake without buffers");
        acq = f & WX_DO_ACQUIRE;
    }
    CHECK(acq && frames >= (int)(WX_JOB_RETRY_S * 30) - 2, "a failed allocation retries after %g s while shown (%d frames)",
          WX_JOB_RETRY_S, frames);
    wx_job_acquired(&J, true);
    CHECK(wx_job_step(&J, &a, 0.03) == WX_DO_BAKE, "the retry's buffers bake");

    wx_job_init(&J, &IO);
    wx_job_show(&J, true);
    wx_job_acquired(&J, false);
    wx_job_show(&J, false);
    int any = 0;
    for (int f = 0; f < 600; f++) any |= wx_job_step(&J, &a, 1.0 / 30);
    CHECK(any == 0, "hidden: no retry");
}

static void test_race_start(void)
{
    wx_temp_t a = key(3, 12, 1), b = key(-2, 12, 3), c = key(-2, 12, 1);
    for (int i = 0; i < 4; i++) {
        int at = 1 + i % 2;
        const wx_temp_t *n = i < 2 ? &b : &c;
        baked(a);
        s_race_at = at;
        s_race_mode = RACE_TAKE;
        s_race_fired = 0;
        s_race_took = false;
        for (int f = 0; f < 20 && !s_race_fired; f++) wx_job_step(&J, n, 1.0 / 30);
        s_race_mode = RACE_NONE;
        CHECK(s_race_fired == at, "race point %d reached", at);
        CHECK(!s_race_took || (s_race_in.lo == -2 && s_race_in.cloud_band == n->cloud_band && IO.kind == (i < 2 ? WX_BAKE_FULL : WX_BAKE_RING)),
              "a job taken at race point %d carries the new key and kind (lo %d band %d)", at, s_race_in.lo, s_race_in.cloud_band);
        CHECK(s_race_took == (at == 2), "race point %d: the job is %s", at, at == 2 ? "queued" : "not yet queued");
    }
}

static void test_race_done(void)
{
    wx_temp_t a = key(3, 12, 1);
    for (int at = 3; at <= 4; at++) {
        shown();
        wx_job_step(&J, &a, 0.03);
        wx_job_take(&J);
        IO.ms = 0;
        s_race_at = at;
        s_race_mode = RACE_STEP;
        s_race_fired = 0;
        s_race_acts = -1;
        s_race_step_t = a;
        wx_job_done(&J, 9);
        s_race_mode = RACE_NONE;
        CHECK(s_race_fired == at, "race point %d reached", at);
        CHECK(s_race_acts == (at == 3 ? 0 : (WX_DO_APPLY | WX_DO_PAINT)), "race point %d: step acts 0x%x", at, s_race_acts);
        CHECK(at == 3 || IO.ms == 9, "a job applied at race point %d carries its time (%d)", at, IO.ms);
    }
}

int main(void)
{
    test_first_bake();
    test_rebake_detaches();
    test_partial_rebake();
    test_partial_during_fade_in();
    test_rain_face_switch();
    test_user_switch_cuts();
    test_turn_face();
    test_hide_show_drops_a_done_job();
    test_hide_during_bake();
    test_ota_hold();
    test_alloc_retry();
    test_race_start();
    test_race_done();
    if (failures) {
        printf("wx_job: %d failure(s)\n", failures);
        return 1;
    }
    printf("wx_job: all tests passed\n");
    return 0;
}
