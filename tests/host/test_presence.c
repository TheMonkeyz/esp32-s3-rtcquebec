// presence_sm.c: the screen dimming state machine (ACTIVE -> DIM -> OFF, what wakes it) and the settings' limits
#include <math.h>
#include <stdlib.h>
#include "check.h"
#include "presence.h"

#define DT 0.1f                                   // presence.c's tick (TICK_MS)

static const presence_cfg_t normal = {           // presence.c's defaults ("Normal")
    .enabled = true, .margin_db = 10, .wake_s = 3, .dim_s = 600, .off_s = 3000,
    .bright_pct = 100, .dim_pct = 15, .baseline_db = -60,
};

// n ticks of the same input; the state after them
static presence_state_t run(presence_sm_t *sm, int n, bool loud, bool moved, bool touched)
{
    presence_state_t s = sm->state;
    for (int i = 0; i < n; i++) s = presence_sm_step(sm, &normal, true, loud, moved, touched, DT);
    return s;
}
#define SECONDS(s) ((int)lroundf((s) / DT))

// Ticks until the state becomes `want` (loud or quiet all along), at most max
static int until(presence_sm_t *sm, presence_state_t want, bool loud, int max)
{
    int n = 0;
    while (sm->state != want && n < max) { presence_sm_step(sm, &normal, true, loud, false, false, DT); n++; }
    return n;
}

int main(void)
{
    // Quiet: dims after 10 min, off 50 min later (60 min in all). Ticks of 0.1 s summed in floats drift (~1 s an
    // hour, harmless): within 5 s is on time.
    presence_sm_t sm = { .state = PRESENCE_ACTIVE };
    int t = until(&sm, PRESENCE_DIM, false, SECONDS(4000));
    CHECK(abs(t - SECONDS(600)) <= SECONDS(5), "dim after %d ticks", t);
    t = until(&sm, PRESENCE_OFF, false, SECONDS(4000));
    CHECK(abs(t - SECONDS(3000)) <= SECONDS(5), "off %d ticks later", t);
    CHECK(run(&sm, SECONDS(3600), false, false, false) == PRESENCE_OFF, "stays off in silence");

    // A single bang (0.5 s loud) doesn't wake an off screen; sustained noise (3 s) does
    CHECK(run(&sm, 5, true, false, false) == PRESENCE_OFF, "a bang doesn't wake");
    run(&sm, SECONDS(10), false, false, false);
    CHECK(sm.score == 0, "the score falls back to 0: %f", sm.score);
    t = until(&sm, PRESENCE_ACTIVE, true, SECONDS(60));
    CHECK(abs(t - SECONDS(3)) <= 1, "3 s of noise wakes: %d ticks", t);
    CHECK(sm.quiet_s == 0, "quiet count restarts: %f", sm.quiet_s);

    // Mostly continuous noise wakes too: 1 s loud, 0.4 s quiet (falls at half speed) -> wakes within ~5 s
    sm = (presence_sm_t){ .state = PRESENCE_OFF };
    int ticks = 0;
    while (sm.state == PRESENCE_OFF && ticks < SECONDS(20)) {
        presence_sm_step(&sm, &normal, true, ticks % 14 < 10, false, false, DT);
        ticks++;
    }
    CHECK(sm.state == PRESENCE_ACTIVE && ticks < SECONDS(6), "intermittent noise woke after %d ticks", ticks);

    // While dim, a short noise restarts the off countdown (and doesn't wake)
    sm = (presence_sm_t){ .state = PRESENCE_DIM, .quiet_s = 600 + 2999 };
    CHECK(run(&sm, 2, true, false, false) == PRESENCE_DIM, "dim stays dim on a short noise");
    CHECK(fabsf(sm.quiet_s - 600) < 1e-3, "off countdown restarted: %f", sm.quiet_s);

    // Noise while active keeps it from dimming
    sm = (presence_sm_t){ .state = PRESENCE_ACTIVE, .quiet_s = 599 };
    run(&sm, 1, true, false, false);
    CHECK(sm.quiet_s == 0 && sm.state == PRESENCE_ACTIVE, "noise resets the quiet count");

    // Motion (pick-up) wakes at once, from dim or off, and counts as activity while on
    sm = (presence_sm_t){ .state = PRESENCE_OFF };
    CHECK(run(&sm, 1, false, true, false) == PRESENCE_ACTIVE, "motion wakes an off screen");
    sm = (presence_sm_t){ .state = PRESENCE_DIM, .quiet_s = 700 };
    CHECK(run(&sm, 1, false, true, false) == PRESENCE_ACTIVE, "motion wakes a dim screen");
    sm = (presence_sm_t){ .state = PRESENCE_ACTIVE, .quiet_s = 599 };
    CHECK(run(&sm, 1, false, true, false) == PRESENCE_ACTIVE && sm.quiet_s == 0, "motion is activity");

    // A touch: the same
    sm = (presence_sm_t){ .state = PRESENCE_OFF };
    CHECK(run(&sm, 1, false, false, true) == PRESENCE_ACTIVE, "touch wakes an off screen");
    sm = (presence_sm_t){ .state = PRESENCE_DIM, .quiet_s = 700 };
    CHECK(run(&sm, 1, false, false, true) == PRESENCE_ACTIVE, "touch wakes a dim screen");
    sm = (presence_sm_t){ .state = PRESENCE_ACTIVE, .quiet_s = 599 };
    CHECK(run(&sm, 1, false, false, true) == PRESENCE_ACTIVE && sm.quiet_s == 0, "touch is activity");

    // Turned off (or no microphone): always active, wherever it was
    sm = (presence_sm_t){ .state = PRESENCE_OFF, .quiet_s = 4000 };
    CHECK(presence_sm_step(&sm, &normal, false, false, false, false, DT) == PRESENCE_ACTIVE && sm.quiet_s == 0,
          "not running: active");

    // Limits: NaN and out-of-range values from the page or NVS
    presence_cfg_t c = { .margin_db = NAN, .wake_s = 0, .dim_s = -5, .off_s = 1e9f, .bright_pct = 0, .dim_pct = 200,
                         .baseline_db = NAN };
    presence_clamp_cfg(&c);
    CHECK(c.margin_db == 1 && fabsf(c.wake_s - 0.2f) < 1e-6 && c.dim_s == 1 && c.off_s == 86400, "%f %f %f %f",
          c.margin_db, c.wake_s, c.dim_s, c.off_s);
    CHECK(c.bright_pct == 5 && c.dim_pct == 100 && c.baseline_db == -100, "%d %d %f", c.bright_pct, c.dim_pct,
          c.baseline_db);
    presence_cfg_t d = normal;
    presence_clamp_cfg(&d);
    CHECK(d.dim_s == 600 && d.off_s == 3000 && d.baseline_db == -60 && d.margin_db == 10, "defaults kept");

    return check_done("test_presence");
}
