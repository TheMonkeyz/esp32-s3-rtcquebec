# The display in the browser

The firmware's own code, compiled to WebAssembly: LVGL 9.2.2 with the display's settings (`lv_kconfig.h` and
`sdkconfig.h`, generated from its sdkconfig), `main.c` (its start-up runs as a task, and its settings-page routes),
`ui.c`, `app_text.c`, `rtc_api.c`, `favs.c`, `departures.c`, `map.c`, `geo.c`, and espforge's components, all unchanged.
Only the hardware and the network stack are replaced, by **espforge's `web/emu/forge`** at the tag in
`main/idf_component.yml` (since v0.3.1; `tools/fetch_forge.py` puts it in `.espforge/`): the board, touch,
`esp_http_client` as `fetch()`, NVS in `localStorage`, FreeRTOS tasks as fibers, the web server for the settings page,
`localtime_r` following TZ, screen dimming without microphones (`emu_presence.c`), and the page's script
(`emu-page.js`). Its README says what each file stands in for. This folder holds only what is this app's:

| File | What |
|---|---|
| `Makefile` | the app's sources (`APP_SRC`), `emu_main.c`, the embedded font, `png_rows.c` for the map's tiles (with miniz's tinfl); the rules are espforge's `emu.mk` |
| `emu_main.c` | demo favourites for a new visitor, a stop from the page's address (`emu_param`), then `app_main` as a task and the loop |
| `index.html` | the page's words and look |
| `lv_kconfig.h`, `sdkconfig.h` | `make config` from the firmware's sdkconfig (commit both: CI builds the emulator without the firmware) |

The firmware itself has one `#ifdef EMU_BUILD`: `ui.c` finds its font as an array here.

Left out (the display has them, the browser doesn't need them): Wi-Fi setup and Easy Connect (a long-press still opens
the setup screen, with nothing behind it), updates, the test console, diag, snapshots, the microphones and the motion
sensor.

## Build (WSL)

```bash
git clone --depth 1 https://github.com/emscripten-core/emsdk.git ~/emsdk && ~/emsdk/emsdk install 6.0.11 && ~/emsdk/emsdk activate 6.0.11
source ~/emsdk/emsdk_env.sh
python3 tools/fetch_forge.py                # espforge at the pinned tag, in .espforge/
cd web/emu && make -j8                      # build/: emu.js, emu.wasm (~1 MB), index.html, settings.html...
```

It needs LVGL where ESP-IDF's component manager puts it (`managed_components/lvgl__lvgl`, from a firmware build) and
ESP-IDF's cJSON (`IDF_PATH`, default `/mnt/c/Espressif/esp-idf`); `LVGL=`, `CJSON=` and `ESPFORGE=` (an espforge
checkout, to try an unreleased change) override them. Serve `build/` over HTTP (`.claude/launch.json`: "emulator",
port 8766, or `python -m http.server 8766 -d web/emu/build`); `file://` can't load the WebAssembly. `make try
F=../../main/ui.c` compiles one file and shows the first errors; `make config` after changing LVGL or `FORGE_*`
options. Smoke test: `cd tools/webtest && node ../../.espforge/web/emu/forge/smoke.js ../../web/emu/build shots`.

## On the flasher site

`python tools/make_flasher_site.py site --stable dist --emu web/emu/build` copies the build's files to the site's
`try/` and adds `"try": "try/"` to `channels.json`; the flasher page then shows *Try it in your browser*.

CI (`.github/workflows/firmware.yml`, job `emulator`) builds it on every push, from the latest stable release (the tag
itself when a stable tag is pushed), with LVGL from GitHub at the firmware's version, cJSON 1.7.19 and Emscripten
6.0.11 (and espforge's tag when that release takes it). The `pages` job adds it to the site; if the emulator build
fails, the site is published without it.

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
  the display: the main loop runs the tasks meanwhile (espforge's `emu_tasks.c`), but LVGL doesn't draw until it is answered
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
