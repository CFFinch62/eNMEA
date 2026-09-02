#pragma once

// The product's own name, used wherever the firmware identifies itself: the
// setup page's title and heading, the access point it hosts, and the serial log
// prefix.
//
// It lives in a macro because eNMEA and eAIS share the settings portal and the
// network layer byte-for-byte. A device flashed with eAIS that calls itself
// eNMEA on its own setup page is confusing in exactly the situation where the
// user is least sure what they are looking at - but forking those files to fix
// the wording would mean maintaining two copies of code carrying
// hardware-found fixes. Set it per project in platformio.ini:
//
//     -DSETUP_PRODUCT_NAME='"eAIS"'
//
// String literal, so it concatenates at compile time and the page stays a
// constexpr with no runtime formatting.
#ifndef SETUP_PRODUCT_NAME
#define SETUP_PRODUCT_NAME "eNMEA"
#endif

// Log prefix, e.g. "[eNMEA] ". Written as a macro rather than assembled at
// runtime so format strings stay literals.
#define LOG_TAG "[" SETUP_PRODUCT_NAME "] "
