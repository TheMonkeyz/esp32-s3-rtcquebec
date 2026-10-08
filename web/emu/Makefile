# The RTC Québec display in the browser: what this app builds; the rules, the stand-ins for the hardware and the page's
# script are espforge's (web/emu/forge at the tag in main/idf_component.yml, cloned by tools/fetch_forge.py).
#   python3 ../../tools/fetch_forge.py && make -j8     # in WSL, after `source ~/emsdk/emsdk_env.sh`: build/
#   make config                                        # lv_kconfig.h, sdkconfig.h from the firmware's sdkconfig

# espforge at the pinned tag: its components, board and emulator part
ESPFORGE ?= ../../.espforge
ifeq ($(wildcard $(ESPFORGE)/web/emu/forge/emu.mk),)
$(error no espforge at $(ESPFORGE): python3 tools/fetch_forge.py (or ESPFORGE=<espforge checkout>))
endif
FORGE_EMU := $(ESPFORGE)/web/emu/forge
COMP := $(ESPFORGE)/components
BOARD := $(ESPFORGE)/boards/ws_amoled175/board/include

# main/'s sources, built unchanged (main/CMakeLists.txt SRCS)
APP_SRC := main.c ui.c app_text.c rtc_api.c favs.c departures.c map.c geo.c
# This app's own emulator file: main(), the two demo stops, a stop from the page's address
APP_EMU := emu_main.c
# What main/CMakeLists.txt embeds that the app reads: the font, as ttf_start / ttf_end (ui.c)
EMBED := ttf=../../main/montserrat.ttf
# More of the framework than the starter: the map's tiles (png_rows, with miniz's tinfl)
FORGE_CORE += png_rows.c

include $(FORGE_EMU)/emu.mk
