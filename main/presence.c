// Makeshift presence sensor (weather_amoled's presence.c, ported): the board's microphones (ES7210 via I2S) drive the
// screen brightness.
//   ACTIVE --(quiet for dim_s)--> DIM --(quiet for off_s more)--> OFF
//   DIM/OFF --(noise sustained for wake_s)--> ACTIVE      (a single bang doesn't wake it)
//   A touch always wakes it (and the touch that wakes an OFF screen is swallowed: the board's press filter, see
//   presence_touch() and main.c). Picking the display up or moving it (QMI8658 accelerometer) wakes it too, and counts
//   as activity while on. The state machine itself is presence_sm.c (pure, host-tested).
#include "presence.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2s_std.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "nvs.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "board.h"
#include "imu.h"

static const char *TAG = "presence";

#define SAMPLE_RATE   16000
#define TICK_MS       100                        // analysis window
#define FRAMES        (SAMPLE_RATE * TICK_MS / 1000)
#define PIN_MCLK      42
#define PIN_BCLK      9
#define PIN_WS        45
#define PIN_DIN       10                         // ES7210 -> ESP32 (the BSP calls it DSIN)
#define PIN_DOUT      8                          // ESP32 -> ES8311 (speaker: unused here, configured for later)
#define ES7210_ADDR   0x80                       // 8-bit address as esp_codec_dev expects
#define CALIB_MAX     600                        // up to 60 s of 100 ms samples
#define MOTION_G      0.10f                      // default pick-up threshold: change from the resting position (g)
#define TOUCH_RECENT_MS (TICK_MS + 50)           // a finger down within this counts as this tick's activity

static presence_cfg_t cfg = {
    .enabled = true, .margin_db = 10, .wake_s = 3, .dim_s = 600, .off_s = 3000,   // "Normal": dim 10 min, off at 60 min
    .bright_pct = 100, .dim_pct = 15, .baseline_db = -60,
};
static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
static esp_codec_dev_handle_t mic;
static bool mic_ok;

static volatile presence_state_t state = PRESENCE_ACTIVE;
static volatile bool touch_woke;                          // presence_touch() since the task's last tick
static uint32_t mic_errors;                               // failed microphone reads since boot
static volatile float level_db = -90, score, quiet_s;
static volatile int cur_pct = -1;                 // brightness actually applied
static volatile bool calibrating;
static volatile int calib_left_ticks;
static float *calib_buf;
static int calib_n;
static bool imu_ok;
static volatile bool motion_wake = true;
static volatile float motion_thr = MOTION_G;
static volatile float motion_g, motion_show;      // now; recent peak for the settings page meter

/* ---------------- settings ----------------
 * NVS namespace "presence", one typed key per setting (this project's rule: weather_amoled's "cfg" blob is dropped
 * as unreadable when the struct changes size, i.e. on an update). A missing key keeps its default.
 *   enabled u8 (0/1) | margin u16 (0.1 dB) | wake u16 (0.1 s) | dim u32 (s) | off u32 (s) | bright u8 (%)
 *   dim_pct u8 (%) | baseline i16 (0.1 dBFS) | motion u8 (0/1) | motion_mg u16 (mg, 20..500) */

static void load_cfg(void)
{
    nvs_handle_t h;
    if (nvs_open("presence", NVS_READONLY, &h) != ESP_OK) return;
    presence_cfg_t c = cfg;
    uint8_t u8;
    uint16_t u16;
    uint32_t u32;
    int16_t i16;
    if (nvs_get_u8(h, "enabled", &u8) == ESP_OK) c.enabled = u8;
    if (nvs_get_u16(h, "margin", &u16) == ESP_OK) c.margin_db = u16 / 10.0f;
    if (nvs_get_u16(h, "wake", &u16) == ESP_OK) c.wake_s = u16 / 10.0f;
    if (nvs_get_u32(h, "dim", &u32) == ESP_OK) c.dim_s = u32;
    if (nvs_get_u32(h, "off", &u32) == ESP_OK) c.off_s = u32;
    if (nvs_get_u8(h, "bright", &u8) == ESP_OK) c.bright_pct = u8;
    if (nvs_get_u8(h, "dim_pct", &u8) == ESP_OK) c.dim_pct = u8;
    if (nvs_get_i16(h, "baseline", &i16) == ESP_OK) c.baseline_db = i16 / 10.0f;
    presence_clamp_cfg(&c);
    cfg = c;
    if (nvs_get_u8(h, "motion", &u8) == ESP_OK) motion_wake = u8;
    if (nvs_get_u16(h, "motion_mg", &u16) == ESP_OK && u16 >= 20 && u16 <= 500) motion_thr = u16 / 1000.0f;
    nvs_close(h);
}

static bool nvs_ok(esp_err_t e, const char *what)
{
    if (e != ESP_OK) ESP_LOGE(TAG, "NVS %s: %s", what, esp_err_to_name(e));
    return e == ESP_OK;
}

static bool save_cfg(void)
{
    presence_cfg_t c;
    presence_get_config(&c);
    nvs_handle_t h;
    if (!nvs_ok(nvs_open("presence", NVS_READWRITE, &h), "open presence")) return false;
    bool ok = nvs_ok(nvs_set_u8(h, "enabled", c.enabled), "enabled") &&
              nvs_ok(nvs_set_u16(h, "margin", (uint16_t)lroundf(c.margin_db * 10)), "margin") &&
              nvs_ok(nvs_set_u16(h, "wake", (uint16_t)lroundf(c.wake_s * 10)), "wake") &&
              nvs_ok(nvs_set_u32(h, "dim", (uint32_t)lroundf(c.dim_s)), "dim") &&
              nvs_ok(nvs_set_u32(h, "off", (uint32_t)lroundf(c.off_s)), "off") &&
              nvs_ok(nvs_set_u8(h, "bright", (uint8_t)c.bright_pct), "bright") &&
              nvs_ok(nvs_set_u8(h, "dim_pct", (uint8_t)c.dim_pct), "dim_pct") &&
              nvs_ok(nvs_set_i16(h, "baseline", (int16_t)lroundf(c.baseline_db * 10)), "baseline") &&
              nvs_ok(nvs_set_u8(h, "motion", motion_wake), "motion") &&
              nvs_ok(nvs_set_u16(h, "motion_mg", (uint16_t)lroundf(motion_thr * 1000)), "motion_mg") &&
              nvs_ok(nvs_commit(h), "commit");
    nvs_close(h);
    return ok;
}

void presence_get_config(presence_cfg_t *out)
{
    taskENTER_CRITICAL(&mux);
    *out = cfg;
    taskEXIT_CRITICAL(&mux);
}

bool presence_set_config(const presence_cfg_t *in)
{
    presence_cfg_t c = *in;
    c.baseline_db = cfg.baseline_db;              // only calibration changes the baseline
    presence_clamp_cfg(&c);
    taskENTER_CRITICAL(&mux);
    cfg = c;
    taskEXIT_CRITICAL(&mux);
    bool ok = save_cfg();
    presence_wake();                              // show the result of the new settings right away
    ESP_LOGI(TAG, "config: %s, margin %.0f dB, wake %.1f s, dim %.0f s, off +%.0f s, %d%%/%d%%",
             c.enabled ? "on" : "off", c.margin_db, c.wake_s, c.dim_s, c.off_s, c.bright_pct, c.dim_pct);
    return ok;
}

/* ---------------- status / control ---------------- */

void presence_get_status(presence_status_t *st)
{
    st->level_db = level_db;
    st->threshold_db = cfg.baseline_db + cfg.margin_db;
    st->state = state;
    st->wake_progress = cfg.wake_s > 0 ? score / cfg.wake_s : 0;
    st->quiet_s = quiet_s;
    st->calibrating = calibrating;
    st->calib_left_s = calib_left_ticks * TICK_MS / 1000.0f;
    st->mic_ok = mic_ok;
    st->brightness = cur_pct < 0 ? 0 : cur_pct;
    st->imu_ok = imu_ok;
    st->motion_g = motion_show;
    st->motion_thr = motion_thr;
}

bool presence_motion_wake(void) { return motion_wake; }

bool presence_set_motion(bool on, float threshold_g)
{
    if (!(threshold_g >= 0.02f)) threshold_g = 0.02f;
    if (threshold_g > 0.5f) threshold_g = 0.5f;
    motion_wake = on;
    motion_thr = threshold_g;
    ESP_LOGI(TAG, "wake on pick-up %s, threshold %.2f g", on ? "on" : "off", threshold_g);
    return save_cfg();
}

bool presence_calibrate(int seconds)
{
    if (!mic_ok || calibrating) return false;
    if (seconds < 2) seconds = 2;
    if (seconds > CALIB_MAX * TICK_MS / 1000) seconds = CALIB_MAX * TICK_MS / 1000;
    calib_n = 0;
    calib_left_ticks = seconds * 1000 / TICK_MS;
    calibrating = true;
    ESP_LOGI(TAG, "calibrating for %d s - keep quiet", seconds);
    return true;
}

void presence_wake(void)
{
    touch_woke = true;                            // the task's state machine wakes too (it owns the state)
    state = PRESENCE_ACTIVE;
    quiet_s = 0;
}

// The board's press filter (main.c: touch_set_press_filter), in the LVGL task, when a finger comes down: wakes, and
// says whether the screen was off, so that press is swallowed (no tap, no swipe, no long-press)
bool presence_touch(void)
{
    bool was_off = state == PRESENCE_OFF;
    presence_wake();
    if (was_off) ESP_LOGI(TAG, "touch on a dark screen: wake (this touch does nothing else)");
    return was_off;
}

bool presence_screen_off(void) { return state == PRESENCE_OFF; }

/* ---------------- audio ---------------- */

static bool mic_init(void)
{
    // Both directions on one I2S port (same clocks): microphones in (ES7210), speaker out (ES8311, for later)
    i2s_chan_handle_t rx = NULL, tx = NULL;
    i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    cc.dma_desc_num = 4;
    cc.dma_frame_num = 320;
    cc.auto_clear = true;                                     // silence on the speaker output
    if (i2s_new_channel(&cc, &tx, &rx) != ESP_OK) return false;
    i2s_std_config_t sc = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = { .mclk = PIN_MCLK, .bclk = PIN_BCLK, .ws = PIN_WS, .dout = PIN_DOUT, .din = PIN_DIN },
    };
    if (i2s_channel_init_std_mode(tx, &sc) != ESP_OK || i2s_channel_init_std_mode(rx, &sc) != ESP_OK) return false;
    if (i2s_channel_enable(tx) != ESP_OK || i2s_channel_enable(rx) != ESP_OK) return false;

    audio_codec_i2s_cfg_t icfg = { .port = I2S_NUM_0, .rx_handle = rx, .tx_handle = tx };
    const audio_codec_data_if_t *data_if = audio_codec_new_i2s_data(&icfg);
    audio_codec_i2c_cfg_t ccfg = { .port = 0, .addr = ES7210_ADDR, .bus_handle = board_i2c_bus() };
    const audio_codec_ctrl_if_t *ctrl_if = audio_codec_new_i2c_ctrl(&ccfg);
    if (!data_if || !ctrl_if) return false;
    es7210_codec_cfg_t ecfg = { .ctrl_if = ctrl_if, .mic_selected = ES7210_SEL_MIC1 | ES7210_SEL_MIC2 };
    const audio_codec_if_t *codec = es7210_codec_new(&ecfg);
    if (!codec) return false;
    esp_codec_dev_cfg_t dcfg = { .dev_type = ESP_CODEC_DEV_TYPE_IN, .codec_if = codec, .data_if = data_if };
    mic = esp_codec_dev_new(&dcfg);
    if (!mic) return false;
    esp_codec_dev_set_in_gain(mic, 30.0);
    esp_codec_dev_sample_info_t fs = { .sample_rate = SAMPLE_RATE, .channel = 2, .bits_per_sample = 16 };
    if (esp_codec_dev_open(mic, &fs) != ESP_CODEC_DEV_OK) return false;
    return true;
}

static void apply_brightness(int target)
{
    if (target == cur_pct) return;
    // Fade: ~1 s from full to off
    int step = 10;
    int next = cur_pct < 0 ? target : (target > cur_pct ? (cur_pct + step > target ? target : cur_pct + step)
                                                          : (cur_pct - step < target ? target : cur_pct - step));
    display_lock(-1);
    display_brightness((uint8_t)(next * 255 / 100));
    display_unlock();
    cur_pct = next;
}

static int cmp_float(const void *a, const void *b)
{
    float x = *(const float *)a, y = *(const float *)b;
    return (x > y) - (x < y);
}

static void presence_task(void *arg)
{
    int16_t *buf = heap_caps_malloc(FRAMES * 2 * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    calib_buf = heap_caps_malloc(CALIB_MAX * sizeof(float), MALLOC_CAP_SPIRAM);
    mic_ok = buf && calib_buf && mic_init();
    ESP_LOGI(TAG, "microphones %s, baseline %.1f dBFS", mic_ok ? "ready" : "NOT available", cfg.baseline_db);
    imu_ok = imu_init(board_i2c_bus());
    const float dt = TICK_MS / 1000.0f;
    uint32_t log_tick = 0;
    presence_sm_t sm = { .state = PRESENCE_ACTIVE };
    presence_state_t last_state = sm.state;
    float rest[3] = {0}, motion_peak = 0;                     // resting acceleration, follows in ~2 s
    int imu_skip = 10;                                        // the first second of samples is junk (3.7 g seen)

    while (1) {
        bool heard = false;                                    // this tick's sound level is real
        if (mic_ok) {
            if (esp_codec_dev_read(mic, buf, FRAMES * 2 * sizeof(int16_t)) != ESP_CODEC_DEV_OK) {
                // A failing read used to skip the rest of the loop (`continue`): brightness froze, with no log.
                // Count it, carry on as if quiet (motion and timers still work), log the first one.
                if (mic_errors++ == 0) ESP_LOGW(TAG, "microphone read failed (counted in the 5 s lines)");
                vTaskDelay(pdMS_TO_TICKS(TICK_MS));
            } else heard = true;
        }
        if (heard) {
            double acc = 0;
            for (int i = 0; i < FRAMES * 2; i++) acc += (double)buf[i] * buf[i];
            double rms = sqrt(acc / (FRAMES * 2));
            float db = rms > 0.5 ? 20.0f * log10f((float)(rms / 32768.0)) : -90.0f;
            level_db = db;
        } else if (!mic_ok) {
            vTaskDelay(pdMS_TO_TICKS(TICK_MS));
        }

        // Calibration: baseline = 90th percentile of the quiet room's levels
        if (calibrating) {
            if (calib_n < CALIB_MAX) calib_buf[calib_n++] = level_db;
            if (--calib_left_ticks <= 0) {
                qsort(calib_buf, calib_n, sizeof(float), cmp_float);
                float p90 = calib_buf[(int)(calib_n * 0.9f)];
                taskENTER_CRITICAL(&mux);
                cfg.baseline_db = p90;
                presence_clamp_cfg(&cfg);
                taskEXIT_CRITICAL(&mux);
                save_cfg();
                calibrating = false;
                ESP_LOGI(TAG, "calibrated: baseline %.1f dBFS (min %.1f, max %.1f, %d samples)",
                         p90, calib_buf[0], calib_buf[calib_n - 1], calib_n);
            }
        }

        // Motion: distance from the resting position (a slow average), so tilting or lifting it counts and
        // lying still in any position doesn't
        bool moved = false;
        float a[3];
        if (imu_ok && imu_read(a)) {
            if (imu_skip > 0) {
                if (--imu_skip == 0) for (int i = 0; i < 3; i++) rest[i] = a[i];
            } else {
                float d2 = 0;
                for (int i = 0; i < 3; i++) { float e = a[i] - rest[i]; d2 += e * e; rest[i] += e * 0.05f; }
                motion_g = sqrtf(d2);
                motion_show = motion_g > motion_show * 0.85f ? motion_g : motion_show * 0.85f;   // meter: peak, decays
                if (motion_g > motion_peak) motion_peak = motion_g;
                moved = motion_wake && motion_g > motion_thr;
            }
        }

        // A finger: presence_touch() (the press filter, at once) or one still down / just lifted (touch_idle_ms)
        bool touched = touch_woke || touch_idle_ms() < TOUCH_RECENT_MS;
        touch_woke = false;

        presence_cfg_t c;
        presence_get_config(&c);
        bool loud = heard && !calibrating && level_db > c.baseline_db + c.margin_db;
        if (moved && sm.state != PRESENCE_ACTIVE)
            ESP_LOGI(TAG, "picked up / moved (%.2f g): wake", motion_g);
        presence_sm_step(&sm, &c, c.enabled && mic_ok, loud, moved, touched, dt);
        score = sm.score;
        quiet_s = sm.quiet_s;
        state = sm.state;
        if (sm.state != last_state) {
            static const char *names[] = {"ACTIVE", "DIM", "OFF"};
            ESP_LOGI(TAG, "%s -> %s (level %.1f dB, threshold %.1f dB%s)", names[last_state], names[sm.state],
                     level_db, c.baseline_db + c.margin_db, touched ? ", touch" : "");
            last_state = sm.state;
        }
        int dim = c.dim_pct < c.bright_pct ? c.dim_pct : c.bright_pct;   // never brighter than "full"
        apply_brightness(sm.state == PRESENCE_ACTIVE ? c.bright_pct : sm.state == PRESENCE_DIM ? dim : 0);

        if (++log_tick % 50 == 0) {                            // every 5 s
            ESP_LOGI(TAG, "level %.1f dB (threshold %.1f), score %.1f/%.1f, quiet %.0f s, motion peak %.3f g%s",
                     level_db, c.baseline_db + c.margin_db, sm.score, c.wake_s, sm.quiet_s, motion_peak,
                     mic_errors ? ", MIC READ ERRORS" : "");
            if (mic_errors) ESP_LOGW(TAG, "%lu microphone read errors so far", (unsigned long)mic_errors);
            motion_peak = 0;
        }
    }
}

void presence_start(void)
{
    load_cfg();
    xTaskCreatePinnedToCore(presence_task, "presence", 4096, NULL, 2, NULL, 0);
}
