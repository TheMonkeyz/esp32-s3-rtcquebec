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

// The map screen (a tap on a stop page)
X(T_MAP_NEXT,       "Next bus: %s",                "Prochain bus : %s")
X(T_MAP_LOADING,    "Loading the map...",          "Chargement de la carte...")
X(T_MAP_NO_TILES,   "Can't load the map",          "Impossible de charger la carte")
X(T_MAP_NO_BUS,     "No bus on the way now",       "Aucun bus en route")

// The alerts page (RTC's notices for the favourite routes; their texts are RTC's, in French)
X(T_ALERTS,         "Alerts",                      "Avis")
X(T_ALERTS_NONE,    "No alerts for your routes",   "Aucun avis pour vos parcours")
X(T_ALERT_BEGIN,    "Start: %s",                   "Début : %s")
X(T_ALERT_END,      "End: %s",                     "Fin : %s")
X(T_NO_STOPS,       "No stops yet",                "Aucun arrêt")
X(T_NO_STOPS_HOW,   "Add your stops on the\nsettings page: swipe right,\nthen scan the code",
                    "Ajoutez vos arrêts dans\nles réglages : glissez à droite,\npuis balayez le code")
X(T_SYSTEM,         "System",                      "Système")
X(T_SYS_VERSION,    "Version %s",                  "Version %s")
X(T_SYS_WIFI,       "Wi-Fi %s  ·  %d dBm",         "Wi-Fi %s  ·  %d dBm")
X(T_SYS_OFFLINE,    "Wi-Fi offline",               "Wi-Fi hors ligne")
X(T_SYS_IP,         "Address %s",                  "Adresse %s")
X(T_SYS_MEMORY,     "Memory %u KB  ·  PSRAM %u KB","Mémoire %u Ko  ·  PSRAM %u Ko")
X(T_SYS_UPTIME,     "Up %s",                       "En marche depuis %s")
X(T_SYS_SCAN,       "Scan for the settings page",  "Balayez pour les réglages")

// Updates (forge_ota states and reasons)
X(T_UPD_IDLE,       "Updates: not checked yet",    "Mises à jour : pas encore vérifiées")
X(T_UPD_CHECKING,   "Checking for updates...",     "Vérification des mises à jour...")
X(T_UPD_UP_TO_DATE, "Up to date",                  "À jour")
X(T_UPD_AVAILABLE,  "Update %s available",         "Mise à jour %s offerte")
X(T_UPD_DOWNLOADING,"Updating... %d %%",           "Mise à jour... %d %%")
X(T_UPD_DONE,       "Updated: restarting",         "Mise à jour faite : redémarrage")
X(T_UPD_FAILED,     "Update failed: %s",           "Échec de la mise à jour : %s")
X(T_OTA_NO_SITE,    "Can't reach the update site", "Site des mises à jour injoignable")
X(T_OTA_BAD_SITE,   "Unexpected reply from the update site", "Réponse inattendue du site des mises à jour")
X(T_OTA_NO_IMAGE,   "No app image in the manifest","Aucune image dans le manifeste")
X(T_OTA_NO_START,   "Download failed to start",    "Le téléchargement n'a pas démarré")
X(T_OTA_WRONG,      "Wrong firmware image",        "Mauvaise image de micrologiciel")
X(T_OTA_INTERRUPTED,"Download interrupted",        "Téléchargement interrompu")
X(T_OTA_INVALID,    "Downloaded image is invalid", "Image téléchargée invalide")
X(T_OTA_ROLLED_BACK,"%s was undone: the device restarted before it was confirmed",
                    "%s annulée : l'appareil a redémarré avant de la confirmer")

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
