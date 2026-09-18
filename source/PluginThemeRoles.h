#pragma once

/*
    GALLERY's own themeable colours, added to the house list in `ui/ThemeRoles.h`.

    A plugin declares here whatever the shared kit has no name for. The four cabinet
    colours are the clearest case: telling four responses apart is the whole job of this
    window, so they are as much a part of the theme as the chrome is -- and somebody who
    themes the plugin and cannot reach them has not themed it.

    The two curves below them are the same argument arriving late. Both ship in the
    interface's own ink -- the blend in text(), the output in comment() -- and that is
    deliberate: neither is a cabinet, and a fifth and sixth hue would put them in the set
    of four they are drawn against. But shipping *at* a colour is not the same as *being*
    that colour, and while they were literal calls to text() and comment() there was no
    way to restyle the labels in this window without the graph moving with them.

    See ui/ThemeRoles.h for the shape of an entry and for the warning about renaming.
*/
#define CELINE_PLUGIN_THEME_ROLES(X)                                                    \
    X (irTeal,      "Cabinet 1",           "Cabinets",   0xff3ecfc0)                    \
    X (irRed,       "Cabinet 2",           "Cabinets",   0xffe25a5a)                    \
    X (irPurple,    "Cabinet 3",           "Cabinets",   0xff6f7ceb)                    \
    X (irGold,      "Cabinet 4",           "Cabinets",   0xffe0b64a)                    \
                                                                                        \
    X (blendCurve,  "Blend of the four",   "Curves",     0xfff9fbff)                    \
    X (outputCurve, "What comes out",      "Curves",     0xff888791)                    \
                                                                                        \
    X (stripButton, "Button on a strip",   "Strips",     0xff37364a)                    \
                                                                                        \
    X (solo,        "Solo",                "Slot state", 0xffcdc292)                    \
    X (mute,        "Mute",                "Slot state", 0xffcd9292)                    \
    X (phase,       "Polarity",            "Slot state", 0xff6c8b77)                    \
    X (onPill,      "Ink on a lit pill",   "Slot state", 0xff17151a)                    \
                                                                                        \
    X (tabActive,   "Tab in front",        "Panel work", 0xff312441)                    \
    X (tabInactive, "Tab behind",          "Panel work", 0xff3b334b)                    \
    X (discard,     "Discard",             "Panel work", 0xff6d2d2e)                    \
    X (blendHandle, "Blend handle",        "Panel work", 0xff6a449a)                    \
    X (ultra,       "Top resolution",      "Panel work", 0xff86c99a)
