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
    CHECK(wx_job_step(&J, &b, 0.03) == (WX_DO_BAKE | WX_DO_DETACH), "a re-bake on a shown face detaches first");
    CHECK(wx_job_step(&J, &b, 0.03) == 0, "no paint while a re-bake is queued");
    const wx_temp_t *in = wx_job_take(&J);
    CHECK(in && in->lo == -2 && in->cloud_band == 3, "the re-bake carries the new key");
    CHECK(wx_job_step(&J, &b, 0.03) == 0, "no paint while a re-bake runs");
    CHECK(wx_job_step(&J, &a, 0.03) == 0, "no new job or paint while one runs");
    wx_job_done(&J, 5);
    CHECK(wx_job_step(&J, &b, 0.03) == (WX_DO_APPLY | WX_DO_PAINT), "a re-bake applies, then paints");
    CHECK(IO.key.lo == -2, "the re-bake's key is applied");
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
    CHECK(wx_job_step(&J, &b, 0.03) == (WX_DO_BAKE | WX_DO_DETACH), "released: the bake starts");
    CHECK(wx_job_hold(&J, true) == 0 && wx_job_take(&J) == NULL, "a hold parks a queued job");
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
    wx_temp_t a = key(3, 12, 1), b = key(-2, 12, 3);
    for (int at = 1; at <= 2; at++) {
        baked(a);
        s_race_at = at;
        s_race_mode = RACE_TAKE;
        s_race_fired = 0;
        s_race_took = false;
        wx_job_step(&J, &b, 0.03);
        s_race_mode = RACE_NONE;
        CHECK(s_race_fired == at, "race point %d reached", at);
        CHECK(!s_race_took || (s_race_in.lo == -2 && s_race_in.cloud_band == 3),
              "a job taken at race point %d carries the new key (lo %d band %d)", at, s_race_in.lo, s_race_in.cloud_band);
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
