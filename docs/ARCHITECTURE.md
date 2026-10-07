# Architecture

<!-- How the firmware works and why it works that way. For each part: what it does, the rules that came from bugs
(with the numbers), and what to check after touching it. Framework components are described in docs/COMPONENTS.md;
describe here only how this app uses them. -->

## Hardware (Waveshare ESP32-S3-Touch-AMOLED-1.75)

| Part | Details | Pins |
|---|---|---|
| MCU | ESP32-S3, 16 MB flash, 8 MB octal PSRAM | |
| Display | CO5300 AMOLED 466×466 round, QSPI, RGB565 | CS 12, CLK 38, D0–D3 4/5/6/7, RST 39; column offset +6 |
| Touch | CST9217, I2C `0x5A` | SDA 15, SCL 14, RST 40 |
| Motion sensor | QMI8658, I2C `0x6B` (shared bus) | SDA 15, SCL 14 |
| USB | USB serial/JTAG (COM5 on the dev PC) | |

Board support: `boards/ws_amoled175/board/` (espforge).

## Tasks

<!-- Every task: core / priority, stack size (from measured high-water marks: docs/LESSONS.md L63), its job. The
locking rule for LVGL calls from outside the LVGL task. -->

| Task | Core / priority, stack | Job |
|---|---|---|

## Display pipeline

<!-- Buffers, flush, bus speed, anything that draws outside LVGL and the rules that keep it safe. -->

## Screens

One pager (`ui.c`, forge_lvgl's pager with slide.c drags): **system** (page 0) | **stop** (page 1) | the other
favourites (**stop2**..**stop8**) | **alerts** (last). It holds 2 + `FAVS_MAX` (8) pages, built once;
`pager_set_order()` (added to forge_lvgl on 2026-10-07, on top of `pager_set_count()`) shows system, max(1,
favourites) stop pages and the alerts page, the others hidden after them, so a swipe never reaches an unused page.
Stop pages are found by their object (`sp[i].page`), not by index. The display opens on the
first stop; swipe right for the system page. Long-press anywhere: Wi-Fi setup (espforge).

- **stop pages** (`stop_create` / `stop_refresh`): clock at the top; the route number on an accent badge; the
  direction; stop name and number; the next departure big ("5 min", "< 1 min", or the time itself when an hour or
  more away) with "Real time" (green) / "Scheduled" (dim) / "Cancelled" (red, struck through); the three after it
  in green (real time) or dim (scheduled); a status line: "Updated at 23:07", "Can't reach the RTC" (failing and
  older than 2 min), "Route 800 doesn't stop here in this direction" (RTC said 404), "Stop not served for now",
  "Drop-off only". Minutes count down from the departure times every second between fetches; a departure gone for
  30 s disappears; data older than 10 min shows "--" rather than times that may be wrong.
- **stop without favourites**: page 1 says to add stops on the settings page (swipe right, scan the code).
- **stop pages' alert line** (orange, y 392): "1 alert for this route" / "N alerts..." when a notice names the
  favourite's route in its direction (`deps_alerts_for`).
- **alerts** (`alerts_create` / `alerts_refresh`): RTC's notices for the favourite routes in their directions,
  deduplicated, urgent first then newest, at most 12: the favourite routes concerned on a badge, the title (orange
  when RTC marks it urgent) and subtitle, "Start: ..." / "End: ..." as RTC writes them (French: RTC publishes its
  notices in French only). A list that scrolls up and down (sideways is the pager's); rebuilt only when the notices
  or the language change (a signature of their ids). Status line: "Updated at", or "Can't reach the RTC" after 30 min
  of failures. Empty: "No alerts for your routes".
- **system**: espforge's page (version, Wi-Fi, address, memory, uptime, updates, settings QR code).

Every second the `tick` timer refreshes every stop page, the ones not shown too (a swipe shows a neighbour's picture
rendered in the background); `set_text` / `set_color` change a label only when it differs.

## Data sources

Decided 2026-10-06 (user): **the display calls RTC's web API directly**, for personal use.

- RTC publishes no official real-time feed (no GTFS-Realtime). The official open data is a static GTFS zip
  (https://cdn.rtcquebec.ca/Site_Internet/DonneesOuvertes/googletransit.zip, ~33 MB, `stop_times.txt` 173 MB, new
  version almost daily): far too big for the device, but handy on the PC to find which stops a route serves
  (CLAUDE.md, Useful facts). Licence: free, with attribution "...public information from Réseau de transport de la
  Capitale, updated on [date]" (https://www.rtcquebec.ca/en/open-data).
- Real-time departures: the undocumented API used by rtcquebec.ca, first seen in the MIT extension
  https://github.com/jebeaudet/ChRomeTC. Plain GET over HTTPS, no key, JSON, behind Akamai
  (`Cache-Control: max-age=9`). Not covered by the open-data licence, no service guarantee, it has changed before.
  Ask RTC for permission before distributing devices.
  - `RTC_API = https://api-iv.rtcquebec.ca/api/legacy`
  - `/BorneVirtuelle_ArretParcours?noArret=1025&noParcours=800&codeDirection=0&date=yyyymmdd`: ~1.1 KB,
    `parcours` {noParcours, descriptionDirection}, `arret` {nom, description, latitude, longitude},
    `arretNonDesservi`, `descenteSeulement`, `horaires` [5 x {depart "2026-10-06T22:46:57-04:00", departMinutes,
    ntr (real time), annule (cancelled), nomDestination}]. **404 (an HTML page)**: that route doesn't stop there in
    that direction.
  - `/Parcours_Periode?noParcours=800&date=yyyymmdd`: ~0.5 KB, {description, codeDirectionPrincipale,
    descriptionDirectionPrincipale, codeDirectionRetour, descriptionDirectionRetour}. 404: no such route. The
    direction codes so far match GTFS `direction_id` (800: 0 = Colline Parlementaire).
  - Replies saved on 2026-10-06: `tests/host/data/rtc_*.json`; `rtc_api.c` parses them (host test `test_rtc_api.c`).
- **Polling rules** (`departures.c`, one task "deps" makes every request, settings-page lookups included):
  the favourite on view every 30 s (`DEPS_SHOWN_S`), every other one at most every 5 min (`DEPS_HIDDEN_S`), 2 s
  between two requests, nothing while offline or before SNTP sets the clock (the request needs today's date). The
  service is "RTC" in the health list (svc: shown on the status lines and `/api/info`), User-Agent
  `rtc_quebec/<version> (+https://github.com/TheMonkeyz/esp32-s3-rtcquebec)`. Timeout 10 s.
- Everything that knows the API's shape is in `rtc_api.c` / `rtc_api.h`: when RTC changes it, that file and its
  test data change, nothing else.
- **Service alerts**: the website's own Drupal JSON:API, seen in its home page's requests (2026-10-07):
  `https://www.rtcquebec.ca/en/api/notices_v2` (only under `/en/`; `/fr/api` is a 404). The display asks per route
  (`rtc_notices_url`): published (`status`), started (`date_notice_start <= now`), not ended (`date_notice_end >
  now` OR null), naming the route (`parcours.routes.name`), newest first, 6 at most, only the fields it shows
  (title, subtitle, start/end, `description_work_begin` / `_end` (short HTML), `urgent`) and the related
  `paragraph--avis_parcours` -> `taxonomyTermsRoutes` (`name` = route number, `code_direction`). The query is
  ~1.3 KB (esp_http_client's send buffer is raised to 2 KB); a reply is ~1.4 KB with no notice, ~13 KB for one
  notice naming 15 routes (most of it JSON:API links); `Cache-Control: max-age=60`; CORS echoes the page's origin
  (the emulator works). Every 10 min per distinct favourite route (`DEPS_ALERTS_S`), only when no departures are
  due. `rtc_parse_notices` (host test on replies saved 2026-10-07, `tests/host/data/rtc_notices_*.json`) keeps the
  route/direction pairs; `rtc_notice_for` matches a favourite. Struck-out text in the HTML (`<s>25 septembre</s> /
  indéterminée`: a date replaced) is dropped.
- To investigate: vehicle positions (live map) endpoint; map tiles (weather_amoled shows OpenStreetMap tiles: check
  the tile usage policy).

## Settings and web API

Routes added by `main.c` (on top of docs/PROTOCOL.md §4):

| Route | Key | What |
|---|---|---|
| `GET /api/favs` | no | `{"max":8,"favs":[{"stop","route","dir","stop_name","direction"}]}` (names once fetched) |
| `POST /api/favs` | yes (none on the setup network) | the whole list `{"favs":[{"stop","route","dir"}]}` in page order; a favourite not already in the list is checked with RTC first. `{"ok":true}` or `{"ok":false,"bad":<index>,"why":"invalid"\|"not_served"\|"rtc"\|"save"}` |
| `POST /api/route` | yes (none on the setup network) | `{"route":"800"}` -> `{"ok":true,"route","name","dirs":[{"code","name"}x2]}` or `{"ok":false,"why":"no_route"\|"rtc"}` |
| `POST /api/settings` | yes | `{"lang":"en"\|"fr"}` (espforge's starter) |

Refusals answer 200 with `ok:false` (a 404 / 502 made the browser log errors the page tests count as failures).
The lookups run in the deps task (`deps_lookup_route`, `deps_check_fav`); the HTTP handler waits up to 20 s for it to
take the job, then for the end (the job is on the handler's stack).

The page's "My stops" section: the list (move up, remove), then stop number + route -> "Find directions" -> pick a
direction -> "Add this stop". Tested against the mock (`tools/webtest/tests/stops.spec.js`; the mock knows routes
800 and 11, stops 1025 and 1005 on 800 direction 0, 1026 on direction 1; route 999 = RTC unreachable).

NVS namespace `favs`: `n` (u8) and `f0`..`f7` strings `"<stop>/<route>/<dir>"` (`favs.c`; typed keys, not a blob).

## Languages

English and French (Canada), espforge's i18n: display texts in `main/i18n_strings.h`, page texts in `I18N` in
`main/web/index.html`. French is Québec French, standard written; RTC's own words where they exist ("Descente
seulement", "parcours", "arrêt").

## Updates

<!-- Partition layout, OTA site, channels, anything app-specific about rollback. -->

## Memory budget

<!-- Table of big users (where, size), measured internal RAM free / min ever, PSRAM min. -->

| Item | Where | Size |
|---|---|---|

## Known issues / TODO

<!-- One bullet each, with the version it was seen in. -->
