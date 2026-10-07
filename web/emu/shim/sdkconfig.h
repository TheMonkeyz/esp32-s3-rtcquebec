#pragma once
// Browser emulator: the Kconfig values the firmware and the shared headers read (net.h, ota.h, svc.c, main.c), as
// sdkconfig.defaults / main/Kconfig.projbuild set them
#define CONFIG_FORGE_SETUP_SSID "RTC-Setup"
#define CONFIG_FORGE_OTA_SITE "https://themonkeyz.github.io/esp32-s3-rtcquebec/"
#define CONFIG_FORGE_REPO "TheMonkeyz/esp32-s3-rtcquebec"
#define CONFIG_FORGE_PRODUCT ""
#define CONFIG_FORGE_UA_COMMENT ""
#define CONFIG_APP_TZ "EST5EDT,M3.2.0,M11.1.0"
