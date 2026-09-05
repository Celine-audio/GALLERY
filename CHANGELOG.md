# Changelog

All notable changes to GALLERY are recorded here.

Follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/)

## [Unreleased]

### Added

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
