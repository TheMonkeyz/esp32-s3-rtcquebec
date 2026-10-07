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

Planned v1 (a port of RTC Nomade, Android): favourite stops' next departures (one swipe screen each, chosen on the
settings page), service alerts for those routes, a live bus map; plus espforge's system and Wi-Fi setup screens.

<!-- One subsection per screen in forge.json "screens": what it shows, where the data comes from, gestures, what
marks it out of date. -->

## Data sources

Decided 2026-10-06 (user): **the display calls RTC's web API directly**, for personal use.

- RTC publishes no official real-time feed (no GTFS-Realtime). The official open data is a static GTFS zip
  (https://cdn.rtcquebec.ca/Site_Internet/DonneesOuvertes/googletransit.zip, ~33 MB, `stop_times.txt` 173 MB, new
  version almost daily): far too big for the device. Licence: free, with attribution "…public information from
  Réseau de transport de la Capitale, updated on [date]" (https://www.rtcquebec.ca/en/open-data).
- Real-time departures: the undocumented API used by rtcquebec.ca, seen in the MIT extension
  https://github.com/jebeaudet/ChRomeTC (`js/popup.js`):
  `https://api-iv.rtcquebec.ca/api/legacy/BorneVirtuelle_ArretParcours?noArret=&noParcours=&codeDirection=&date=yyyymmdd`
  (next passages for one stop + route + direction) and `Parcours_Periode?noParcours=&date=` (a route's directions).
  Not covered by the open-data licence, no service guarantee, it has changed before. Rules: poll slowly (only the
  screen on view, ≥ 30 s), send our User-Agent, keep the parsing in one module so a change is one fix, show
  "schedule unavailable" rather than stale times. Ask RTC for permission before distributing devices.
- To investigate: the endpoints for service alerts and vehicle positions (live map), and map tiles (weather_amoled
  shows OpenStreetMap tiles; check the tile usage policy).

## Settings and web API

<!-- The app's routes on top of docs/PROTOCOL.md §4, NVS namespaces and keys (typed keys, not blobs: L68). -->

## Languages

<!-- Languages offered, rules for each (French = Québec, "1er"), where texts live. -->

## Updates

<!-- Partition layout, OTA site, channels, anything app-specific about rollback. -->

## Memory budget

<!-- Table of big users (where, size), measured internal RAM free / min ever, PSRAM min. -->

| Item | Where | Size |
|---|---|---|

## Known issues / TODO

<!-- One bullet each, with the version it was seen in. -->
