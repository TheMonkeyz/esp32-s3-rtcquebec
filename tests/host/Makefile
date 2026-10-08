# Host unit tests: firmware C files built with gcc against small shims of the ESP-IDF headers (shim/) and a scripted
# HTTP client (fake.c), with AddressSanitizer. Linux or WSL:   make -C tests/host
# IDF_PATH: an ESP-IDF checkout (cJSON, for tests of JSON parsers); on the Windows PC through WSL it is
# /mnt/c/Espressif/esp-idf (test_rtc_api needs it).
IDF_PATH ?= /mnt/c/Espressif/esp-idf
CJSON := $(IDF_PATH)/components/json/cJSON
# espforge's components at the tag in main/idf_component.yml (managed_components after an idf.py build), else
# tools/fetch_forge.py's .espforge (as weather_amoled). Their own tests (utf8, png_rows, textfit, testcon, presence...)
# run in espforge's CI at that tag; here: this app's files, and the i18n core with this app's texts.
FORGE ?= $(if $(wildcard ../../managed_components/forge_core/CMakeLists.txt),../../managed_components,../../.espforge/components)
ifeq ($(wildcard $(FORGE)/forge_core/i18n.c),)
$(error no espforge components at $(FORGE): build the firmware once, or python tools/fetch_forge.py, or FORGE=<espforge>/components)
endif
CORE := $(FORGE)/forge_core
MAIN := ../../main
OUT := build
CFLAGS := -std=gnu11 -D_DEFAULT_SOURCE -g -O1 -Wall -Wno-unused-function -fsanitize=address,undefined \
          -fno-sanitize-recover=undefined -Ishim -I$(CORE)/include -I$(MAIN) -include shim/host.h
TESTS := i18n rtc_api

all: $(TESTS:%=$(OUT)/test_%)
	@set -e; for t in $(TESTS); do ./$(OUT)/test_$$t; done

# i18n.c is pure C (its NVS half, i18n_nvs.c, is not compiled here); the texts are the app's
$(OUT)/test_i18n: test_i18n.c $(CORE)/i18n.c $(CORE)/include/forge_i18n.h $(MAIN)/i18n_strings.h | $(OUT)
	$(CC) $(CFLAGS) -o $@ test_i18n.c $(CORE)/i18n.c
# rtc_api.c parses RTC's replies with cJSON (ESP-IDF's copy); the replies are in data/
$(OUT)/test_rtc_api: test_rtc_api.c $(MAIN)/rtc_api.c $(MAIN)/rtc_api.h | $(OUT)
	$(CC) $(CFLAGS) -I$(CJSON) -o $@ test_rtc_api.c $(MAIN)/rtc_api.c $(CJSON)/cJSON.c -lm
$(OUT):
	mkdir -p $(OUT)
clean:
	rm -rf $(OUT)
.PHONY: all clean
