# Changelog

All notable changes to GALLERY are recorded here.

Follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/)

## [Unreleased]

### Added

- A **STEREO** or **MONO** badge beside the loaded response's name. A slot loaded from
  one side of a stereo file convolves in mono, and once the file was chosen nothing on
  the strip said which of the two you had.
- Closing the theme editor with colours you have not saved now asks, offering **Save**,
  **Discard** or **Cancel**. Every way out goes through it — the Close button, the escape
  key and the title bar's own close button.
- **Loading a stereo response asks which side to use.** A two-channel cabinet capture is
  usually two microphone positions rather than a stereo image, so convolving both put a
  different cabinet in each ear with nothing to say a choice had been made. The answer is
  kept in the session beside the path, so reopening one sounds the way you left it.
- `tests/ThemeReachTests.cpp`, which renders the whole editor, moves every colour the
  theme has, renders it again, and fails if anything the design ships is still on screen.
  It found four real bugs the day it was first run across all four plugins.
- **A theme is now this plugin's own**, in `<name>.celthm` under the company folder
  rather than one file shared by the house. Every instance of it on the machine wears
  the same colours whatever host or format it is loaded as, and an existing shared theme
  is inherited on first run so nothing is lost by the split. Themes stay cross-
  compatible: one exported from another Céline plugin still loads, and the colours this
  one does not have are simply skipped.
### Fixed

- **The caret in a value box was invisible on the light strip.** It was the last thing
  in the box still taking its colour from the look and feel, which sets it for the dark
  half of the design -- so on the near-white panel the text cursor was white on white.
  It takes the row's own ink now, like the text and the selection around it.
- **The value text on the light strip turned white while you edited it.** Not the text
  colour, as it looked: opening the editor selects the whole value, so what you see the
  instant you click in is `highlightedTextColourId` -- which the look and feel sets for
  the dark half of the design, because it has no way of knowing a particular row stands
  on the other one. The selection now takes the row's own ink, with a wash of that ink
  behind it, so it reads on either side of the split.
- **The header came up empty until the window was resized.** `setSize` fires `resized()`,
  which measures the logo and the wordmark to lay the header out — and those had moved
  into `applyColours`, which ran afterwards. So the first layout saw no artwork, placed
  nothing, and the header stayed blank until something else resized the window. `setSize`
  goes last in the constructor again, which is the house rule and exactly this reason.
- The house mark and the plugin's wordmark are the same size in every plugin. GALLERY
  drew them at 26 and 18 pixels where AURA and SPACE used 20 and 14, on a header band
  that is the same height in all three.
- **Clicking into a value box no longer draws a border round it.** The slider's text box
  asked for one in the armed colour while it was being edited -- the last rule left
  anywhere in the window, and one that appeared on a click, which is exactly what made it
  read as a system control dropped into the design.
- The digits you type into a value box are the theme's ink. JUCE fills
  `textWhenEditingColourId` from its own colour scheme rather than leaving it unset, so
  the text being edited was never taking its colour from the theme.
- **Discarding a theme now puts the colours back.** It marked the change abandoned and
  left it on screen, so "discard" only meant "do not write the file" -- the window behind
  it kept the colours you had just rejected until something else reloaded the theme.
- **The theme window no longer opens behind the plugin.** Building it by hand to
  intercept every way of closing it lost the two things `DialogWindow::LaunchOptions`
  does for you: it is on top when the host keeps its own windows on top -- Ableton does,
  and the window was unreachable without closing the plugin -- and it is told the scale
  the editor is being shown at.
- **The blend pad no longer stalls while you drag it.** Every mouse-move recomputed the
  whole spectrum — four responses summed and transformed — which macOS hid by coalescing
  moves and Windows did not, so the dot only caught up when the pointer stopped, and only
  under the Spectrum view. The poll that redraws the graph picks the change up within a
  frame anyway.
- The plugin name no longer sits behind its own wordmark. Loading the artwork moved into
  `applyColours` and the fallback text's visibility was left behind in the constructor,
  where the wordmark had not been read yet.
- **The yellow ring around whatever you were editing is gone.** JUCE draws a focus
  outline as a separate desktop window, and its default is a rounded rectangle at a fixed
  radius of three — so on a field rounded to the house radius it traced a shape the
  control does not have, sitting slightly off its corners. It also lived only as long as
  that window did, which is why it appeared on one launch and not the next. Nothing here
  needs it: a field being edited says so with its caret and its selection.
- **The theme editor's own Close button follows the accent it is showing you.** Its
  colours were set once when the window opened, so picking a new accent recoloured every
  other control in the plugin and left the button next to the swatch on the old one. The
  window's title, subtitle and status line had the same fault.
- The library's two icon buttons took their accent at construction, which no theme change
  could reach.
- **The toolbar's mark did not follow the theme.** The logo and the wordmark were tinted
  once when the window opened, and tinting is destructive — so they stayed on whatever
  colour the theme happened to be at that moment.
- **The About window did not follow the theme at all.** It is a window of its own, so the
  editor's `sendLookAndFeelChange` never reached it; it now listens to the palette
  directly, and its marks are re-read from the binary rather than re-tinted.
- **Group headings no longer escape the colour list.** They are painted by the panel in
  the scrolled list's coordinates, and nothing clipped them — so a heading scrolled past
  the top carried on being drawn above the list, over the subtitle and the footer. It
  showed up as headings appearing in the middle of the window whenever something made
  the panel repaint underneath the colour picker.
- The colour picker no longer paints a square panel inside a rounded bubble. It filled
  its own background, which met the bubble's rounded corners and lost the argument.
- **Theming one instance now reaches the others.** Each plugin format is a separately
  loaded module with its own copy of everything static, so the VST3 and the AU open in
  one session were two palettes that never met — theming one left the other on the old
  colours until it was reloaded. A window reads the saved theme when it opens, which is
  the moment it can matter; nothing watches the disk in the background. Colours you are
  in the middle of choosing are never overwritten by what another instance saved.
- Building a palette no longer schedules a save of the file it has just read. Reading
  any colour builds it, and the first read can come from a static initialiser — before
  there is a message loop for the save to wait on, which JUCE asserts about.
- **A theme you pick is kept — when you press Save.** It used to be live until you
  closed the plugin and then gone, because nothing wrote it. There is now a **Save**
  button in the theme editor, lit only while there is something to keep, and the status
  line says whether there is. Editing itself touches nothing: a colour picker sends a
  change per mouse move, and a preference is not worth a file per mouse move.
- Text fields no longer draw a ring when you click into them. The caret already says
  where the typing goes, and it was the one edge in the window that arrived on a click.

- **Button backgrounds and text fields are separate colours in the theme.** They shipped
  as one — every button wore the same slate as every panel — so a theme could not lift
  the controls off the surfaces they sit on. Two new roles, **Button** and **Text
  field**, ship at exactly the values they replace, so nothing looks different until
  somebody moves them.
- **A theming engine.** Every colour the interface draws with is editable at runtime,
  from **Theme…** in the settings menu, and can be written to and read from a `.celthm`
  file to be kept or shared. Changes show at once — the palette is what everything draws
  from, so there is no Apply to forget.
- The theme file is shared by every Céline plugin: one `theme.celthm` under the company
  folder, so theming one of them themes all of them. A key a build does not know is
  ignored and a key it knows but the file omits keeps its shipped value, which is what
  lets one file serve three plugins with different palettes.
- Plugin-specific colours are in the theme too, not just the chrome — the four cabinet
  colours, the solo, mute and polarity pills, the tab bar, the blend handle and the top
  resolution tier. A palette that could not reach them could not re-skin the plugin.

### Changed

- The look and feel is split: `ui/LookAndFeelBase` carries everything the four plugins
  draw the same way, and `ui/PluginLookAndFeel` is a subclass for what this one does
  differently. Fifteen files under `source/ui/` are now byte-identical across all four,
  which is what makes the shared kit a move rather than a merge — see `CELINEUI.md`.
- Formats : **Fx|Filter** to VST3, **lv2:FilterPlugin** to LV2, **filter** to
  CLAP, and **Harmonic** to AAX.
- The CLAP build declares that it handles mono/stereo.
- `Theme`'s accessors are lookups rather than constants. The shipped values, the editor
  labels and the file keys are generated from one list (`ui/ThemeRoles.h`), so the enum,
  the table, the `.celthm` format and the editor's rows cannot drift apart.
- Every control that took its colours once in a constructor now gathers them into an
  `applyColours()` called from `lookAndFeelChanged()` as well, so a theme change reaches
  them. A snapshot does not follow a theme, and the failure is silent: half the window
  in the new colours and half in the old.
- `textDisabled` and `tabInactive` are roles of their own rather than aliases of
  `comment` and `chrome`. Shipping at the same value is not the same as being one
  colour, and a theme has to be able to pull them apart.

--

## [1.0.0] — 2026-09-01

First release.

[Unreleased]: https://github.com/Celine-audio/GALLERY/compare/v1.0.0...HEAD
[1.0.0]: https://github.com/Celine-audio/GALLERY/releases/tag/v1.0.0
