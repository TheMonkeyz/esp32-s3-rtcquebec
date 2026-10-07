# Changelog

What changed in each release. The display shows these notes on its update screen, and the settings page shows them
before you install; only the entries newer than the version you have are shown. The flasher site publishes them as
`notes.json`, built by `tools/make_flasher_site.py`.

How to write an entry: add a `## vX.Y.Z - YYYY-MM-DD` section at the top **before** tagging the release, with one
`- ` line per change, written for the person holding the display. Release candidates get their own
`## vX.Y.Z-rc.N - YYYY-MM-DD` section, shown only to Beta users; the final release's section lists everything again.

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
