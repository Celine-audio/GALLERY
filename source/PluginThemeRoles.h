#pragma once

/*
    GALLERY's own themeable colours, added to the house list in `ui/ThemeRoles.h`.

    A plugin declares here whatever the shared kit has no name for. The four cabinet
    colours are the clearest case: telling four responses apart is the whole job of this
    window, so they are as much a part of the theme as the chrome is -- and somebody who
    themes the plugin and cannot reach them has not themed it.

    See ui/ThemeRoles.h for the shape of an entry and for the warning about renaming.
*/
#define CELINE_PLUGIN_THEME_ROLES(X)                                                    \
    X (irTeal,      "Cabinet 1",          "Cabinets",   0xff3ecfc0)                     \
    X (irRed,       "Cabinet 2",          "Cabinets",   0xffe25a5a)                     \
    X (irPurple,    "Cabinet 3",          "Cabinets",   0xff6f7ceb)                     \
    X (irGold,      "Cabinet 4",          "Cabinets",   0xffe0b64a)                     \
                                                                                        \
    X (solo,        "Solo",               "Slot state", 0xffcdc292)                     \
    X (mute,        "Mute",               "Slot state", 0xffcd9292)                     \
    X (phase,       "Polarity",           "Slot state", 0xff6c8b77)                     \
    X (onPill,      "Ink on a lit pill",  "Slot state", 0xff17151a)                     \
                                                                                        \
    X (tabActive,   "Tab in front",       "Panels",     0xff312441)                     \
    X (tabInactive, "Tab behind",         "Panels",     0xff3b334b)                     \
    X (discard,     "Discard",            "Panels",     0xff6d2d2e)                     \
    X (blendHandle, "Blend handle",       "Panels",     0xff6a449a)                     \
    X (ultra,       "Top resolution",     "Panels",     0xff86c99a)
