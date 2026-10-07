#pragma once
#include <stdbool.h>

// Screen dimming by sound (ported from weather_amoled's presence.c): the board's microphones, its motion sensor and
// the touch screen tell whether someone is around; the screen dims, then turns off, when the room stays quiet.
// docs/ARCHITECTURE.md, "Screen dimming".

typedef enum { PRESENCE_ACTIVE = 0, PRESENCE_DIM = 1, PRESENCE_OFF = 2 } presence_state_t;

typedef struct {
    bool  enabled;
    float margin_db;      // noise must exceed baseline + margin to count as "loud"
    float wake_s;         // seconds of (mostly) continuous noise needed to wake from DIM/OFF
    float dim_s;          // quiet seconds before dimming
    float off_s;          // further quiet seconds before the screen turns off
    int   bright_pct;     // normal brightness
    int   dim_pct;        // dimmed brightness
    float baseline_db;    // background noise (dBFS), set by calibration
} presence_cfg_t;

typedef struct {
    float level_db, threshold_db, wake_progress, quiet_s, calib_left_s;
    presence_state_t state;
    bool  calibrating, mic_ok;
    int   brightness;
    bool  imu_ok;         // motion sensor found
    float motion_g;       // recent movement (change from the resting position, g; peak, decays in ~1 s)
    float motion_thr;     // movement that wakes it (g)
} presence_status_t;

void presence_start(void);                          // after board_init (shares its I2C bus)
void presence_get_config(presence_cfg_t *out);
bool presence_set_config(const presence_cfg_t *in); // saves to NVS (baseline is kept); false = not saved
void presence_get_status(presence_status_t *st);
bool presence_calibrate(int seconds);               // measure background noise; keep quiet meanwhile
void presence_wake(void);
bool presence_touch(void);                          // a finger came down: wakes; true if the screen was off
bool presence_screen_off(void);
bool presence_motion_wake(void);                    // wake on pick-up / movement (saved)
bool presence_set_motion(bool on, float threshold_g);   // threshold 0.02..0.5 g (saved); false = not saved

// The state machine, pure (host test tests/host/test_presence.c): one step of dt seconds.
//   ACTIVE --(quiet for dim_s)--> DIM --(quiet for off_s more)--> OFF
//   DIM/OFF --(noise sustained for wake_s)--> ACTIVE      (a single bang doesn't wake it)
//   A touch or a movement wakes it, and counts as activity while it is on.
// running: enabled and the microphones work (otherwise it stays ACTIVE).
typedef struct {
    presence_state_t state;
    float score;          // sustained-noise score (s): rises while loud, falls at half speed while quiet
    float quiet_s;        // quiet time so far (dim_s and more once dimmed)
} presence_sm_t;
presence_state_t presence_sm_step(presence_sm_t *sm, const presence_cfg_t *c, bool running, bool loud, bool moved,
                                  bool touched, float dt);
void presence_clamp_cfg(presence_cfg_t *c);         // the limits every setting is held to (page, NVS)
