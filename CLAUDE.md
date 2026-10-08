# Notes for AI-assisted sessions

Read first, in this order: `README.md`, `docs/WORKFLOW.md` (the loop), `docs/TESTING.md` (how to prove a change),
`docs/LESSONS.md` (what went wrong before, L1..), `docs/PROTOCOL.md` (what the tools read from the firmware),
`docs/COMPONENTS.md` (the framework's components), then this project's `docs/HISTORY.md` and
`docs/ARCHITECTURE.md` once they exist (templates in `docs/templates/`). Project values (app name, repo, OTA site,
screens, build dir) are in `forge.json`.

## The loop

1. Change, then a test build labelled above the current release (`version.txt`, touch `CMakeLists.txt`).
2. Flash and probe: `python tools/harness/harness.py --flash <bin>` or the devloop helper + test console.
3. Prove it on the device: log lines, snapshots, the harness's checks and baseline.
4. The user tries it (real fingers, real phone).
5. "Document and commit": ARCHITECTURE, TESTING, this file, CHANGELOG.
6. Claude tags and tests a release candidate (`harness.py --ota vX.Y.Z-rc.N`); a stable tag only with the user's OK.

Details: `docs/WORKFLOW.md`, `docs/RELEASING.md`. Skills in `.claude/skills/` hold the step lists.

## Rules that are not negotiable

- **Stage parts under unique names and compare md5 before flashing** (`tools/devloop/stage.py`). A reused path has
  delivered a stale file (L1).
- **Check `ota: Running <ver> from ota_N` in a fresh log before testing** (L2).
- **Label test builds above the current release**; delete `version.txt` when done (L9, L10).
- **Build exactly what you commit.** Set later work aside with `git stash push -u`, build, commit, `git stash pop`
  (L14).
- **Run the host tests before tagging** when a C file they compile changed: `wsl make -C tests/host` (L15).
- **Ask before a stable tag.** Release candidates Claude may tag, push and test itself (L18).
- **One-line progress note before anything that takes more than a minute** (build, flash, log window) (L41).
- **Ask the user to interact during the log window, and say exactly when and what** (L42).
- **Never take over the user's screen.** Ask first; prefer the flash helper (L43).
- **Relay `>>> ASK THE USER` from the harness at once** (L44).
- **Every fix gets a test that fails on the old code** (harness, Playwright or host test) (L57).
- **Reset cached board facts (`.devloop/ip`, `.devloop/key`) after a flash or an install** (L7).
- **Never restart a board in the first 60 s after an update** (it rolls back) (L23).
- **Report what the log shows**, not what a message suggests (L46).

Lessons by topic (memory, LVGL, touch, Wi-Fi, OTA, Windows tooling…): `docs/LESSONS.md`. Add a lesson there when a
bug teaches something general; add the project-specific fact below.

## Built on espforge

This project was created from the [espforge](https://github.com/TheMonkeyz/espforge) template (v0.2.1, 2026-10-06;
local checkout `C:\Users\lmathieu\ESPDEV\espforge`). **Since v0.3.1 it takes espforge's components and board at a
release tag** (as weather_amoled): forge_core, forge_net, forge_ota, forge_lvgl, forge_presence, dns_server and the
board (`boards/ws_amoled175/board`) in `main/idf_component.yml`, all at the same tag; the component manager fetches them
into `managed_components/`. Host tests and the emulator use `tools/fetch_forge.py` (espforge at that tag in
`.espforge/`, git-ignored): the emulator's framework part is espforge's `web/emu/forge`. The tools (`tools/devloop`,
`tools/harness`, `tools/webtest`, `tools/make_flasher_site.py`) are still copies: port fixes both ways.

- **A framework change** is made in espforge, released there as an rc (its tests, its harness), then the tags here are
  bumped together (delete `dependencies.lock` first). To try an unreleased espforge change here first, point the
  entries at the checkout for one build (`override_path:`, as weather_amoled's `tools/forge_local.py`; never commit it).
- **Keep app code in `main/`.** Screen dimming is espforge's forge_presence; this app gives it the board's
  microphones, motion sensor, touch and brightness (`presence_hooks` in main.c).
- **espforge backlog** (the user's rule, 2026-10-07, for esp32-s3-rtcquebec, weather_amoled and espforge): when a
  change could go into espforge (framework code, board support, tools, tests, docs, a lesson), add an entry to
  espforge's `docs/BACKLOG.md` (`C:\Users\lmathieu\ESPDEV\espforge\docs\BACKLOG.md`, its format at the top) in the
  same session, before calling the work done. Alignment sessions work through it for future projects.

## The app

An RTC Nomade-like display for Québec City buses: favourite stops' next departures (real time), alerts, live map.
Data: RTC's website API called directly (undocumented, personal use, decided 2026-10-06): see
docs/ARCHITECTURE.md "Data sources" for its endpoints and polling rules. Never poll faster than those rules allow.

## Working setup (this PC)

- **Board:** Waveshare ESP32-S3-Touch-AMOLED-1.75 on **COM5** of a Windows 11 PC (`forge.json` `port` empty = auto).
- **ESP-IDF v5.5.4** at `C:\Espressif\esp-idf` (CI uses the same version, from `forge.json` `idf`). In PowerShell:
  `. C:\Espressif\esp-idf\export.ps1`, then
  `idf.py -B build\v55 -D SDKCONFIG=build\v55\sdkconfig build`.
- **GitHub CLI** signed in as TheMonkeyz; not on the PATH of older shells: Git Bash
  `"/c/Program Files/GitHub CLI/gh.exe"`, PowerShell `& "C:\Program Files\GitHub CLI\gh.exe"`.
- **Host tests** run in WSL (Ubuntu): `wsl make -C tests/host`. **Settings page tests**: `cd tools/webtest && npm test`
  (Node in `C:\Program Files\nodejs`).
- **Two ways to work:**
  1. **Claude Code on the PC** (preferred): builds, `idf.py`, the harness and `gh` run directly. The flash helper
     still owns the COM port while it is logging; the harness talks to it. **Claude starts the helper itself, in the
     background** (`run_in_background`: `powershell -NoProfile -ExecutionPolicy Bypass -File tools\devloop\flash_helper.ps1`), so no window
     opens on the user's screen (user's rule, 2026-10-06, as weather_amoled does); restart it the same way after
     editing `flash_helper.ps1` (L6). `start_flash_helper.bat` (a window) only when the user asks for one.
  2. **Claude desktop app / cloud**: builds run in a cloud container, the shell has **no USB** and can't type into
     Windows terminals. The user starts `tools\devloop\start_flash_helper.bat` once; Claude stages the parts, runs
     `python tools/devloop/devloop.py flash 120` (or writes `.devloop/flash.request`), waits for
     `.devloop/flash.done` and reads `.devloop/serial_log.txt`. The cloud can't reach the board's IP: no
     snapshots, no harness. Cloud build recipe: `docs/WORKFLOW.md`.
- The display's IP is in the log (`web: Settings page: https://<ip>/`); its key comes from the console (`key`).

## How the user likes to work

- Short progress notes; no screen takeover; changes verified on the device (log + snapshot) before they are called
  done.
- Stable releases only with their OK; rc releases Claude publishes and tests, then reports.
- New features → minor version bump.
- **French = Canadian French (Québec), standard written:** no anglicisms, no slang; **1er** for the first of the
  month; check grammar agreement when a noun changes.
- New display text goes in `main/i18n_strings.h` (`X(T_ID, "English", "Français")`), never a bare literal on screen;
  page text goes in `I18N` in `main/web/index.html`. Check French in snapshots: it is longer and wraps.

## Bugs hit, and their fixes

Number them; one entry per bug: symptom, cause, fix, the test that guards it. General lessons go to docs/LESSONS.md.

## User preferences learned

Add each preference the user states (gestures, wording, timing, look) with the date, in their words when possible.

## Useful facts

Board facts, API quirks, numbers measured on this hardware, test recipes: anything a future session would otherwise
rediscover.

- **Real stops for tests** (2026-10-06): route 800 direction 0 (Colline Parlementaire) stops at 1025 (St-Dominique);
  direction 1 (Terminus Chute-Montmorency) at 1105 (St-Dominique), **not** 1026. To find more: download the static
  GTFS (docs/ARCHITECTURE.md, Data sources), take one trip of `route_id` `1-<route>` and `direction_id` from
  `trips.txt`, its stops from `stop_times.txt`, their `stop_code` from `stops.txt`. Never guess stop numbers at the
  live API: each guess is a request to RTC.
- **Adding favourites without the page**: `Board.api('/api/favs', {'favs': [...]})` from tools/harness (it knows the
  key); then `snapshot.py stop`.
- **forge_lvgl `pager_set_count()` / `pager_set_order()`** were added here (2026-10-06/07): espforge has them since
  v0.3.0.
- **Playwright's browser** on a fresh clone: `cd tools/webtest && npm ci && npx playwright install chromium`, then
  copy `%LOCALAPPDATA%\ms-playwright` to `tools/webtest/.browsers` (git-ignored): the harness runs under the
  Microsoft Store Python, which hides AppData\Local from its child processes (L146), so without the copy its webtest
  step fails every test in ~1 ms ("Executable doesn't exist") while `npm test` by hand passes.
- **Screen names**: `stop` is the first favourite, `stop2`..`stop8` the others (registered only for pages in use);
  the harness's navigation test reads `/api/favs` and swipes through all of them.
