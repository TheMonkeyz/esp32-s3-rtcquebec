#!/bin/bash
# Compile one file of the firmware for the browser and show the first errors (development helper, WSL)
source ~/emsdk/emsdk_env.sh >/dev/null 2>&1
cd "$(dirname "$0")"
L=${LVGL:-../../managed_components/lvgl__lvgl}
C=../../components
IDF=${IDF_PATH:-/mnt/c/Espressif/esp-idf}
emcc -c -O0 -DEMU_BUILD -DEMU_VERSION='"dev"' -Dlocaltime_r=emu_localtime_r -I. -Ishim -I../../main \
  -I$C/forge_core/include -I$C/forge_lvgl/include -I$C/forge_net/include -I$C/forge_ota/include \
  -I../../boards/ws_amoled175/board/include -I$L -I$L/src -I$IDF/components/json/cJSON \
  -include string.h -include stdint.h \
  '-DLV_CONF_KCONFIG_EXTERNAL_INCLUDE="lv_kconfig.h"' -DLV_LVGL_H_INCLUDE_SIMPLE \
  "$@" -o /tmp/try.o 2>&1 | grep -E "error|fatal" | head -${N:-12}
