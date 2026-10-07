# The display in the browser

The firmware's own code, compiled to WebAssembly: LVGL 9.2.2 with the display's settings (`lv_kconfig.h`, generated
from its sdkconfig), `main.c` (its start-up runs as a task, and its settings-page routes), `ui.c`, `app_text.c`,
`rtc_api.c`, `favs.c`, `departures.c`, forge_lvgl's `forge_lvgl.c`, `pager.c`, `slide.c`, `screens.c`, forge_core's
`i18n.c`, `i18n_nvs.c`, `textfit.c`, `testcon_registry.c`, forge_net's `svc.c` and forge_ota's `ota_web.c`, all
unchanged. Only the hardware and the network stack are replaced:

| File | Stands in for |
|---|---|
| `emu_display.c` | the board (`board.h`): `board_init()` (LVGL, display, touch, forge_lvgl's panel hooks) and the AMOLED panel, a 466x466 RGB565 framebuffer that `index.html` copies to a round canvas |
| `emu_touch.c` | the touch chip: the mouse or a finger on the canvas |
| `emu_http.c` | `esp_http_client`: `fetch()`, awaited with ASYNCIFY (RTC's API answers `Access-Control-Allow-Origin: *`) |
| `emu_nvs.c` | NVS: the favourites and the language, kept in the page's `localStorage` (`rtcquebec_emu_nvs`) |
| `emu_stubs.c` | Wi-Fi (always connected to "browser", so `app_main` goes straight to the stops), updates (none; Restart reloads the page), diag, the test console |
| `emu_web.c` | the web server: the settings page below the emulator (`build/settings.html`, the display's `main/web/index.html` with `emu-settings.js` first in its head) queues its `/api/` requests, served here between LVGL frames by the display's own handlers (main.c's, ota_web.c's); `/api/info` as forge_net's web.c; Wi-Fi scan and save answer that they need the real display |
| `emu_tasks.c` | FreeRTOS tasks, queues and binary semaphores: each task an Emscripten fiber, run by the main loop between LVGL frames; a wait inside a task (`vTaskDelay`, `ulTaskNotifyTake`, a request) goes back to the main loop. `app_main`, departures.c's "deps" and ui.c's "setup_radio" run as is |
| `emu_time.c` | `localtime_r` for the firmware's files (`-Dlocaltime_r=emu_localtime_r`): the POSIX TZ main.c sets (Eastern time), which Emscripten ignores |
| `emu_main.c` | the scheduler and LVGL's task: demo favourites for a new visitor, then `app_main` as a task, then the loop |
| `shim/` | ESP-IDF and FreeRTOS headers; `vTaskDelay` hands control back to the browser (`emscripten_sleep`) |

The firmware itself has one `#ifdef EMU_BUILD`: `ui.c` finds its font as an array here (`build/fonts.c`).

Left out (the display has them, the browser doesn't need them): Wi-Fi setup and Easy Connect (a long-press still opens
the setup screen, with nothing behind it), updates, the test console, diag, snapshots, the motion sensor.

## Build (WSL)

```bash
git clone --depth 1 https://github.com/emscripten-core/emsdk.git ~/emsdk && ~/emsdk/emsdk install 6.0.11 && ~/emsdk/emsdk activate 6.0.11
source ~/emsdk/emsdk_env.sh
cd web/emu && make -j8      # build/emu.js, build/emu.wasm (~0.9 MB), build/index.html; ~1 min the first time
```

It needs LVGL where ESP-IDF's component manager puts it (`managed_components/lvgl__lvgl`, from a firmware build) and
ESP-IDF's cJSON (`IDF_PATH`, default `/mnt/c/Espressif/esp-idf`); `LVGL=` and `CJSON=` override them (a git worktree
has no `managed_components`: `LVGL=<main checkout>/managed_components/lvgl__lvgl`). Serve `build/` over HTTP
(`.claude/launch.json`: "emulator", port 8766, or `python -m http.server 8766 -d web/emu/build`); `file://` can't load
the WebAssembly. `build/fonts/` holds the page's font for a local run (on the site it comes from `../fonts/`).

`try.sh <file.c>` compiles one file with the emulator's flags and shows the first errors.

After changing LVGL options in `sdkconfig.defaults`: `python3 web/emu/gen_lv_kconfig.py build/v55/sdkconfig > web/emu/lv_kconfig.h`.

## On the flasher site

`python tools/make_flasher_site.py site --stable dist --emu web/emu/build` copies `index.html`, `emu.js`, `emu.wasm`,
`settings.html` and `emu-settings.js` to the site's `try/` and adds `"try": "try/"` to `channels.json`; the flasher
page then shows *Try it in your browser* (hidden on a site built without `--emu`). `index.html` uses the site's font
from `../fonts/`.

CI (`.github/workflows/firmware.yml`, job `emulator`) builds it on every push, from the latest stable release (the tag
itself when a stable tag is pushed; with no stable release yet, or one older than the emulator, the pushed commit is
built, said in the log), with LVGL from GitHub at the firmware's version (`main/idf_component.yml`), cJSON 1.7.19
(ESP-IDF 5.5.4's) and Emscripten 6.0.11. The `pages` job adds it to the site; if the emulator build fails, the site is
published without it.

## Notes

- **RTC's API, politely.** The requests are departures.c's own, with its polling rules: the stop on view every 30 s,
  the others every 5 min, 2 s apart, one at a time; a page load fetches each favourite once, as a display starting up
  does. Nothing else polls. A plain `fetch()`, no header of ours: a custom one would turn every request into a CORS
  preflight, which RTC may not answer; the browser's User-Agent goes instead of the firmware's. Test against it
  sparingly and never in a loop.
- A new visitor (no NVS `favs` yet) gets two favourites, route 800 both ways: stop 1025 (St-Dominique, toward Colline
  Parlementaire) and stop 1105 (toward Terminus Chute-Montmorency). Added once: favs.c writes `n` with every save, so
  a visitor who removes them keeps none.
- `?stop=1025&route=800&dir=0` in the address makes that favourite the first one (added if new, in place of the last
  when the list is full; kept), so the display opens on it: a link to a given stop.
- `main.c` is not rewritten for the browser: `app_main` runs as a task (fiber) with the stubs saying Wi-Fi is up, so
  start-up order, the routes' registration and `ui_home()` are the firmware's.
- A request from the settings page that needs RTC (Find directions, Add) waits in its handler for the deps task, as on
  the display: the main loop runs the tasks meanwhile (`emu_sem_take`), but LVGL doesn't draw until it is answered
  (~0.1-0.5 s).
- The times are Québec's whatever the visitor's time zone (`emu_time.c`): "Updated at", the clock and the service date
  RTC is asked for.
- A click can start and end between two of LVGL's touch reads: `emu_touch.c` holds a press until LVGL has read it.
  Only a new press is held, never a move, and `touch_forget()` drops it (a drag, slide.c, reads the finger itself).
  A drag must last a few frames to be one: an instant synthetic drag is read as a tap where it ended.
- The settings page: the display's own page in an iframe, unchanged, opened with a key in its address
  (`#k=0000000000000000`, the emulator checks none) so it doesn't say it has no key. Its requests are served by the
  main loop (`emu_web_poll()`), never inside a call from JavaScript: a handler may wait (forge_ota's `update_post`,
  departures.c's lookups), and ASYNCIFY can't unwind a call made while the main loop is itself suspended. Its list
  shows a stop's name once that stop has been fetched (as on the display: the page reads `/api/favs` when it opens).
- A hidden tab slows timers to about once a second: the polling goes on (as the display's), the canvas updates when
  the page is shown.
