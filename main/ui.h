#pragma once
#include <stdbool.h>
#include "ota.h"

// The screens: one pager holding "system" (page 0) and a page per favourite stop ("stop" is the first: swipe left for
// the next ones), "setup" (Wi-Fi setup, long-press anywhere) and "message" (start-up messages). Any task may call
// these: they take the display lock.
void ui_init(void);
void ui_home(void);                                 // the pager, on the first stop
void ui_favs_changed(void);                         // the favourites list changed: pages shown, texts (deps_set_favs first)
void ui_deps_changed(int i);                        // favourite i has new departures (departures.c's task)
void ui_message(const char *title, const char *body);
void ui_texts_changed(void);                        // the language changed: every label again

// Wi-Fi setup: page 1 = the setup network (QR to join it), page 2 = Easy Connect (Android). note: shown at the top
// (NULL: "Tap to cancel" / "Tap to try again"). First-time setup can't be closed.
void ui_wifi_setup(const char *note);
bool ui_wifi_setup_open(void);
bool ui_wifi_setup_close(void);                     // as a tap would, unless a phone is on the setup network
void ui_wifi_setup_end(void);                       // the saved network is back: stop Easy Connect

void ui_ota(const ota_status_t *st);                // forge_ota listener (OTA task)
