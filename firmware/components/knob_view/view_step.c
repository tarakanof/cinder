#include "knob_view.h"

#include "view_policy.h"

static int kept_mood(const knob_view_state_t *s) { return s->view.has_mood ? (int)knob_view_mood(&s->view) : -1; }

bool knob_view_conditional(const knob_view_state_t *s) { return s->have_view || s->unsupported; }

void knob_view_state_reset(knob_view_state_t *s)
{
    s->have_view = false;
    s->unsupported = false;
    s->compat = KNOB_COMPAT_OK;
}

static knob_step_t kept(const knob_view_state_t *s)
{
    return s->have_view ? (knob_step_t){.act = KNOB_STEP_RESTORE, .mood = kept_mood(s)}
                        : (knob_step_t){.act = KNOB_STEP_NONE, .mood = -1};
}

knob_step_t knob_view_step(knob_view_state_t *s, int status, const char *body)
{
    knob_step_t r = {.act = KNOB_STEP_NONE, .mood = -1};
    switch (view_answer(status, knob_view_conditional(s))) {
    case VIEW_ANS_NEW: {
        int major = 0;
        knob_view_res_t res = knob_view_read(body, &s->view, &major);
        if (res == KNOB_VIEW_TOO_OLD || res == KNOB_VIEW_TOO_NEW) {
            s->unsupported = true;
            s->major = major;
            s->compat = res == KNOB_VIEW_TOO_NEW ? KNOB_COMPAT_UPDATE_KNOB : KNOB_COMPAT_UPDATE_EMBER;
            r = kept(s);
            r.etag_set = true;
            return r;
        }
        if (res != KNOB_VIEW_OK) {
            s->have_view = false;
            s->unsupported = false;
            return (knob_step_t){.act = KNOB_STEP_UNPARSED, .mood = -1, .etag_clear = true};
        }
        s->have_view = true;
        s->unsupported = false;
        s->major = major;
        s->compat = KNOB_COMPAT_OK;
        return (knob_step_t){.act = KNOB_STEP_APPLY, .mood = kept_mood(s), .etag_set = true};
    }
    case VIEW_ANS_SAME:
        if (s->unsupported) return kept(s);
        return (knob_step_t){.act = KNOB_STEP_APPLY, .mood = kept_mood(s)};
    case VIEW_ANS_REFETCH:
        return (knob_step_t){.act = KNOB_STEP_REFETCH, .mood = -1, .etag_clear = true};
    case VIEW_ANS_FAILED:
    default:
        return r;
    }
}
