# Project history and retrospective

<!-- The story that connects the other docs, written for the next session. ARCHITECTURE says how things work,
TESTING how to check them, CLAUDE.md the lessons in short; this file says how the project got here. Update it at each
stable release and after each evaluation. -->

## In numbers

<!-- Days, commits, releases and tags; lines of C; boards and users. One line each. -->

## Timeline

<!-- One subsection per day or per release series: what was built, which bugs cost time, what changed in the way of
working. Name versions and lesson numbers (CLAUDE.md bug N, docs/LESSONS.md LN). -->

### 2026-10-06: project started from espforge v0.2.1

Repository TheMonkeyz/esp32-s3-rtcquebec created from the espforge template; names: app `rtc_quebec`, OTA site
https://themonkeyz.github.io/esp32-s3-rtcquebec/, setup network `RTC-Setup`. Goal: an RTC Nomade-like display (next
departures at favourite stops, alerts, live bus map). Research found no official RTC real-time feed; the user chose
to call RTC's website API directly (docs/ARCHITECTURE.md, Data sources).

## How the work is done now

<!-- The loop as this project actually runs it, if it differs from docs/WORKFLOW.md. Numbered steps. -->

## What paid off

<!-- Practices and tools that found bugs or saved time, each with the example that proves it. -->

## What cost time

<!-- What went wrong in the way of working (not single bugs): wrong assumptions, missing tools, reporting before
checking. Each with what to do instead. -->

## Design rules that emerged

<!-- Rules the code now follows, with the reason (e.g. "background work yields to the user"). -->

## Open threads

<!-- What is unfinished or known to be weak, with the numbers that say how weak. -->

- Swipe from a stop page to the system page: 40.5 fps with a 195 ms gap (an LVGL redraw right after the move),
  against 60.6 fps / 57 ms the other way (harness, v0.1.0-deps.5, 2026-10-06).
- Saving a new favourite asks RTC twice (the check, then the first fetch): the check's reply could be kept.
- Service alerts and the live bus map: endpoints not found yet.
