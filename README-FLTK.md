# XNEdit XR

Native FLTK text editor for DomeD, forked from [unixwork/xnedit](https://github.com/unixwork/xnedit).
The upstream history and legacy frontend are retained. This frontend uses XNEdit's
text buffer, UTF-8 utilities, regular-expression engine and EditorConfig parser;
it does not link Motif or Xt. The default build requires Wayland. X11 is an
explicit build option and never an automatic fallback.

## Build

Use the de-xr patched FLTK 1.4.5 toolkit, including native BDF rendering and glyph
fallback. Build as your regular user:

```sh
cmake -S . -B build-fltk -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DFLTK_DIR=/opt/fltk-vr/toolkit/share/fltk
cmake --build build-fltk --parallel 4
ctest --test-dir build-fltk --output-on-failure
```

`make` also builds this FLTK frontend. The upstream Motif build is available only
through explicit legacy targets such as `make legacy` / `make linux`.

To opt into X11 support, use a separate build with `-DXNEDIT_ENABLE_X11=ON`.
That build still defaults to Wayland; select X11 with `--x11` or
`FLTK_BACKEND=x11`. The default binary rejects both forms of X11 selection.
The shared workspace FLTK library may contain both platform drivers, but the
application's X11 association code and dependencies are compiled only when opted in.

## Editing

- Multiple independent document tabs, split views, line numbers, wrapping, syntax
  colors, bracket matching, indentation, duplicate/delete line, and read-only mode.
- Enter and keypad Enter reveal each new line from the left edge, including after
  horizontal scrolling. Auto-indent preserves the preceding indentation; disable
  Document → Auto indent to start new lines at column zero.
- XNEdit-buffer-backed undo/redo. Literal search supports Unicode case-insensitive
  matching; regex search, lookaround and captured replacement use the upstream engine.
- UTF-8, UTF-16LE/BE and explicitly selected legacy encodings; BOM and LF/CRLF/CR
  line endings survive load/save. Invalid text is rejected rather than silently
  replaced. Open with Encoding supports additional GLib conversion encodings.
- Atomic saves preserve existing file permissions, detect changes on disk before
  replacing a file, and support symlink targets. The interactive size limit is 64 MiB.
- The upstream EditorConfig parser supplies indentation, tab width, encoding and
  line endings. As upstream, trimming trailing whitespace/final-newline properties
  are not implemented by that parser.
- Recent files and window size are stored in `$XDG_CONFIG_HOME/xnedit-xr/settings.json`.
  Recovery copies of modified buffers are written every five seconds under
  `$XDG_STATE_HOME/xnedit-xr/recovery`, with private permissions. A subsequent launch
  offers recovery after the owning process has exited. Closing a dirty document
  requires Save, Discard or Cancel.
- Native clipboard and primary-selection paste use FLTK. Scrolling follows normal
  desktop direction. Font, native bitmap strike, palette and accent follow DomeD.

DomeD draws the exported universal menu. The local menu collapses only after a
working host and window association exist, and returns if the host disappears.
Shortcuts continue to work while the menu is external. Ctrl+N/O/S/W/Q manage files,
Ctrl+Z / Ctrl+Shift+Z undo/redo, Ctrl+F/H find/replace, F3 / Shift+F3 search,
Ctrl+L goes to a line, and Ctrl+PageUp/PageDown changes tabs. Escape closes search;
it does not quit the editor. Super shortcuts remain owned by DomeD.

```sh
xnedit file.txt other.py
xnedit +120 source.c
xnedit --read-only notes.txt
xnedit --encoding WINDOWS-1252 legacy.txt
xnedit --version
xnedit --build-options
```

## Scope

Remote `xnc` and the NEdit macro language/interpreter are outside this fork's
scope. The new frontend does not currently expose rectangular selection or the
legacy custom highlight-pattern dialogs; syntax colors are a native lexical
implementation. The legacy source remains available for reference and does not
become the default frontend.

## Validation and installation

```sh
uv run --no-project --with python-xlib --with pillow python tests/test-wayland.py
XNEDIT_TEST_BINARY="$PWD/build-x11/xnedit" \
  uv run --no-project --with dbus-next --with python-xlib --with pillow python tests/test-ui.py
```

The first test uses a private real DomeD compositor and verifies mandatory
Wayland, menu association and actual keyboard editing/saving. The opt-in X11
suite verifies menu-host transitions, split editing, search/regex replacement,
undo/redo, independent tabs and read-only files. Artifacts stay under build-fltk.

`cmake --install build-fltk --prefix /usr/local` installs the binary, desktop entry
and documentation. `scripts/install-system.py --prepare DIRECTORY --remove-kate` stages the
release and a reviewed install plan, including DomeD-specific MIME defaults.
Run the prepared `install.py` with sudo only after reviewing that plan. Existing
files are backed up. Kate removal is restricted to the installed `kate`,
`kate-plugins` and `kate-krunner-plugin` packages, checked with RPM before removal;
it does not autoremove KDE libraries or delete personal Kate settings. No desktop
or display-manager session is restarted.

GPL-2.0-or-later, retaining upstream notices and exceptions. Shared de-xr UI hooks
are MIT; see NOTICE-FLTK.md and UPSTREAM.json.
