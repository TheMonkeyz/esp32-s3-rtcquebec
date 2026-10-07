# The display in the browser (README.md): LVGL + the firmware's screens built to WebAssembly with Emscripten.
#   make            # in WSL, after `source ~/emsdk/emsdk_env.sh`: build/emu.js + build/emu.wasm, then open index.html
# LVGL: the firmware's own copy (ESP-IDF's component manager puts it in managed_components); cJSON: ESP-IDF's.
LVGL ?= ../../managed_components/lvgl__lvgl
IDF_PATH ?= /mnt/c/Espressif/esp-idf
CJSON ?= $(IDF_PATH)/components/json/cJSON
ifeq ($(filter clean,$(MAKECMDGOALS)),)
ifeq ($(wildcard $(CJSON)/cJSON.c),)
$(error no cJSON at $(CJSON): set IDF_PATH (source ESP-IDF's export.sh) or CJSON=<folder with cJSON.c>)
endif
ifeq ($(wildcard $(LVGL)/lvgl.h),)
$(error no LVGL at $(LVGL): build the firmware once (idf.py fetches it into managed_components) or LVGL=<lvgl checkout>)
endif
endif
MAIN := ../../main
COMP := ../../components
BOARD := ../../boards/ws_amoled175/board
OUT := build
# miniz 3.0.2 (MIT): its tinfl is what png_rows.c finds in the ESP32-S3's ROM (the map's tiles; as tests/host)
MINIZ := $(OUT)/miniz
VERSION ?= $(shell git describe --tags --always 2>/dev/null || echo dev)

# The firmware, unchanged: main.c runs as a task (emu_main.c), its routes serve the settings page (emu_web.c).
# presence.c (microphones, motion sensor) is not: emu_presence.c stands in for it (the screen stays on, "No microphone")
FIRMWARE := main.c ui.c app_text.c rtc_api.c favs.c departures.c map.c geo.c presence_sm.c
FORGE_CORE := i18n.c i18n_nvs.c textfit.c testcon_registry.c png_rows.c
FORGE_LVGL := forge_lvgl.c pager.c slide.c screens.c
FORGE_NET := svc.c
FORGE_OTA := ota_web.c
EMU := emu_main.c emu_display.c emu_touch.c emu_http.c emu_nvs.c emu_stubs.c emu_tasks.c emu_web.c emu_time.c \
       emu_presence.c
LV_SRC := $(shell find $(LVGL)/src -name '*.c')

CFLAGS := -O2 -DEMU_BUILD '-DEMU_VERSION="$(VERSION)"' -I. -Ishim -I$(MAIN) -I$(COMP)/forge_core/include \
          -I$(COMP)/forge_lvgl/include -I$(COMP)/forge_net/include -I$(COMP)/forge_ota/include -I$(BOARD)/include \
          -I$(LVGL) -I$(LVGL)/src -I$(CJSON) -I$(MINIZ) \
          -include string.h -include stdint.h '-DLV_CONF_KCONFIG_EXTERNAL_INCLUDE="lv_kconfig.h"' \
          -DLV_LVGL_H_INCLUDE_SIMPLE -Wno-unused-parameter
# The firmware's local time is its TZ (main.c sets it); Emscripten's localtime_r ignores TZ (the browser's zone):
# emu_time.c follows TZ instead, so a visitor anywhere sees Québec's times and service date, as on the display
FWFLAGS := -Dlocaltime_r=emu_localtime_r
LDFLAGS := -sASYNCIFY -sASYNCIFY_STACK_SIZE=65536 -sALLOW_MEMORY_GROWTH -sSTACK_SIZE=262144 -sENVIRONMENT=web \
           -sEXPORTED_FUNCTIONS=_main,_emu_fb,_emu_fb_dirty,_emu_touch,_emu_brightness,_malloc,_free \
           -sEXPORTED_RUNTIME_METHODS=HEAPU8,HEAPU16 -sMODULARIZE -sEXPORT_NAME=RtcEmu

OBJ := $(patsubst %.c,$(OUT)/fw/%.o,$(FIRMWARE)) $(patsubst %.c,$(OUT)/forge_core/%.o,$(FORGE_CORE)) \
       $(patsubst %.c,$(OUT)/forge_lvgl/%.o,$(FORGE_LVGL)) $(patsubst %.c,$(OUT)/forge_net/%.o,$(FORGE_NET)) \
       $(patsubst %.c,$(OUT)/forge_ota/%.o,$(FORGE_OTA)) $(patsubst %.c,$(OUT)/emu/%.o,$(EMU)) \
       $(patsubst $(LVGL)/%.c,$(OUT)/lvgl/%.o,$(LV_SRC)) $(OUT)/cjson/cJSON.o $(OUT)/emu/fonts.o $(OUT)/miniz/miniz.o

all: $(OUT)/emu.js $(OUT)/index.html $(OUT)/settings.html $(OUT)/emu-settings.js $(OUT)/fonts/montserrat.ttf

$(OUT)/emu.js: $(OBJ)
	emcc $(CFLAGS) $(LDFLAGS) $(OBJ) -o $@
	@ls -l $(OUT)/emu.js $(OUT)/emu.wasm

$(OUT)/index.html: index.html
	cp $< $@

# index.html takes the site's font from ../fonts/ (the flasher site's, tools/make_flasher_site.py): served from build/,
# that is build/fonts/ (only for a local run; the site has its own)
$(OUT)/fonts/montserrat.ttf: $(MAIN)/montserrat.ttf
	@mkdir -p $(dir $@)
	cp $< $@

# The display's own settings page under the emulator: unchanged, with emu-settings.js first in its <head> (its requests
# to /api/ go to the emulator, emu_web.c)
$(OUT)/settings.html: $(MAIN)/web/index.html
	@mkdir -p $(OUT)
	sed 's|<head>|<head><script src="emu-settings.js"></script>|' $< > $@   # (one <head>; macOS's sed has no 0,/re/)
	@grep -q 'emu-settings.js' $@ || (echo "settings.html: no <head> to put emu-settings.js in" && rm -f $@ && false)
$(OUT)/emu-settings.js: emu-settings.js
	@mkdir -p $(OUT)
	cp $< $@

$(OUT)/fw/%.o: $(MAIN)/%.c lv_kconfig.h
	@mkdir -p $(dir $@)
	emcc $(CFLAGS) $(FWFLAGS) -c $< -o $@
$(OUT)/forge_core/%.o: $(COMP)/forge_core/%.c lv_kconfig.h | $(MINIZ)/miniz.h
	@mkdir -p $(dir $@)
	emcc $(CFLAGS) -c $< -o $@
$(OUT)/forge_lvgl/%.o: $(COMP)/forge_lvgl/%.c lv_kconfig.h
	@mkdir -p $(dir $@)
	emcc $(CFLAGS) -c $< -o $@
$(OUT)/forge_net/%.o: $(COMP)/forge_net/%.c lv_kconfig.h
	@mkdir -p $(dir $@)
	emcc $(CFLAGS) -c $< -o $@
$(OUT)/forge_ota/%.o: $(COMP)/forge_ota/%.c lv_kconfig.h
	@mkdir -p $(dir $@)
	emcc $(CFLAGS) -c $< -o $@
$(OUT)/emu/%.o: %.c lv_kconfig.h
	@mkdir -p $(dir $@)
	emcc $(CFLAGS) -c $< -o $@
$(OUT)/emu/fonts.o: $(OUT)/fonts.c
	@mkdir -p $(dir $@)
	emcc $(CFLAGS) -c $< -o $@
$(OUT)/lvgl/%.o: $(LVGL)/%.c lv_kconfig.h
	@mkdir -p $(dir $@)
	@emcc $(CFLAGS) -c $< -o $@
$(OUT)/cjson/cJSON.o: $(CJSON)/cJSON.c
	@mkdir -p $(dir $@)
	emcc $(CFLAGS) -c $< -o $@

# The TTF font the firmware embeds, as an array (ui.c, EMU_BUILD)
$(OUT)/fonts.c: $(MAIN)/montserrat.ttf
	@mkdir -p $(OUT)
	python3 -c "import sys; \
f = lambda n, p: 'const unsigned char %s_start[] = {%s};\nconst unsigned char *const %s_end = %s_start + %d;\n' % (n, ','.join(str(b) for b in open(p, 'rb').read()), n, n, len(open(p, 'rb').read())); \
open('$@', 'w').write(f('ttf', '$(MAIN)/montserrat.ttf'))"

clean:
	rm -rf $(OUT)
.PHONY: all clean

# miniz for png_rows.c (see MINIZ above): downloaded once
$(MINIZ)/miniz.c: $(MINIZ)/miniz.h
$(MINIZ)/miniz.h:
	@mkdir -p $(MINIZ)
	curl -fsSL -o $(OUT)/miniz.zip https://github.com/richgel999/miniz/releases/download/3.0.2/miniz-3.0.2.zip
	python3 -c "import zipfile; zipfile.ZipFile('$(OUT)/miniz.zip').extractall('$(MINIZ)', ['miniz.c', 'miniz.h'])"
$(OUT)/miniz/miniz.o: $(MINIZ)/miniz.c
	emcc -O2 -c $< -o $@
