// Display texts: one line per text, one column per language (forge_i18n.h: English, French). Included several
// times with different X() definitions (app_text.h, app_text.c, tests/host/test_i18n.c): no include guard.
// French is Canadian French (Québec), standard written: "appuyez longuement", "balayez le code QR", "1er".
// Every text needs every language: an empty "" shows English (tests/host/test_i18n.c checks), and printf
// conversions (%s, %d) must be the same, in the same order. Check fit with snapshots: French is longer and the
// round screen wraps it badly; give long lines explicit \n breaks.

// Screens
// A stop's departures (one page per favourite). Minutes count down from the departure times (ui.c).
X(T_DEP_MIN,        "%d min",                      "%d min")
X(T_DEP_NOW,        "< 1 min",                     "< 1 min")
X(T_DEP_LIVE,       "Real time",                   "Temps réel")
X(T_DEP_SCHED,      "Scheduled",                   "Horaire prévu")
X(T_DEP_CANCELLED,  "Cancelled",                   "Annulé")
X(T_DEP_NONE,       "No more departures today",    "Plus de départs aujourd'hui")
X(T_DEP_LOADING,    "Loading...",                  "Chargement...")
X(T_DEP_UPDATED,    "Updated at %s",               "Mis à jour à %s")
X(T_DEP_OFFLINE,    "Can't reach the RTC",         "Impossible de joindre le RTC")
X(T_DEP_NOT_FOUND,  "Route %s doesn't stop here\nin this direction",
                    "Le parcours %s ne s'arrête pas ici\ndans cette direction")
X(T_DEP_NOT_SERVED, "Stop not served for now",     "Arrêt non desservi pour le moment")
X(T_DEP_DROP_OFF,   "Drop-off only",               "Descente seulement")
X(T_STOP_ALERT1,    "1 alert for this route",      "1 avis pour ce parcours")
X(T_STOP_ALERTS,    "%d alerts for this route",    "%d avis pour ce parcours")

// The map (swipe left from a stop)
X(T_MAP_NEXT,       "Next bus: %s",                "Prochain bus : %s")
X(T_MAP_LOADING,    "Loading the map...",          "Chargement de la carte...")
X(T_MAP_NO_TILES,   "Can't load the map",          "Impossible de charger la carte")
X(T_MAP_NO_BUS,     "No bus on the way now",       "Aucun bus en route")

// The alerts of a stop (swipe right from it): RTC's notices for its route in its direction (their texts are RTC's,
// in French)
X(T_ALERTS_ROUTE,   "Alerts: %s",                  "Avis : %s")
X(T_ALERTS_NONE,    "No alerts for this route",    "Aucun avis pour ce parcours")
X(T_ALERT_BEGIN,    "Start: %s",                   "Début : %s")
X(T_ALERT_END,      "End: %s",                     "Fin : %s")
X(T_NO_STOPS,       "No stops yet",                "Aucun arrêt")
X(T_NO_STOPS_HOW,   "Add your stops from your\nphone: touch and hold,\nthen Stops on phone",
                    "Ajoutez vos arrêts depuis\nle téléphone : appuyez\nlonguement, puis\nArrêts (téléphone)")

// Settings screen (a long press; forge_settings: the app's word for each settings_text_t code, ui.c set_texts;
// weather_amoled's French). Sections in capitals; a row holds its name and its value in 300 px.
X(T_SET_DONE,       "Done",                        "OK")
X(T_SET_SEC_SCREEN, "SCREEN",                      "ÉCRAN")
X(T_SET_DIM_QUIET,  "Dim when quiet",              "Tamiser si calme")
X(T_SET_WAKE_PICKUP,"Wake on pick-up",             "Réveil si soulevé")
X(T_SET_TIMING,     "Timing",                      "Délais")
X(T_SET_SHORT,      "Short",                       "Courts")     // agrees with "Délais" (plural)
X(T_SET_NORMAL,     "Normal",                      "Normaux")
X(T_SET_LONG,       "Long",                        "Longs")
X(T_SET_CUSTOM,     "Custom",                      "Autres")
X(T_SET_BRIGHTNESS, "Brightness %d%%",             "Luminosité %d %%")
X(T_SET_LANGUAGE,   "Language",                    "Langue")
X(T_SET_SEC_MORE,   "MORE",                        "PLUS")
X(T_SET_PHONE,      "Stops on phone",              "Arrêts (téléphone)")   // T_NO_STOPS_HOW names it
X(T_SET_PHONE_SCAN, "Scan with your phone's\ncamera to choose your\nstops and settings",
                    "Balayez avec l'appareil\nphoto du téléphone pour\nchoisir arrêts et réglages")
X(T_SET_PHONE_NONE, "Connect to Wi-Fi first",      "Connectez d'abord le Wi-Fi")
X(T_SET_WIFI,       "Wi-Fi network",               "Réseau Wi-Fi")
X(T_SET_UPDATES,    "Updates",                     "Mises à jour")
X(T_SET_CHECK_NOW,  "Check now",                   "Vérifier")
X(T_SET_CHECKING,   "Checking...",                 "Vérification...")
X(T_SET_UP_TO_DATE, "Up to date",                  "À jour")
X(T_SET_FAILED,     "Failed",                      "Échec")
X(T_SET_INSTALL,    "Install %s",                  "Installer %s")
X(T_SET_TAP_AGAIN,  "Tap again",                   "Touchez encore")
X(T_SET_RESTART,    "Restart",                     "Redémarrer")
X(T_SET_RESTARTING, "Restarting...",               "Redémarrage...")
X(T_SET_SEC_ABOUT,  "ABOUT",                       "À PROPOS")
X(T_SET_VERSION,    "Version",                     "Version")
X(T_SET_NETWORK,    "Network",                     "Réseau")
X(T_SET_OFFLINE,    "Offline",                     "Hors ligne")
X(T_SET_IP,         "IP address",                  "Adresse IP")
X(T_SET_MEMORY,     "Memory",                      "Mémoire")
X(T_SET_MEMORY_KB,  "%u / %u KB",                  "%u / %u Ko")
X(T_SET_UPTIME,     "Running for",                 "En marche depuis")
X(T_SET_UPTIME_MIN, "%d min",                      "%d min")
X(T_SET_UPTIME_H,   "%d h %02d",                   "%d h %02d")
X(T_SET_UPTIME_D,   "%d d %d h",                   "%d j %d h")

// Wi-Fi setup screen
X(T_WIFI_SETUP,     "Wi-Fi setup",                 "Configuration Wi-Fi")
X(T_WIFI_JOIN,      "Scan to join %s\n(password %s).\nAndroid? Swipe left to skip\nthe password",
                    "Balayez pour joindre %s\n(mot de passe %s).\nAndroid? Glissez à gauche\npour éviter le mot de passe")
X(T_WIFI_DPP_TITLE, "Easy Connect",                "Easy Connect")      // short: the circle is ~260 px wide up there
X(T_WIFI_DPP_HOW,   "Android phone on your Wi-Fi?\nScan this (camera or any QR app):\nit sends its network.\nSwipe right for other phones",
                    "Téléphone Android sur votre Wi-Fi?\nBalayez ce code (caméra, appli QR) :\nil envoie son réseau.\nGlissez à droite : autres téléphones")
X(T_WIFI_DPP_NONE,  "Easy Connect isn't available.\nSwipe right for other phones.",
                    "Easy Connect n'est pas offert.\nGlissez à droite : autres téléphones.")
X(T_WIFI_RECEIVED,  "Wi-Fi received",              "Wi-Fi reçu")
X(T_WIFI_GOT,       "Got \"%s\" from your phone.\nRestarting...", "« %s » reçu du téléphone.\nRedémarrage...")
X(T_WIFI_DPP_FAIL,  "That didn't work. Swipe right\nand join the setup network.",
                    "Échec. Glissez à droite et\njoignez le réseau de configuration.")
X(T_TAP_RETRY,      "Tap to try again",            "Touchez pour réessayer")
X(T_TAP_CANCEL,     "Tap to cancel",               "Touchez pour annuler")

// Start-up messages (main.c)
X(T_STARTING,       "Starting...",                 "Démarrage...")
X(T_CONNECTING,     "Connecting to\n%s\n\nLong-press for Wi-Fi setup", "Connexion à\n%s\n\nAppuyez longuement pour le Wi-Fi")
X(T_CONNECTED,      "Connected",                   "Connecté")
X(T_CANT_REACH,     "Can't reach %s\nTap to try again", "%s injoignable\nTouchez pour réessayer")
X(T_STILL_TRYING,   "Can't reach %s\nStill trying\n\nLong-press for Wi-Fi setup",
                    "%s injoignable\nNouvel essai en cours\n\nAppuyez longuement pour le Wi-Fi")
X(T_FIRST_SETUP,    "First-time setup",            "Première configuration")
