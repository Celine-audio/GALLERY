#!/bin/bash
#
# Turns this template into a named plugin. Run it once, in a fresh copy:
#
#     cp -R template MyPlugin && cd MyPlugin && ./scaffold.sh
#
# With no arguments it asks. With arguments it does not, which is what you want
# from a script another script calls:
#
#     ./scaffold.sh "Tape Delay" TapeDelay Tdly "A delay that wobbles"
#
# It rewrites the identity in CMakeLists.txt and source/ProductInfo.h, resets the
# version, rewrites the README's title, and offers to start a fresh git history.

set -euo pipefail

cd "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if [[ ! -f CMakeLists.txt || ! -f source/ProductInfo.h ]]; then
    echo "This does not look like the template directory." >&2
    exit 1
fi

# Refuse to run twice. The rewrites below are search-and-replace against the
# template's placeholder names, so a second run would find nothing to change and
# quietly report success on a project it had not touched.
if ! grep -q 'set(PROJECT_NAME "PluginTemplate")' CMakeLists.txt; then
    echo "Already scaffolded: CMakeLists.txt no longer names PluginTemplate." >&2
    echo "Start from a fresh copy of the template if you meant to do this." >&2
    exit 1
fi

# --- clear out anything the copy should not have brought ------------------------
#
# `cp -R` copies the template's build directories, and a CMakeCache.txt records the
# absolute path it was generated in. Configuring the copy then fails with "the
# current CMakeCache.txt directory is different than the directory where
# CMakeCache.txt was created" -- which points at the cache rather than at the copy,
# so it reads like a CMake fault rather than a stale file.
#
# .idea is the same story for CLion: it holds the old project's name and paths.
#
# All of it is already in .gitignore, so a `git clone` never carries any of it. This
# is purely for the copy route, and it runs before anything else because opening the
# project in an IDE is often the first thing that happens after scaffolding.
STALE=()
for junk in cmake-build-debug cmake-build-release cmake-build-relwithdebinfo \
            .idea .env .cache compile_commands.json; do
    [[ -e "$junk" ]] && STALE+=("$junk")
done

# Builds/ is kept, since .gitignore deliberately keeps the empty folder as a hint.
if [[ -d Builds ]]; then
    find Builds -mindepth 1 ! -name '.gitkeep' -delete 2>/dev/null || true
fi

if (( ${#STALE[@]} )); then
    echo "Removing build and IDE state carried over by the copy: ${STALE[*]}"
    rm -rf "${STALE[@]}"
fi

find . -name '.DS_Store' -not -path './JUCE/*' -delete 2>/dev/null || true

# Falls back to the default when there is no terminal to ask at, so the script is
# usable from another script or a CI step without hanging or erroring.
ask() {
    local prompt="$1" default="$2" reply=""

    # -t 0 rather than testing /dev/tty: the device node exists in places where
    # opening it still fails, and the question being asked is whether a person is
    # there to answer, which is exactly what a terminal on stdin means.
    if [[ -t 0 ]]; then
        read -r -p "$prompt [$default]: " reply || true
    fi

    echo "${reply:-$default}"
}

# --- the facts, each checked against what CMake and JUCE actually allow ---------
#
# Checked rather than assumed, because every one of these fails late and far from
# the cause: a space in PROJECT_NAME breaks the CMake target, an accent in a name
# that gets stripped to ASCII silently loses letters (this is how "Céline Audio"
# once became "clineaudio"), and a duplicate PLUGIN_CODE makes a host treat two
# plugins as one and quietly load neither.

# Re-asks when there is somebody to ask, and fails loudly when there is not. A
# script that silently accepts an invalid name hands back a project that will not
# configure, with nothing pointing at why.
ask_valid() {
    local prompt="$1" default="$2" pattern="$3" why="$4" value

    while true; do
        value=$(ask "$prompt" "$default")

        if [[ "$value" =~ $pattern ]]; then
            printf '%s' "$value"
            return 0
        fi

        echo "  \"$value\" will not do: $why" >&2
        [[ -t 0 ]] || exit 1
    done
}

PRODUCT="${1:-}"
if [[ -z "$PRODUCT" ]]; then
    PRODUCT=$(ask_valid "Product name, as DAWs will show it" "My Plugin" \
        '^[A-Za-z0-9][A-Za-z0-9 ._+-]*$' \
        "spaces are fine, but keep it to unaccented letters, digits and . _ + - . It becomes the bundle filename, and travels through zip archives and download URLs where anything else is trouble.")
elif [[ ! "$PRODUCT" =~ ^[A-Za-z0-9][A-Za-z0-9\ ._+-]*$ ]]; then
    echo "Product name must be unaccented letters, digits, spaces and . _ + -" >&2
    exit 1
fi

# The internal name is the product with everything a CMake target cannot hold
# stripped out. tr on bytes, so this only ever produces ASCII -- and if that leaves
# nothing usable, the validator below catches it rather than the build doing so.
DEFAULT_PROJECT="$(printf '%s' "$PRODUCT" | LC_ALL=C tr -cd 'A-Za-z0-9')"
[[ -z "$DEFAULT_PROJECT" ]] && DEFAULT_PROJECT="MyPlugin"

PROJECT="${2:-}"
if [[ -z "$PROJECT" ]]; then
    PROJECT=$(ask_valid "Internal name" "$DEFAULT_PROJECT" \
        '^[A-Za-z][A-Za-z0-9_-]*$' \
        "no spaces and no accents: it is a CMake target name, a C++ identifier in places, and a directory in the build tree. Letters, digits, _ and - only, starting with a letter.")
elif [[ ! "$PROJECT" =~ ^[A-Za-z][A-Za-z0-9_-]*$ ]]; then
    echo "Internal name must start with a letter and hold only letters, digits, _ and -" >&2
    exit 1
fi

# Four characters, at least one uppercase: JUCE requires it, and a host treats two
# plugins sharing a code as the same plugin.
DEFAULT_CODE="$(printf '%s' "$DEFAULT_PROJECT" | cut -c1-4)"
while [[ ${#DEFAULT_CODE} -lt 4 ]]; do DEFAULT_CODE="${DEFAULT_CODE}x"; done
DEFAULT_CODE="$(printf '%s' "${DEFAULT_CODE:0:1}" | LC_ALL=C tr 'a-z' 'A-Z')${DEFAULT_CODE:1}"

CODE="${3:-}"
if [[ -z "$CODE" ]]; then
    CODE=$(ask_valid "Plugin code, unique to this plugin" "$DEFAULT_CODE" \
        '^[A-Za-z0-9]*[A-Z][A-Za-z0-9]*$' \
        "exactly four ASCII letters or digits with at least one uppercase. Two plugins sharing a code are one plugin as far as a host is concerned, and the second to load simply does not appear.")
fi

if [[ ${#CODE} -ne 4 || ! "$CODE" =~ ^[A-Za-z0-9]*[A-Z][A-Za-z0-9]*$ ]]; then
    echo "Plugin code must be exactly 4 letters or digits with at least one uppercase." >&2
    exit 1
fi

TAGLINE="${4:-}"
if [[ -z "$TAGLINE" ]]; then
    TAGLINE=$(ask_valid "One line saying what it does" "TODO: what this plugin does" \
        '^[^"\\]*$' \
        "it is written into a C++ string literal, so no double quotes or backslashes.")
elif [[ "$TAGLINE" == *'"'* || "$TAGLINE" == *'\'* ]]; then
    echo "The tagline goes into a C++ string literal: no double quotes or backslashes." >&2
    exit 1
fi

# ASCII only, and the reason is worth stating: it is written into a C++ source file
# that MSVC compiles without /utf-8, where a raw accented byte is read in the local
# codepage rather than as UTF-8. The default keeps the escaped spelling already in
# ProductInfo.h, so "Céline Audio" survives by not being rewritten at all.
COMPANY=$(ask_valid "Company" "Celine Audio" \
    '^[A-Za-z0-9][A-Za-z0-9 .,&_+-]*$' \
    "unaccented letters, digits and simple punctuation. An accented display name is safest added to ProductInfo.h by hand, as an escape.")

ORG=$(ask_valid "GitHub owner" "Celine-audio" \
    '^[A-Za-z0-9]([A-Za-z0-9-]*[A-Za-z0-9])?$' \
    "a GitHub account or organisation name: letters, digits and hyphens, not starting or ending with one.")

# Defaults to yes when there is nobody to ask, which is the right way round: a
# standalone that turns out to be unwanted is deleted from FORMATS in one line, where
# a missing one is only noticed when somebody wants to run the plugin without a host.
ask_yes_no() {
    local prompt="$1" reply=""

    if [[ -t 0 ]]; then
        read -r -p "$prompt [Y/n]: " reply || true
    fi

    [[ ! "$reply" =~ ^[Nn] ]]
}

if ask_yes_no "Build a standalone application as well as the plug-in?"; then
    STANDALONE=TRUE

    # Asked rather than assumed. macOS shows this text in the permission dialog, and
    # a plugin that asks for the microphone and never uses it is a thing people
    # notice and hold against you.
    if ask_yes_no "  Does it take live audio input (asks for microphone access)?"; then
        MICROPHONE=TRUE
    else
        MICROPHONE=FALSE
    fi

    # ASIO is how Windows users get low latency. Enabling it here only states the
    # intent -- CMake still requires the SDK to be present before it switches the
    # flag on, because JUCE_ASIO=1 without it does not build at all.
    if ask_yes_no "  Support ASIO on Windows (needs Steinberg's SDK at build time)?"; then
        ASIO=TRUE
    else
        ASIO=FALSE
    fi
else
    STANDALONE=FALSE
    MICROPHONE=FALSE
    ASIO=FALSE
fi

# Derived from the GitHub owner, not the company's display name. A bundle ID has to
# be ASCII, and stripping non-alphanumerics from a name with an accent in it does not
# transliterate -- it deletes the bytes, so "Céline Audio" came out "clineaudio".
BUNDLE_SUFFIX="$(printf '%s' "$PROJECT" | LC_ALL=C tr 'A-Z' 'a-z')"
BUNDLE_PREFIX="$(printf '%s' "$ORG" | LC_ALL=C tr -cd 'A-Za-z0-9' | LC_ALL=C tr 'A-Z' 'a-z')"
BUNDLE_ID="com.$BUNDLE_PREFIX.$BUNDLE_SUFFIX"
REPO_URL="https://github.com/$ORG/$PROJECT"

echo
echo "  Product     $PRODUCT"
echo "  Project     $PROJECT"
echo "  Code        $CODE"
echo "  Company     $COMPANY"
echo "  Bundle ID   $BUNDLE_ID"
echo "  Source      $REPO_URL"
echo "  Standalone  $STANDALONE (microphone $MICROPHONE, ASIO $ASIO)"
echo

# --- rewrite ------------------------------------------------------------------

# A literal, not a regex: product names contain characters sed would otherwise
# read as syntax.
escape() { printf '%s' "$1" | sed -e 's/[&|\\]/\\&/g'; }

sed -i '' \
    -e "s|set(PROJECT_NAME \"PluginTemplate\")|set(PROJECT_NAME \"$(escape "$PROJECT")\")|" \
    -e "s|set(PRODUCT_NAME \"Plugin Template\")|set(PRODUCT_NAME \"$(escape "$PRODUCT")\")|" \
    -e "s|set(BUNDLE_ID \"com.celineaudio.plugintemplate\")|set(BUNDLE_ID \"$(escape "$BUNDLE_ID")\")|" \
    -e "s|PLUGIN_CODE Tmpl|PLUGIN_CODE $(escape "$CODE")|" \
    -e "s|LV2URI \"https://github.com/Celine-audio/PluginTemplate\"|LV2URI \"$(escape "$REPO_URL")\"|" \
    -e "s|set(COMPANY_NAME \"Celine Audio\")|set(COMPANY_NAME \"$(escape "$COMPANY")\")|" \
    -e "s|^set(BUILD_STANDALONE .*)$|set(BUILD_STANDALONE $STANDALONE)|" \
    -e "s|^set(NEEDS_MICROPHONE .*)$|set(NEEDS_MICROPHONE $MICROPHONE)|" \
    -e "s|^set(WANT_ASIO .*)$|set(WANT_ASIO $ASIO)|" \
    CMakeLists.txt

# ProductInfo's companyName holds the accented spelling as an escape sequence. Left
# alone when the company is unchanged, so "Céline Audio" survives; rewritten plainly
# only when it is something else, which the validator has already held to ASCII.
if [[ "$COMPANY" != "Celine Audio" ]]; then
    sed -i '' \
        -e "s|inline constexpr auto companyName = .*;|inline constexpr auto companyName = \"$(escape "$COMPANY")\";|" \
        source/ProductInfo.h
fi

sed -i '' \
    -e "s|inline constexpr auto tagline = \".*\";|inline constexpr auto tagline = \"$(escape "$TAGLINE")\";|" \
    -e "s|inline constexpr auto repositoryUrl = \".*\";|inline constexpr auto repositoryUrl = \"$(escape "$REPO_URL")\";|" \
    source/ProductInfo.h

# The licence and policy files name the product in prose. One token pair throughout
# -- "Plugin Template" and "PluginTemplate", matching PRODUCT_NAME and PROJECT_NAME --
# so there is a single thing to replace rather than a search through paragraphs.
for doc in LICENSE THIRD-PARTY-NOTICES CONTRIBUTING.md SECURITY.md; do
    [[ -f "$doc" ]] || continue
    sed -i '' \
        -e "s|Celine-audio/PluginTemplate|$(escape "$ORG/$PROJECT")|g" \
        -e "s|Plugin Template|$(escape "$PRODUCT")|g" \
        -e "s|PluginTemplate|$(escape "$PROJECT")|g" \
        "$doc"
done

echo "0.1.0" > VERSION

# The README is the template's own, describing the template. Replace it with one for
# the new plugin, in the house shape -- and reflecting the answers above, so it does
# not advertise a standalone or an ASIO build that was declined.

FORMAT_LINE="Built as **VST3®**, **AU** (macOS), **LV2** and **CLAP**, on Windows, macOS and Linux."
[[ "$STANDALONE" == TRUE ]] && FORMAT_LINE="Built as **VST3®**, **AU** (macOS), **LV2**, **CLAP** and **Standalone**, on Windows, macOS and Linux."

{
cat <<EOF
# $PRODUCT

$TAGLINE

---

## Formats

$FORMAT_LINE
EOF

if [[ "$ASIO" == TRUE ]]; then
cat <<'EOF'

The Windows standalone supports **ASIO®** alongside WASAPI and DirectSound. It is off
unless the SDK is found at build time -- see Building below.
EOF
fi

cat <<'EOF'

Nothing is code-signed, so Gatekeeper and SmartScreen will warn on first run.

<p>
EOF

if [[ "$ASIO" == TRUE ]]; then
cat <<'EOF'
  <img alt="ASIO Compatible. ASIO is a registered trademark of Steinberg Media Technologies GmbH"
       src="docs/logos/ASIO.png" height="78">
  &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;
EOF
fi

cat <<'EOF'
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="docs/logos/VST.png">
    <img alt="VST Compatible. VST is a registered trademark of Steinberg Media Technologies GmbH" src="docs/logos/VST_2.png" height="78">
  </picture>
  &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="docs/logos/AU-onwhite.svg">
    <img alt="Audio Units" src="docs/logos/AU.svg" height="78">
  </picture>
  &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="docs/logos/CLAP-white.png">
    <img alt="CLAP" src="docs/logos/CLAP.svg" height="70">
  </picture>
  &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="docs/logos/lv2_white.svg">
    <img alt="LV2" src="docs/logos/lv2_black.svg" height="52">
  </picture>
</p>

---

## Built on

[JUCE](https://juce.com) 9, with the build system derived from
[Pamplejuce](https://github.com/sudara/pamplejuce). CLAP support comes from
[clap-juce-extensions](https://github.com/free-audio/clap-juce-extensions).

---

## Building

Needs **CMake 3.25** or newer and a **C++23** compiler.

The submodules are not optional — JUCE, the shared CMake modules and the CLAP
wrapper all live in them, and one has submodules of its own:

EOF

cat <<EOF
\`\`\`bash
git clone --recursive $REPO_URL.git
\`\`\`
EOF

cat <<'EOF'

If you already cloned without `--recursive`:

```bash
git submodule update --init --recursive
```

Then:

```bash
cmake -B Builds -G Ninja -DCMAKE_BUILD_TYPE=Release
```

```bash
cmake --build Builds
```

On macOS, `-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"` builds a universal binary — keep
the quotes, or the shell eats the semicolon and you get one architecture.

To run the tests:

```bash
ctest --test-dir Builds --output-on-failure
```
EOF

if [[ "$ASIO" == TRUE ]]; then
cat <<'EOF'

### ASIO

Off unless the SDK is found, deliberately: `JUCE_ASIO=1` without it does not degrade
gracefully, the Windows build fails to compile. Download the
[ASIO SDK](https://www.steinberg.net/developers/asiosdk-open/), unpack it, and point
CMake at it:

```bash
cmake -B Builds -G Ninja -DASIO_SDK_DIR=/path/to/asiosdk
```

CMake reports which way it went at configure time.
EOF
fi

cat <<EOF

---

## Disclaimer

This software is provided "as is", without warranty of any kind. No liability can be
claimed for any harm or damage caused by its use.

---

## Licence and credits

$PRODUCT being free open-source software using the [JUCE](https://juce.com)
framework, and using its free licence, it inherits its AGPLv3 terms. $PRODUCT is then
under the [GNU AGPL v3](COPYING) licence. The full notices are in
[\`LICENSE\`](LICENSE) and [\`THIRD-PARTY-NOTICES\`](THIRD-PARTY-NOTICES), and the same
summary is available within the plugin under **Settings → About**.

<p>
  <img alt="Licensed under the GNU AGPL v3" src="docs/logos/AGPLv3.svg" height="62">
</p>

### What that means in practice

Using $PRODUCT costs nothing and obliges nothing. The licence governs the distribution
*of the software*, not what you make with it. You may fork and modify it, provided you
do so under the AGPLv3 licence and pass the source on.

### Credits

- Build system derived from [Pamplejuce](https://github.com/sudara/pamplejuce),
  © 2022 Sudara Williams, MIT
- Icons from [Font Awesome Free](https://fontawesome.com), © Fonticons, Inc., used
  under [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/)
- Typefaces [Jura](https://github.com/ossobuffo/jura),
  [JetBrains Mono](https://github.com/JetBrains/JetBrainsMono) and
  [Nico Moji](https://fonts.google.com/specimen/Nico+Moji), all under the SIL OFL 1.1
EOF

if [[ "$ASIO" == TRUE ]]; then
cat <<'EOF'
- VST® and ASIO® are registered trademarks of Steinberg Media Technologies GmbH
- ASIO Interface Technology by Steinberg Media Technologies GmbH. The
  [ASIO SDK](https://www.steinberg.net/developers/asiosdk-open/) is dual-licensed
  under Steinberg's own licence or the GPLv3; this project takes the GPL option,
  which is what lets an ASIO-enabled build stay AGPLv3
EOF
else
cat <<'EOF'
- VST® is a registered trademark of Steinberg Media Technologies GmbH
EOF
fi
} > README.md

echo "Rewrote CMakeLists.txt, source/ProductInfo.h, VERSION and README.md."
echo

# --- history ------------------------------------------------------------------

if [[ -d .git ]]; then
    fresh=""
    if [[ -t 0 ]]; then
        read -r -p "Start a fresh git history? The template's commits are not yours. [y/N]: " fresh || true
    fi

    if [[ "${fresh:-}" =~ ^[Yy]$ ]]; then
        # An orphan branch rather than deleting .git and re-initialising. The
        # submodules' own checkouts live under .git/modules, and their working trees
        # hold a .git *file* pointing into it -- throw the directory away and the
        # submodules are left dangling, with the next `git submodule update` failing
        # on a repository that no longer exists. This keeps all of that and simply
        # gives the work a history with no commits behind it.
        git checkout -q --orphan _scaffold
        git add -A
        git commit -q -m "Initial commit"
        git branch -M main

        # The template's remote is not this plugin's.
        git remote remove origin 2>/dev/null || true
        git remote remove backup 2>/dev/null || true

        echo "History reset to a single commit. Add your remote with:"
        echo "  git remote add origin $REPO_URL.git"
    fi
fi

echo
echo "Done. Next:"
echo "  1. Your DSP goes in processBlock, between the two marked points."
echo "  2. Your controls go in PluginEditor::buildContent and layOutContent."
echo "  3. Parameters go in source/Parameters.{h,cpp}, both lists."
echo "  4. Drop a wordmark at assets/icons/wordmark.svg when you have one."
