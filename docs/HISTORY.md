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

### 2026-10-07 to 10-09: v0.4.0, the weather_amoled navigation and a shared Settings screen

The user's redesign: stops in a column (vertical drags, as weather_amoled's places), the row alerts | stops | map,
Settings on a long press with weather_amoled's options, and "maybe the whole settings screen should be standardized
in espforge". Done framework-first: espforge v0.5.0-rc.1 (PR #26) brought forge_settings and nested pagers
(`pager_on_view`, four neighbour pictures), built and flashed here against the checkout (`tools/forge_local.py`)
before the tag. The harness found three bugs before the user did (CLAUDE.md bugs 2-3: a console-task stack overflow,
a map page slow enough to lose swipes), and the user's question about "updated 5 minutes ago" was a start-up bug as
old as v0.1 (bug 1). Tooling lessons: the flash helper and the harness now follow the staged build folder (espforge
L198), and the desktop app's shell needs `IDF_COMPONENT_CACHE_PATH` for a build that fetches a new espforge tag.

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

- Opening the map: one 150-200 ms compose right after the drag lands (picture copied, path drawn: harness
  swipe_gap_max_ms.stop_to_map 175-194 ms). Drawing the path into forge_map's tiles once would remove it.
- Lists (alerts, Settings) scroll through LVGL at ~25 fps: espforge backlog "list scrolls drawn as pictures".
- Saving a new favourite asks RTC twice (the check, then the first fetch): the check's reply could be kept.
- Service alerts and the live bus map: endpoints not found yet.
