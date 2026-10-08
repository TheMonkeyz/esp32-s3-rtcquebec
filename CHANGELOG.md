# Changelog

What changed in each release. The display shows these notes on its update screen, and the settings page shows them
before you install; only the entries newer than the version you have are shown. The flasher site publishes them as
`notes.json`, built by `tools/make_flasher_site.py`.

How to write an entry: add a `## vX.Y.Z - YYYY-MM-DD` section at the top **before** tagging the release, with one
`- ` line per change, written for the person holding the display. Release candidates get their own
`## vX.Y.Z-rc.N - YYYY-MM-DD` section, shown only to Beta users; the final release's section lists everything again.

## v0.3.2 - 2026-10-08
- Behind the scenes: the street map now comes from espforge (v0.4.0's forge_map, shared with other displays); it looks and works as before.

## v0.3.2-rc.1 - 2026-10-08
- Behind the scenes: the street map now comes from espforge (v0.4.0's forge_map, shared with other displays); it looks and works as before.

## v0.3.1 - 2026-10-07
- Measuring the background noise can no longer go wrong when someone talks during it: the settings page says the room wasn't quiet enough and keeps the previous level. When it works, it shows the level measured.
- Behind the scenes: the display now uses espforge's own parts at a tested release (v0.3.0: Wi-Fi, updates, screens, screen dimming, the board) instead of copies of them, with a little more of the scarcest memory left free; nothing else changes on the screen.

## v0.3.1-rc.2 - 2026-10-07
- Behind the scenes: espforge's stable v0.3.0 (the same parts as rc.1, with a little more of the scarcest memory left free).

## v0.3.1-rc.1 - 2026-10-07
- Measuring the background noise can no longer go wrong when someone talks during it: the settings page says the room wasn't quiet enough and keeps the previous level. When it works, it shows the level measured.
- Behind the scenes: the display now uses espforge's own parts at a tested release (Wi-Fi, updates, screens, screen dimming, the board) instead of copies of them; nothing else changes on the screen.

## v0.3.0 - 2026-10-07
- The screen dims when the room has been quiet for 10 minutes and turns off after an hour; it lights up again when there is sound for a few seconds, when you pick up or move the display, or when you touch it.
- The touch that lights up a dark screen does only that: it doesn't open a map or change the page.
- New "Screen" section on the settings page: turn dimming on or off, choose when it dims and turns off, the brightness and the dimmed level, waking on pick-up; it shows the screen's state live.
- "Measure the background noise" on the settings page: stay quiet 5 seconds where the display sits, so it knows what counts as sound in your room.

## v0.3.0-rc.1 - 2026-10-07
- The screen dims when the room has been quiet for 10 minutes and turns off after an hour; it lights up again when there is sound for a few seconds, when you pick up or move the display, or when you touch it.
- The touch that lights up a dark screen does only that: it doesn't open a map or change the page.
- New "Screen" section on the settings page: turn dimming on or off, choose when it dims and turns off, the brightness and the dimmed level, waking on pick-up; it shows the screen's state live.
- "Measure the background noise" on the settings page: stay quiet 5 seconds where the display sits, so it knows what counts as sound in your room.

## v0.2.1 - 2026-10-07
- The map shows the route's path in blue, and the buses as small bus icons.
- Zoom the map: swipe down to zoom in, up to zoom out.
- Only the buses inside the map are shown (the ones farther away used to sit on its edge, off their route).
- The route's name at the top of the map is no longer cut by the round screen.
- Fixed: the map sometimes showed no buses, and couldn't be zoomed, when it had just been opened.

## v0.2.1-rc.1 - 2026-10-07
- The map shows the route's path in blue, and the buses as small bus icons.
- Zoom the map: swipe down to zoom in, up to zoom out.
- Only the buses inside the map are shown (the ones farther away used to sit on its edge, off their route).
- Fixed: the map sometimes showed no buses, and couldn't be zoomed, when it had just been opened.

## v0.2.0 - 2026-10-07
- Service alerts: swipe past your last stop for the RTC's current notices about your routes (works, detours, stops not served), with when they end. A stop page says when an alert applies to it.
- The buses on a map: tap a stop page to see its route's buses coming your way on a street map around the stop, updated every 20 seconds. Tap anywhere to go back.

## v0.2.0-rc.1 - 2026-10-07
- Service alerts: swipe past your last stop for the RTC's current notices about your routes (works, detours, stops not served), with when they end. A stop page says when an alert applies to it.
- The buses on a map: tap a stop page to see its route's buses coming your way on a street map around the stop, updated every 20 seconds. Tap anywhere to go back.

## v0.1.1 - 2026-10-07
- Adding a stop: a route number that isn't in use (like 999) now says the route doesn't exist, instead of "The RTC didn't answer".
- Try the display in your browser from the installer page: its own screens and settings page, with live departures for two stops.

## v0.1.1-rc.1 - 2026-10-07
- Adding a stop: a route number that isn't in use (like 999) now says the route doesn't exist, instead of "The RTC didn't answer".
- Try the display in your browser from the installer page: its own screens and settings page, with live departures for two stops.

## v0.1.0 - 2026-10-07
- Next departures at your favourite stops, one page each: the next bus in minutes, real time or scheduled, and the three after it. Swipe right for the system page.
- Add, order and remove your stops on the settings page: stop number, route, then pick the direction. It works from the display's setup network too.
- Wi-Fi setup from a phone (the display's setup network or Easy Connect), a settings page, and updates over Wi-Fi from the Stable or Beta channel.

## v0.1.0-rc.1 - 2026-10-06
- Next departures at your favourite stops, one page each: the next bus in minutes, real time or scheduled, and the three after it. Swipe right for the system page.
- Add, order and remove your stops on the settings page: stop number, route, then pick the direction.
- First version, built on espforge v0.2.1: Wi-Fi setup, settings page and updates over Wi-Fi.
