# CLAUDE.md

Guidance for Claude Code working in this repository.

## What this is

**GALLERY** — a four-slot guitar cabinet impulse-response loader, built on Céline
Audio's house plugin template (derived from [Pamplejuce](https://github.com/sudara/pamplejuce)),
carrying the house look, About window, licensing and CI.

Four cabinets convolve the input in parallel and are summed. Each slot has a file, a
solo/mute/polarity trio, an alignment delay, pan, and a low/high cut whose slope is
selectable. Their relative levels are not per-slot controls at all — one
XY pad blends the four, a cabinet at each corner. Above the strips, one graph draws all four at
once — as frequency responses or as waveforms — each in its own colour, which is what
makes the four comparable and what makes alignment possible at all.

The layout came from a Figma export, which is no longer in the tree. Render the window
and look at it — see "Verifying UI work", which is the only check there is now.

SPACE and AURA are two sibling plugins on the same kit. Their source is not vendored
here either, so where the code cites one it is stating a house decision, not pointing at
something to go and read.

**Where the Figma and the house disagree, the house wins.** The toolbar is the kit's
45px band rather than the mockup's 84, the front tab is the accent at a fifth strength
rather than the mockup's brown, the horizontal sliders are AURA's dimensions, and the
output trim sits in AURA's column on the right. These plugins are used one after the
other on the same signal; a control that is the same control should not be in a
different place, or a different size, in each.

**With nothing loaded the plugin passes audio through untouched.** It replaces the
signal with what comes out of the cabinets, so an empty set of them is arithmetically
silence — but a plugin that mutes a track the moment it is inserted reads as broken,
and gets removed before anybody finds the Load button.

## Build commands

CLion's default directories are used so CLI and IDE builds share one cache.

```bash
cmake -B cmake-build-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug
```

```bash
cmake --build cmake-build-debug
```

```bash
./cmake-build-debug/Tests
```

```bash
./cmake-build-release/Benchmarks
```

Release builds go in `cmake-build-release`. `ctest --test-dir cmake-build-debug`
runs tests and benchmarks together.

## Where code goes

| | |
|---|---|
| One slot's audio | `source/dsp/IrSlot` — owns the file, the convolver, the cuts and the gains |
| Summing the four | `PluginProcessor::processBlock` |
| Parameters | `source/Parameters.h` (ID) and `source/Parameters.cpp` (declaration) — **both**, they are separate lists |
| A strip's controls | `source/ui/IrStripControl` |
| The blend between cabinets | `source/ui/BlendPad` and `Parameters::blendWeights` |
| The graph | `source/ui/AnalyserGraph` and the two displays under it |
| What the graph draws | `source/ui/AnalyserFeed` — not the editor, which is layout and wiring |
| This plugin's own colours | `source/PluginThemeRoles.h` (the roles) and `source/PluginTheme.h` (the accessors) |
| Product facts | `source/ProductInfo.h` — tagline, repo URL, copyright |
| Anything the person chose, not the session | `source/Settings` — one file per machine, shared by every instance |

## Structure

- `source/` — processor, editor, parameters, product facts
- `source/dsp/` — `IrSlot` (a slot end to end), `PartitionedConvolver` (zero-latency,
  hot-swappable), `ImpulseResponse` (the file), `CutFilter`,
  `SpectrumAnalyzer` (the live output trace), `LogSpectrum` (the display's frequency
  grid, and how a transform is read onto it), `MixSpectrum` (the four blended into one
  curve), `Lagrange` (reading a signal between two of its samples)

  There was a `SpectralSmoother` here, applying a minimum-phase correction to flatten a
  capture's fine structure. It was removed as not earning its place; it is in the
  history if it is ever wanted back.
- `source/ui/` — the house kit (`Theme.h`, `Fonts`, `PluginLookAndFeel`, `AboutPanel`,
  `ParameterControl`, `IconButton`, `EmbeddedAssets`, and the theming engine —
  `Theme.h`, `ThemeRoles.h`, `ThemePalette`, `ThemePanel`) plus GALLERY's own:
  `IrStripControl`, `BlendPad`, `AnalyserGraph`, `MultiSpectrumDisplay`,
  `MultiWaveformDisplay`, `CutRangeSlider`, `TabHeader`, `LetterToggleButton`,
  `LibraryPanel`, `PlotGeometry`, and `AnalyserFeed` — which is not a component at all
  but everything the graph draws, gathered and handed to it
- `tests/` — Catch2. `tests/dsp/` for the engine, `tests/processor/` for the plugin as
  a host sees it (one file per property it has to hold, over the cabinet fixture in
  `tests/helpers/`); `benchmarks/` — Catch2 benchmarks
- `assets/` — embedded as BinaryData by `cmake/Assets.cmake`, everything in the folder
- `JUCE/`, `cmake/`, `modules/clap-juce-extensions` — submodules

`SharedCode` is an INTERFACE library linking the source into both the plugin and the
test targets, which is what keeps them from violating the ODR.

## How a slot works

The split to hold on to is that **the response is built on the message thread and
never touched by the audio thread.** What the audio thread runs is a convolution, two
cut filters, a delay line and two ramped gains, and nothing it touches is ever resized
underneath it. Only the resolution setting rebuilds a response; everything else on a
strip is read per block and costs nothing.

**Alignment is a delay line, not a rebuild.** It used to be a windowed-sinc shift baked
into the response, which meant a rebuild every time the knob moved — and the engine
walks between two filters over about 40 ms, so rebuilds any faster than that arrive
mid-walk and cut it short. Measured at a 45 ms throttle the worst break in the waveform
went up 140×; at the 150 ms it needed to be safe, the control moved in six steps a
second, which is what "not smooth" was. A delay is continuous by construction, so that
is what it is now: `juce::dsp::DelayLine` with third-order Lagrange interpolation, moved
a sample at a time.

Two things about that line worth knowing before changing it. **Every push is paired
with a pop** — JUCE's line keeps a read position of its own and only advances it on the
pop, so a block that pushed without popping would leave the two walking apart and the
delay would become whatever the drift had reached. And **no headroom is needed under
the interpolator**: JUCE writes to decreasing indices, so the four samples a third-order
Lagrange reads are the newest and three older ones, and at a delay of exactly nothing
the fractional part is zero and the newest sample comes back untouched. A slot with the
control at rest passes through bit for bit, and a test says so.

Two consequences worth knowing before changing anything here:

- **Cuts are *not* baked into the response**, unlike alignment. A steep cut rings for
  far longer than a cabinet capture lasts, so folding one in would truncate its own
  tail and quietly get the bottom octave wrong. They run live.
- **`rebuildLock` guards every rebuild and every read of what one writes.** The audio
  thread never takes it. It is there because the message thread and whichever thread
  the host calls `prepareToPlay` on can otherwise be inside the same buffers at once.

`IrSlot::isLoaded()` is the message thread's question; `isActive()` is the audio
thread's. They are not interchangeable.

**An idle engine's input history is a ghost.** A slot stops calling its convolver once
its fade has landed, so the history freezes at whatever was playing then — and a
response loaded on top of that convolves it, measured at −25 dBFS of the past arriving
on a signal that is now silent. `setImpulseResponse` therefore takes `discardHistory`
and the caller has to decide: keeping the history is exactly what makes an ordinary hot
swap seamless, and throwing it away is only right for an engine that has been idle. The
audio thread does the throwing, as it picks the filter up — that history is the one
thing in the convolver it owns outright.

## The blend pad

There is no per-cabinet gain. `BlendPad` replaced four gain knobs, and the reason is
what those knobs were being used for: nobody sets a cabinet's level in isolation, they
set it against the other three. Four numbers that have to stay summing to one is not
four controls — it is one control with four readouts.

`Parameters::blendWeights` is the law, and it lives there rather than in the pad because
three things need it: the audio thread, the mix curve, and the pad's own drawing.
Bilinear, a cabinet per corner — slot 0 top-left, 1 top-right, 2 bottom-left, 3
bottom-right, with y positive upwards. The centre is a quarter each; a corner is that
cabinet at about 85%, with the other three held at a floor of 5% each.

**A corner never silences the other three** (`Parameters::blendFloor`). Taking a cabinet
out of the blend altogether is what the mute button is for, and a pad that also did it
would be two controls doing one job with no way to tell from the pad which had been
used.

Two cases in `measureBlend` that look like edge-case noise and are not:

- **Solo hands the whole output to what is soloed**, however far the pad is from it,
  renormalised over the soloed set. Solo is an instruction to listen to something, so
  it has to arrive at full strength rather than at the share the pad left it.
- **Muting every cabinet is silence**, not the dry signal. Four muted cabinets are
  still four cabinets, and turning anything up would substitute a sound nobody asked
  for.

## Response length

A slot keeps as much of its response as its **RES** button asks for, cycling through
three tiers: NORMAL 2048, HIGH 8192, ULTRA 54000 samples. Quoted at 48 kHz and held as
durations, so a 96 kHz session gets the same cabinet rather than half of one
(`Parameters::referenceRate`). A speaker in a box has stopped long before the first of
them; the other two are for captures with a room on them, and ULTRA is past a second,
which is a reverb rather than a cabinet.

Measured with four cabinets at 48 kHz: NORMAL 1.8% of a core, HIGH 2.5%, ULTRA 6.3%.
The **memory is always the worst case**, though — `PartitionedConvolver::prepare` sizes
for `maximumResponseSeconds` whatever tier is in use, because reallocating outside
`prepareToPlay` would be resizing buffers the audio thread is inside. That is about
28 MB across the four slots at 48 kHz and twice that at 96. Changing the tiers changes
that number; the CPU is what the setting buys back.

ULTRA wears `Theme::ultra()`, a soft green, where HIGH wears the ordinary accent. Not a
warning — there is nothing wrong with being on it, it is only expensive — but it is not
simply more of the same either, so it does not read as one more step of the accent.

**What is dropped is faded, not cut** (`IrSlot::truncate`). A response ending on a
non-zero sample is a step convolved into every transient that goes through it, heard as
a click on the attack rather than as a shortened tail. A cabinet has stopped by 2048
samples and would not notice; a room very much does.

## Moving a cut without crackling

A cut spends most of its life parked at the end of its travel, switched off. Two
separate faults lived in moving it off that end, both measured, both now covered by
`tests/processor/ArtefactTests.cpp`:

- **It used to switch on from silence.** A filter engaged part way through a signal
  starts from zero state, so its first output is its *step* response rather than its
  steady one — a jump of a third of full scale into whatever is playing. On the high cut
  at 24 dB/octave that was the worst break in the waveform going up 1179×. It is faded
  in and out over 8 ms now (`CutFilter::engageSeconds`). Note that priming the state
  analytically was tried first and cannot work: the state a filter *would* have been in
  depends on the signal, and the two kinds disagree about it in the worst possible way —
  a low cut passes a steady signal and a high cut blocks it, so any single assumption is
  exactly wrong for one of them.
- **Coefficients were rebuilt once a block.** A cascade redesigned between one block and
  the next steps, and a step in an IIR holding energy is a transient rather than a
  change of tone. The frequency is ramped (multiplicatively — frequency is heard in
  ratios) and the coefficients rebuilt every 32 samples from where the ramp has reached.

`CutFilter` therefore has two entry points and the difference matters. `setParameters`
moves it at once and is for the copies the graph and the mix display keep — those never
call `process()`, so a ramp they never advance would leave them drawing the previous
setting for ever. `setTarget` is the audio path's, and ramps.

## Level

Three rules, and together they are why the plugin sits at the same level whatever is
loaded in it. Each was a complaint before it was a rule; changing any one of them
brings the complaint back.

- **A response is levelled so its loudest frequency is unity** (`IrSlot::levelToPeak`).
  A convolution sums a tap per sample, so a raw capture carries whatever gain its own
  length and level add up to — routinely twenty or thirty decibels. This makes a
  cabinet a filter that can only ever take away, which is what the loaders people
  compare this against do. The tempting alternative — normalising total energy, so
  convolving noise preserves its level — is about twelve decibels hot on a real
  cabinet: there is nothing above six kilohertz, so three quarters of the spectrum
  being near-silent drags the average down and the compensating gain up. Nobody hears
  the average. That twelve decibels is measured, not guessed.
- **The four shares always sum to one** (`Parameters::blendWeights`, renormalised by
  `PluginProcessor::measureBlend`). Four microphones on one speaker are largely
  correlated, so they add nearly arithmetically rather than as power — hence shares
  summing to one, not to its root. Four quarters and one whole come out at the same
  loudness; four halves would be six decibels up in the middle of the pad and nowhere
  else. The renormalisation is over what is *audible*, so muting one of four shares the
  output between the other three instead of quietly dropping the lot by a quarter.
- **Pan is equal-power referred to the centre**, scaled by √2 so a slot left alone is
  unity rather than three decibels down.

The spectrum display is scaled to match: `levelToPeak` takes the same amount off
the measured curve as off the audio, so the graph shows what the plugin is doing, and
the editor drops it a further fixed 6 dB purely so a slot turned up has somewhere to
go. The waveform display *does* scale to fit, but with one scale shared across all
four traces — normalising each separately would draw a cabinet turned down as though
it were not.

**One interpolation, three readers** (`source/dsp/Lagrange.h`). Alignment is a delay
line on the audio thread; the mix curve adds each cabinet in at that same delay; and a
response recorded at one rate is resampled on the way into a session at another. All
three read a signal between two of its samples, and they have to agree — a picture that
interpolated differently from the audio would be a picture of something else. Two things
measured here rather than assumed: linear interpolation cost 1.8 dB at 12 kHz and 3.0 dB
at 16 resampling 48 kHz into 44.1, so the same file was a duller cabinet at one rate than
the other; and reading at the wrong end of the pair puts a delayed cabinet a whole sample
early, which a comb test at the right frequency does not notice.

## Theming

Every colour is editable at runtime, from **Theme…** in the settings menu, and a theme
can be written to and read from a `.celthm` file to be shared.

The shape of it, in four files:

- **`ui/ThemeRoles.h`** — the list, as an X-macro. One entry carries four things that
  have to agree: the identifier the code uses, the label the editor shows, the group it
  is edited under, and the value the design ships with. The enum, the info table, the
  file's keys and the editor's rows are all generated from it. A plugin's own colours go
  in **`PluginThemeRoles.h`** beside it — GALLERY's four cabinets, AURA's three curves —
  and its accessors in **`PluginTheme.h`**, which `Theme.h` includes inside the
  namespace so `Theme::irSlot(2)` reads exactly like `Theme::chrome()`.
- **`ui/ThemePalette.h/.cpp`** — the colours in force, the `.celthm` reader and writer,
  and a `ChangeBroadcaster` so a change reaches every open window. One per process, in a
  function-local static: a palette at namespace scope could be read by a look and feel
  constructed before it.
- **`ui/Theme.h`** — the accessors, each a lookup, each documented with what it is *for*.
- **`ui/ThemePanel.h/.cpp`** — the editor. Live: a colour changed there reaches the
  window behind it on the next repaint, so there is no Apply to forget.

**Renaming a role breaks every theme anybody has saved**, because the identifier is the
key in the file. Adding one is free — an unknown key is ignored and a missing one keeps
its shipped value, which is what lets a theme written by a plugin with more colours than
this one still load.

**The theme file is shared by every Céline plugin** — one `theme.celthm` under the
company folder. Theming one of them themes all of them, which is the point of a house
look, and the ignore-unknown-keys rule is what makes one file serve three plugins with
different palettes.

Two things a theme change has to do, and both are easy to leave out:
`PluginLookAndFeel::applyPalette()` re-reads everything JUCE is *told* rather than asks
for, and `sendLookAndFeelChange()` gives every child a chance to do the same. The window
does both in its `changeListenerCallback`.

## House conventions

**Assets are looked up by filename, never by the BinaryData identifier.** JUCE derives
those identifiers by *stripping* characters rather than replacing them, so
`arrow-pointer-solid-full.svg` becomes `arrowpointersolidfull_svg`. Getting it wrong is
silent — the lookup returns null and nothing draws. Use `Celine::Assets::drawable("name.svg")`.
An asset that may legitimately be absent passes `IfMissing::returnNull`, or it will
assert on every launch in Debug.

**Colours come from `Theme`, never from a hex literal at the call site**, and every one
of them is a lookup rather than a constant: what they answer is whatever theme is in
force. Two consequences, both of which are silent when broken:

- **Read them at paint time.** A colour taken once in a constructor and handed to
  `setColour` is a snapshot, and a snapshot does not follow a theme change. Where a JUCE
  widget insists on being *told* its colours, gather them into an `applyColours()` and
  call it from both the constructor and an override of `lookAndFeelChanged()` — which is
  what the window calls on every child when the theme moves.
- **A new colour goes in `ui/ThemeRoles.h`** (or the plugin's own `PluginThemeRoles.h`),
  which is the one list the enum, the `.celthm` key, the editor's label and the shipped
  value are all generated from. `Theme.h` is where it is given a name and a reason.

**A slot's colour is its name.** `Theme::irSlot(index)` gives teal, red, purple, gold
for slots 0 to 3, and anything drawn for a slot wears it: its traces on both graphs,
its knob rings, its cut band, the rule along the top of its strip. The three state
pills — `Theme::solo()`, `mute()`, `phase()` — deliberately do *not*: solo means the
same thing on all four cabinets, and colouring it by slot would say the opposite.

Note that `Theme::teal()` is a lavender despite its name, inherited from the kit. It is
not slot one's colour; `Theme::irTeal()` is.

**A component's `setSize` goes last in its constructor.** It fires `resized()`, and
`resized()` measures artwork and children that must exist by then. Called early, it
silently places everything at zero size — and a window that later opens at a different
size hides the bug completely.

## Code quality

Resolve every compile warning. Warnings are errors here.

LSP/clangd reports false positives against JUCE's module system ("undeclared
identifier", "file not found"). Ignore those unless the build actually fails.

## Threading

Two threads:

- **Audio** — `processBlock`. Never allocate, lock, or block. `parameterChanged` counts:
  host automation calls it on this thread, so it may only set a flag. Even
  `juce::AsyncUpdater` is too much — posting a message takes a lock and can allocate.
- **Message** — UI, parameter listeners, timers.

Between them: `std::atomic` or JUCE's parameter types for scalars; a lock-free queue
for anything larger; `juce::Timer` on the message thread to poll state for the UI.
Never call UI code from the audio thread.

## Realtime safety

In `processBlock` and anything it calls: allocate in `prepareToPlay`, not here. No
container growth, no `push_back`, no string building. Prefer fixed-size storage. If a
host hands you a bigger block than it promised, do less work — never grow a buffer.

## The two hot loops

Nearly all of the audio thread is `PartitionedConvolver`'s head dot product and its
tail's complex multiply-accumulate. Both are hand-written for a reason, and both look
like code somebody would tidy:

- **`dotProduct` keeps sixteen running totals.** Float addition is not associative, so a
  reduction into one total is a dependency chain the compiler may not reorder — or
  vectorise — without `-ffast-math`, which this build does not use. Four totals vectorise
  but leave one vector accumulator waiting on its own latency. Sixteen keeps four
  independent chains in flight: 61 ns a dot product down to 17.
- **`multiplyAccumulate` spells out the complex arithmetic.** `std::complex::operator*`
  carries the standard's NaN-to-infinity recovery branch, which is unvectorisable. For
  finite operands the written-out version is the same operations in the same order, so
  it agrees bit for bit: 259 ns a run down to 103.

Together these took `processBlock` with four cabinets from 511 µs to 223 µs a
512-sample block. If either loop is ever rewritten, re-run `./cmake-build-release/Benchmarks`
— "processBlock, four cabinets" is the number that matters, and the empty case beside it
measures the early-out path and will not notice.

## Drawing a spectrum

`LogSpectrum` exists because of one mistake that took three rounds of review to
understand, and it is worth reading before touching either graph.

A transform's bins are evenly spaced in hertz; the axis is evenly spaced in octaves.
At the bottom of it that mismatch is enormous — a thousand-point axis steps by about a
seventh of a hertz at 20 Hz, where a bin is several hertz wide — so **a dozen
consecutive display points fall inside one bin**. Reading the nearest bin, or averaging
a range of them rounded to whole bins, gives all twelve the same value: a flat tread,
then a step. It looks like a rendering fault and it is not; the data really is stepped,
so smoothing it, drawing it as curves, or adding points all leave it exactly as it was.

Two things fix it, and both are load-bearing:

- **Zero-pad well past the response** (`fftSizeFor`). Padding is exact interpolation of
  the spectrum the response would have had, so it is what turns a handful of bins
  across the bottom octaves into enough to draw through.
- **Weight partial bins, and never let the window fall below two of them.** A window
  narrower than a bin can only report the bin it sits in; a window rounded to whole
  bins is a step function of frequency. Both bring the staircase back.

`tests/dsp/LogSpectrumTests.cpp` pins this by measuring the longest run of identical
values in a curve read from a *sloping* spectrum — where any flat run at all is the
reader's doing. Note that it checks the bottom two octaves separately: the top of the
axis has many bins to a point and was never the problem, so a test averaged over the
whole range passes happily while the part anybody would notice is still stepped.

**Both views are drawn through `MixSpectrum`**, which holds each cabinet filtered by
its own cuts. The spectrum blends those into one curve; the waveform summarises them
one at a time. A cut is part of what a cabinet sounds like, so a view of the response
that ignored one would draw a bottom end that is not there.

**Both views also have to place the cabinets in time themselves**, and for the same
reason: alignment is a delay line on the audio thread, so it is not in the samples
either view is built from. `MixSpectrum::compute` therefore takes each cabinet's delay
and adds it in at that offset — interpolated with the same four-point Lagrange the delay
line uses, because rounded to whole samples the notches would jump from one place to the
next as the knob turned. Summed at offset zero the curve is identical at every setting
of the control, which is the one picture that makes alignment look pointless.

**The waveform places the traces itself.** Alignment is a delay line on the audio thread
rather than a shift baked into the response, so a cabinet pushed back is the same
samples arriving later — `Trace::startSeconds` is where the display puts them. Without
it the four sit on top of each other however far apart they have been moved, which
takes the one thing that view exists for and leaves the control apparently doing
nothing. The blend does *not* reach the waveform: it is there to align, and four traces
that changed height as the pad moved would be answering the spectrum's question badly.

That offset is also why **a trace's column count comes from what the trace covers, not
from the window**. A cabinet pushed back fills less of the view than the window spans, so
columns counted from the window summarise under a sample each and the trace comes back
stepped — which reads as a rendering fault and is not one.

The cabinets' curves are **not** smoothed after sampling — twelfth-octave averaging is
already the resolution they are meant to have, and blurring them again removes the
detail somebody is reading the cabinet for. The live output trace *is* smoothed
(`LogSpectrum::smooth`): its ripple is measurement noise with nothing underneath it.

## The library, and exporting a blend

`LibraryPanel` is a browser, not a managed library: it holds no files, copies nothing,
and lists whatever is in the folder it was pointed at, read fresh each time. A row is
dragged onto a strip to load it, which is what it exists for — through the Load button,
trying the next cabinet is a file dialog per slot, four times over, in the same folder
each time.

**The folder itself is not session state.** It lives in `Celine::Settings`, a settings
file under the company folder, because it is one folder on one machine and pointing every
new instance at it again is what stops a library from being used. The session keeps a copy
as a fallback, which is what opens a project saved before that file existed — and what
opens one carried to a machine that has never run the plugin. The settings file is opened
for each read and write rather than held: instances share it, and one holding a copy would
write back at close what it read at load, quietly undoing a folder chosen since.

Subfolders are listed too, opened by a chevron, and the whole tree is scanned up front —
so a search reaches the folders somebody has *not* opened, which is the question they are
asking by typing. The search field carries the root's name in its placeholder rather than
beside it, so one line holds the title, where you are, and both buttons. Both the search
and the shut folders filter into a second array (`shown`) instead of skipping rows while
painting: a row is painted *and* dragged by index, and the two have to mean the same file.

A strip therefore accepts **two kinds of drop and they are not the same thing**. A file
from the desktop arrives as `FileDragAndDropTarget`; a row from the library never leaves
the window and arrives as `DragAndDropTarget`, carrying the path as the drag's own
description. Deliberately not an OS file drag: that would hand the file to whatever is
behind the plugin as readily as to a strip.

**`PluginProcessor::renderBlend` runs an impulse through a copy of the plugin** rather
than summing the four responses again with the same arithmetic written out a second
time. Two implementations of one signal path drift, and the day they do, the file
somebody exported stops being the sound they were listening to — which is the one thing
an export must never be. The copy is settled with three quarters of a second of silence
first, because the wet mix fades in over 50 ms, the engine walks to its filter over 40
and the cuts engage over 8: an impulse sent before all of that lands comes back carrying
the fades rather than the filter.

The output trim and bypass are left out of the render. Those are how loud the plugin
sits in a mix and whether it is switched in, neither of which is a property of a
cabinet. `tests/processor/ExportTests.cpp` checks the rendered file matches
what the plugin gives an impulse, sample for sample.

## Verifying UI work

Render it rather than describing it. A throwaway test using
`createComponentSnapshot`, written to `/tmp` and read back, settles questions about
layout that reasoning does not. Delete the file afterwards. For a refactor that should
change nothing, snapshot before and after and compare the hashes — that turns "it
looks the same" into proof.

`tests/EditorTests.cpp` does this permanently for the thing most worth protecting: it
renders the window with four cabinets loaded and counts pixels of each slot's hue in
the graph, so a trace that stops being fed fails the build instead of leaving a graph
that still looks like a graph.

Measure the *plot*, not the graph's bounds, if you add to it. The tab bar behind the
front tab is `chrome()`, a dark violet of the same hue as slot three — thirty thousand
pixels of it — so a count that includes it reports a purple trace whatever the plugin
is doing.

## Adding dependencies

JUCE modules go in `modules/` as submodules, then `add_subdirectory` and link to
`SharedCode`. Everything else goes through CPM, already configured:

```cmake
CPMAddPackage("gh:nlohmann/json@3.11.3")
```

## Code style

`.clang-format`: Allman braces, 4-space indent, no column limit.
