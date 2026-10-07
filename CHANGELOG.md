# Changelog

What changed in each release. The display shows these notes on its update screen, and the settings page shows them
before you install; only the entries newer than the version you have are shown. The flasher site publishes them as
`notes.json`, built by `tools/make_flasher_site.py`.

How to write an entry: add a `## vX.Y.Z - YYYY-MM-DD` section at the top **before** tagging the release, with one
`- ` line per change, written for the person holding the display. Release candidates get their own
`## vX.Y.Z-rc.N - YYYY-MM-DD` section, shown only to Beta users; the final release's section lists everything again.

## v0.1.0-rc.1 - 2026-10-06
- Next departures at your favourite stops, one page each: the next bus in minutes, real time or scheduled, and the three after it. Swipe right for the system page.
- Add, order and remove your stops on the settings page: stop number, route, then pick the direction.
- First version, built on espforge v0.2.1: Wi-Fi setup, settings page and updates over Wi-Fi.
