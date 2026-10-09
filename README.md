# RTC Québec display

A small round display that shows when the next RTC buses leave your stops in Québec City, like the RTC Nomade app:
real-time next departures at your favourite stops, service alerts for your routes and a live bus map. It runs on the
Waveshare ESP32-S3-Touch-AMOLED-1.75 and is built on [espforge](https://github.com/TheMonkeyz/espforge) (Wi-Fi
setup from a phone, a settings page, updates over Wi-Fi). Work in progress.

Not affiliated with the Réseau de transport de la Capitale (RTC). Departure times come from RTC's public website;
schedules use public information from the RTC (https://www.rtcquebec.ca/en/open-data).

## Try it on the board

**[Install from the web flasher](https://themonkeyz.github.io/esp32-s3-rtcquebec/)** (once the first release is
out): plug the board into a computer by USB, open the page in Chrome or Edge (desktop), pick **Stable** or **Beta**
and press Install. Then set up Wi-Fi from your phone with the display's setup network (`RTC-Setup`) or Easy Connect
(press and hold the screen), and pick your stops on the settings page. Release notes: [CHANGELOG.md](CHANGELOG.md).

## The loop

```
  change ──► test build, labelled above the release ──► flash + probe (devloop, test console)
                                                              │
                                                              ▼
  the user tries it ◄── prove it on the device (harness, snapshots, baseline)
         │
         ▼
  "document and commit" ──► Claude tags vX.Y.Z-rc.N ──► CI ──► Pages Beta channel
                                                                     │
                                                                     ▼
                         stable vX.Y.Z (user's OK) ◄── harness --ota installs and tests it
```

Every few releases a read-only evaluation turns into a fix plan, worked one release candidate per group.
Details: [docs/WORKFLOW.md](docs/WORKFLOW.md).

## Quick start

Prerequisites (Windows; Linux works for everything except the PC-as-phone Wi-Fi tests):

- [ESP-IDF v5.5.4](https://docs.espressif.com/projects/esp-idf/en/v5.5.4/esp32s3/get-started/) (the Windows
  installer puts it in `C:\Espressif\esp-idf`).
- Python 3.10+ (python.org rather than the Microsoft Store build, see L146), Node.js 22 for the page tests, WSL
  (Ubuntu) with gcc and make for the host tests, [GitHub CLI](https://cli.github.com/) for releases.
- The board on USB. [Claude Code](https://claude.com/claude-code) in the project folder.

Build (PowerShell):

```powershell
. C:\Espressif\esp-idf\export.ps1
idf.py -B build\v55 -D SDKCONFIG=build\v55\sdkconfig build
```

Flash through the helper (it owns the COM port and keeps a log window open while the agent acts):

```powershell
tools\devloop\start_flash_helper.bat            # once, in its own window (Claude Code runs flash_helper.ps1 in the background instead)
python tools\devloop\stage.py                   # copy the parts under unique names, check md5
python tools\devloop\devloop.py flash 120       # flash, then log 120 s to .devloop\serial_log.txt
```

Test everything:

```powershell
python tools\harness\harness.py                 # all suites; report in tools\harness\reports\<date>\
```

On first boot the display opens its setup network (`RTC-Setup`); join it with a phone, or scan the Easy Connect
code, and give it your Wi-Fi. The settings page is then at `https://<ip>/` (the address is in the log line
`web: Settings page: https://<ip>/`).

## Layout

```
CMakeLists.txt                  ESP-IDF project
forge.json                      the project's config: app, repo, ota_site, screens, setup SSID, log patterns
sdkconfig.defaults              build config (CONFIG_FORGE_* values, PSRAM, QIO, -O2, rollback, core dump)
sdkconfig.debug                 extra checks for a debug build (heap poisoning, stack watchpoint)
partitions.csv                  two OTA slots, otadata, NVS, core dump
main/                           the app: stops, alerts, map, setup; settings page; idf_component.yml takes
                                espforge's components and board at a tag
main/i18n_strings.h             every display text, English and Canadian French
main/web/index.html             the settings page
tools/fetch_forge.py            espforge at the pinned tag in .espforge/ (host tests, the emulator, CI)
tools/devloop/                  flash helper (Windows), devloop.py, stage.py; state files in .devloop/
tools/harness/                  harness.py, board.py, core_suites.py, app_suites.py, baseline.json, reports/
tools/snapshot.py               a screen rendered off-display, saved as PNG
tools/diag_summary.py           summary of the diag: lines in a log
tools/make_flasher_site.py      release parts (dist) and the Pages flasher/OTA site (site)
tools/webtest/                  Playwright tests of the settings page against a mock device
web/emu/                        the display in the browser (WebAssembly): the site's "Try it in your browser"
tests/host/                     C unit tests with gcc + AddressSanitizer (WSL / Linux)
docs/                           workflow, testing, releasing, lessons, protocol, components, templates
.claude/                        Claude Code permissions and skills
.github/                        CI (build, tests, releases, Pages) and Dependabot
```

## On the display

Drag up and down between your stops; swipe right for the stop's alerts, left for its map (drag down or up on the map
to zoom); touch and hold anywhere for Settings (offline: Wi-Fi setup). Moves follow the finger at ~66 fps: espforge's
forge_lvgl draws them as pictures copied straight to the panel.

- **stop** (one per favourite, up to 8): the route and direction, the next departure big (real time or scheduled), the
  three after it, when it was updated, and how many alerts the route has.
- **alerts**: the stop's route's notices in its direction, as RTC publishes them.
- **map**: the street map around the stop, the route's path and its buses heading that way.
- **Settings** (espforge's forge_settings): screen dimming and timing, wake on pick-up, brightness, language, the
  settings page's QR code, Wi-Fi, updates, restart, and About (version, network, memory).
- **setup** / **setup1**: Wi-Fi setup: the setup network's QR code, and Easy Connect's.

The settings page (`https://<ip>/`) shows device info, Wi-Fi (scan and save), language (English / français) and
updates (channel, check, install, release notes). Everything visible goes through i18n; French is Canadian French.

## Documentation

| File | What |
|---|---|
| [CLAUDE.md](CLAUDE.md) | Rules and setup for agent sessions; the project's bugs, preferences and facts grow here |
| [docs/WORKFLOW.md](docs/WORKFLOW.md) | The loop in detail, "document and commit", evaluation → fix plan, working with or without USB |
| [docs/TESTING.md](docs/TESTING.md) | Builds, devloop, test console, snapshots, harness, host and page tests, profiling |
| [docs/RELEASING.md](docs/RELEASING.md) | Version labels, CHANGELOG, rc and stable releases, CI, Pages, restoring a board |
| [docs/LESSONS.md](docs/LESSONS.md) | 150+ lessons by topic, each with its origin and how to check it |
| [docs/PROTOCOL.md](docs/PROTOCOL.md) | What the PC tools rely on from the firmware: files, console, log lines, HTTP API, OTA site |
| [docs/COMPONENTS.md](docs/COMPONENTS.md) | The framework components and the board API |
| [docs/NEW-PROJECT.md](docs/NEW-PROJECT.md) | Start a project from this template; add a board |
| [docs/templates/](docs/templates/) | HISTORY, ARCHITECTURE, DIAGNOSTICS, IDEAS, EVALUATION, FIX-PLAN skeletons |

## Credits and licence

Built on [espforge](https://github.com/TheMonkeyz/espforge), itself extracted from
[esp32-s3-weather](https://github.com/TheMonkeyz/esp32-s3-weather) (TheMonkeyz). MIT licence, see
[LICENSE](LICENSE); third-party parts in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
