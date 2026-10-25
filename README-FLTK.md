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

## Remote editing

### Gemini, Titan and Spartan

Use **Remote → Retrieve page...** (`Ctrl+Alt+O`) or launch:

```sh
xnedit gemini://jebot.dog/
xnedit spartan://jebot.dog/
```

The response opens in a document tab with Gemtext highlighting when its MIME
is `text/gemini`. Edit normally, then use **Remote → Push document...**
(`Ctrl+Alt+S`) to review the destination, MIME type and optional token.
**Save / Ctrl+S** on a remote tab pushes to its associated upload URL. A local
file can also be published through this dialog; its local tab is preserved and
the confirmed remote copy opens separately. **Save as...** writes a local file
and makes that tab a local document. The editor does not register itself as the
system Gemini browser.

For Gemini/Titan, choose **Remote → Connection settings...** before accessing a private capsule.
You can use an anonymous connection, an owner-only PEM private key with its
certificate, or select an existing **Penguin Mail** identity. For Jebot, load
Penguin Mail's default `misfin/state.json` and select `jebot@jebot.dog`. The
editor retains only the identity reference or PEM file paths; private keys
remain in their existing store. Encrypted PEM keys are not supported.

On the first Gemini/Titan connection, review the server name and SHA-256 certificate fingerprint.
A changed certificate needs fresh confirmation. Server hostname and validity
checks remain mandatory. Trust is checked before sending the resource request
or upload. Cross-server redirects are blocked; open the destination explicitly
and choose an identity for that server.

Before pushing, the editor retrieves the read URL and compares its original
bytes with the revision you retrieved. Conflicts stop the upload and offer an
explicit overwrite choice. Identical remote contents are recognized without a
duplicate upload. This is a preflight check, not an atomic server-side lock;
servers need version/transaction support to eliminate simultaneous-write races.

Transfers run in the background. **Remote → Cancel transfer** cancels current
and queued work. Edits made while pushing remain modified after the earlier
snapshot is confirmed. A reload that overlaps new edits opens its result in a
separate tab. **Remote → Reload remote page** retrieves the associated source
again. Closing a tab is blocked while its transfer is running; Save from the
close prompt starts its push, after which the tab can be closed.

Remote edits are included in the existing private recovery copies, including
the read/push URLs and revision fingerprint. Optional Titan tokens are kept
only in memory for the tab; use the push dialog to re-enter them after restart.
Tokens must be printable ASCII without whitespace or semicolons. They and
private-key data are excluded from editor settings and recovery metadata.

Downloads and uploads are limited to 8 MiB. Remote editing accepts UTF-8/ASCII
text, preserves UTF-8 BOM and LF/CRLF/CR, and rejects binary data or a different
declared charset. Failed/unconfirmed uploads leave edits modified. An
unconfirmed push may have arrived, so retrieve before retrying. A successful
same-server publication redirect is verified against the uploaded bytes. The
only automatic retry is one explicit Gemini/Titan status-44 slowdown response with a delay
of at most five seconds; ambiguous failures are never retried automatically.

Remote preferences and certificate pins are stored privately in
`$XDG_CONFIG_HOME/xnedit-xr/remote.json`. The network implementation uses GIO
and OpenSSL 3, plus libcurl for file-oriented transports; the UI remains native
Wayland/FLTK with DomeD's universal menu.

Spartan uses plaintext TCP (port 300 by default), with the same `spartan://`
URL for retrieval and upload. Uploads send the document bytes directly, without
TLS, client identities, MIME parameters or tokens. The push dialog disables the
Titan-only fields for Spartan. Empty documents are rejected before connecting:
a zero-length Spartan request is a retrieval, not an upload. Query-bearing URLs
are rejected in this editing workflow because Spartan interprets query data as
an upload. Same-server path redirects are supported; a publication redirect is
verified by retrieving and comparing its contents. Spartan writes are never
retried automatically. A Spartan client-error response during the preflight
needs explicit overwrite/create confirmation because it cannot distinguish a
missing page from other client errors.

Publishing requires a server-side Titan or Spartan upload handler. The deployed
`jebot.dog` services were checked for retrieval; uploads and the editor workflow
were tested with isolated protocol fixtures without modifying public pages.

### Other remote files and text services

The existing **Remote → Retrieve page…**, **Push document…**, **Reload remote
page** and **Cancel transfer** actions also accept the following URLs. Command-line
URLs open through the same asynchronous workflow. No additional menus are added.

| URL scheme | Retrieve | Push / Save |
| --- | --- | --- |
| `scp://`, `sftp://`, `ssh://` | Remote file over SSH (`ssh` selects SFTP) | Write, then retrieve and verify exact bytes |
| `http://`, `https://` | HTTP GET | HTTP PUT, then GET and verify; the endpoint must support PUT |
| `ftp://`, `ftps://` | FTP download, using binary transfer to preserve text bytes | Upload, then download and verify |
| `finger://` | Finger query, port 79 | Choose another upload URL or Save as locally |
| `gopher://` | Gopher selector, port 70; text/menu framing is removed | Choose another upload URL or Save as locally |
| `gopher+://` | Gopher+ item attributes, port 70 | Choose another upload URL or Save as locally |
| `scroll://` | Scrolltext over TLS, port 5699 | Choose another upload URL or Save as locally |
| `nex://` | Nex path, port 1900 | Choose another upload URL or Save as locally |
| `nc://`, `tcp://` | Netcat-style byte stream; explicit port required | Send the document bytes once; keep edits unsaved |
| `telnet://` | One-shot Telnet text service, port 23, with IAC negotiation | Send text to the service; keep edits unsaved |

Examples:

```sh
xnedit 'sftp://alice@example.org/home/alice/notes.gmi'
xnedit 'scp://alice@example.org:2222/home/alice/notes.txt'
xnedit 'https://example.org/documents/notes.txt'
xnedit 'finger://example.org/alice'
xnedit 'gopher://example.org/0/notes.txt'
xnedit 'nex://example.org/notes.txt'
xnedit 'gopher+://example.org/0/notes.txt'
xnedit 'scroll://example.org/page.scroll'
xnedit 'nc://127.0.0.1:7000/request%0D%0A'
```

Use percent encoding for spaces, Unicode filenames and request controls. For
`nc`, `tcp` and `telnet`, the decoded URL path after its first slash is the exact
retrieval request, with no implicit newline. `/` sends no request. **Push** sends
only the current document bytes, not that retrieval request. TCP half-closes its
outgoing side after writing; Telnet keeps it available for option negotiations.
Telnet applies NVT CRLF/CR-NUL conversion and strips IAC negotiations.
The server must close its response to finish the transfer. Interactive Telnet
login sessions need an interactive terminal. These streams do not define a file
save operation, so the existing Push dialog always asks before sending and a
successful send never clears modified/recovery state. Finger accepts `/alice` or
`alice@host` URL user information and sends a CRLF-terminated query.

SSH transfers use libcurl's SSH support, your SSH agent or the first available
`~/.ssh/id_ed25519`, `id_ecdsa` or `id_rsa`, and OpenSSH-format
`~/.ssh/known_hosts`. Unknown or mismatching host keys stop the transfer; establish
host trust with `ssh` first. Agent-backed keys can remain encrypted. The URL names
the host, username and port explicitly; libcurl does not read OpenSSH host aliases,
ProxyJump or other `~/.ssh/config` settings. HTTP/FTP credentials may come from a
private `~/.netrc`. Passwords in URLs are rejected and no new credential store is
created. HTTPS and FTPS retain normal CA/hostname verification. FTPS uses implicit
TLS (port 990 by default). Connection settings in the existing menu explain
these authentication sources when a non-Gemini remote tab is selected.

File saves compare the destination bytes with the retrieval baseline before
writing. A changed or existing destination without a matching baseline needs the
existing overwrite confirmation; creating a missing destination also needs
confirmation. The uploaded bytes are retrieved again before marking a document
saved. A failed or unverifiable write keeps edits modified. Preflight checking is
not an atomic server-side compare-and-swap, and these file uploads are not atomic
replacements: a failed upload can leave partial remote contents. No failed write
is retried automatically. Edits made while saving retain their dirty state after
the earlier snapshot is verified.

The shared 8 MiB limit, 20-second timeout, Cancel action, UTF-8 text validation,
BOM/line-ending preservation, private recovery and Save as local copy apply to
these transports. File retrieval also accepts structured text MIME types such as
JSON and XML. HTTP redirects are returned for explicit review, not followed.
Scroll uses the existing TOFU/client-identity settings. Its request includes a
BCP47 language list derived from the desktop locale, falling back to `en`. Its
author and publication/modification header lines are parsed separately from the
editable body. Same-server redirects work; other destinations require explicit
retrieval. A `text/scroll` response selects Scroll syntax even without a filename
extension. Scroll is retrieval-only here; choose a Titan or other upload endpoint
to publish edited text.

`gopher+://` requests item attributes using `TAB !`. Byte-count, period and EOF
Gopher+ framing is checked and removed; attribute text selects the Gopher+ mode.
Standard `gopher://` URLs containing an encoded Gopher+ command, such as
`gopher://host/0/item%09!`, also work. Gopher+ is retrieval-only here. The Scroll
and Gopher+ handlers follow the linked specifications in the syntax section.

Raw stream request URLs are kept in the same private remote settings/recovery
metadata as other remote URLs. All six supported smallnet document formats select
their syntax modes from their response type or filename.

Building these handlers requires `libcurl` development files, with SCP/SFTP
support enabled for SSH transfers. The implementations use native sockets/GIO and
libcurl; no local shell command is formed from document contents or URLs.
Transport references: [libcurl upload](https://curl.se/libcurl/c/CURLOPT_UPLOAD.html),
[SSH host verification](https://curl.se/libcurl/c/CURLOPT_SSH_KNOWNHOSTS.html),
[Finger RFC 1288](https://www.rfc-editor.org/rfc/rfc1288), and
[Telnet RFC 854](https://www.rfc-editor.org/rfc/rfc854).

## Syntax highlighting

Language detection uses the filename, extension and interpreter shebang, including
extensionless scripts. Choose **Document → Language** to override it for the current
tab, or choose **Plain text**. **Automatic** follows Save As filename changes. The
status bar shows the detected/selected language. **View → Syntax highlighting**
turns colouring off without changing the document.

Modes cover C/C++, Rust, Python, JavaScript/TypeScript, Go, Java, C#, shell, Lua,
SQL, JSON, YAML, TOML, INI/systemd/desktop files, HTML, XML, CSS, Markdown,
Makefiles, CMake, unified diffs, Gemini, Gopher, Gopher+, Nex, Spartan and Scroll.
Unknown files use plain text. Highlighting
separates comments, strings, keywords, numbers, types, functions, operators,
directives, keys/tags, headings, variables and diff additions/deletions. It handles
multiline/triple/raw strings, nested Rust comments and lifetimes, Lua/CMake long
brackets, shell heredocs, YAML block scalars, JavaScript regex literals, HTML
script/style content and fenced Markdown code. It remains lexical highlighting,
not a language-server or semantic parser.

The palette follows DomeD light/dark appearance and retains the selected bitmap
font. Both split panes share the same UTF-8 byte-aligned style buffer. Only the
changed style range is replaced; files larger than 4 MiB remain editable with
plain styling to bound interactive highlighting work.

### Small Internet documents

Select each mode under **Document → Language**. Automatic detection uses:

| Mode | Filenames and extensions |
| --- | --- |
| Gemini (Gemtext) | `.gmi`, `.gemini`, `.gemtext` |
| Gopher (gophermap) | `gophermap`, `.gophermap`, `.gph`, `.gopher` |
| Gopher+ | `gophermap+`, `gopherplus`, `.gopher+`, `.gopherplus`, `.gophermap+`; Gopher maps containing a `+INFO:` line |
| Nex | `.nex`; extensionless `index` containing a `=> ` link line |
| Spartan | `.spartan`; Gemtext files containing an unfenced `=:` prompt line |
| Scroll (Scrolltext) | `.scroll`, `.scrolltext` |

The extra suffixes are editor conveniences, not required protocol extensions.
Detection probes at most the first 64 KiB for shared formats. A `.gmi` document
without a prompt uses Gemini highlighting; Spartan's other line types are the
same. Plain `.txt` documents remain plain; choose a language manually when their
role cannot be inferred from the filename. Save As follows the new filename in
Automatic mode.

Gemtext highlights headings, links and their targets/labels, list markers,
quotes and fenced literal text. Spartan also highlights input prompts. Nex
highlights only its directory link syntax, preserving ordinary text and ASCII
art. Gopher highlights tab-separated types, labels, selectors, hosts, ports and
Gopher+ flags, plus Gophernicus comments and map directives. Gopher+ adds attribute
blocks such as `+INFO:`, `+ADMIN:`, `+VIEWS:` and `+ASK:`, with form-field names.
Scroll adds five heading levels, nested list/quote markers, ordered list markers,
input links, link relations, thematic breaks, line escapes and Unicode-aware
line-bounded strong/emphasis/code toggles. Fenced contents stay literal.
All modes reuse DomeD's existing syntax palette and bitmap font.

Format references: [Gemtext specification](https://geminiprotocol.net/docs/gemtext-specification.gmi),
[Gopher RFC 1436](https://www.rfc-editor.org/rfc/rfc1436),
[Gophernicus map syntax](https://github.com/gophernicus/gophernicus/blob/master/README.gophermap),
[Gopher+ memo](https://github.com/gopher-protocol/gopher-plus/blob/master/gopherplus.txt),
[Nex specification](https://nightfall.city/nex/info/specification.txt) and
[authoring guide](https://nightfall.city/nex/info/station-guide.txt),
[Spartan specification](https://github.com/michael-lazar/spartan/blob/main/public/specification.gmi),
and the [archived Scroll speculative specification](https://github.com/michael-lazar/smolnet-portal/blob/master/docs/scroll_spec.txt)
by Christian Lee Seibold (modification date 2024-08-03). Scroll's link relations
remain marked tentative in that specification.

## Find, replace and regular expressions

**Ctrl+F** opens Find, **Ctrl+H** opens Replace, **F3** finds the next match and
**Shift+F3** finds the previous one. The search panel supports literal/regex mode,
case sensitivity, replacement of the selected match and **Replace all**. Searches
wrap with status feedback; regex errors stay in the search status. Repeated
zero-width matches advance at UTF-8 character boundaries. Replace all is one
undoable edit, and read-only documents cannot be replaced.

Open **Search → Regex helper...** or click **Regex help** in the search panel.
The native FLTK helper contains 19 examples, an editable sample, live validation
and first-match/replacement preview. **Insert pattern** adds a token at the pattern
cursor; **Load example** loads its pattern, sample and replacement. **Use in
find/replace** transfers the fields into the editor's search panel with regex
mode enabled. No document is modified by the helper.

The helper uses XNEdit's actual expression engine: `<word>` matches a whole word,
`\d` matches digits, `\s` matches horizontal whitespace and `\n` matches a line
break. In replacements, `&` inserts the whole match, `\1` through `\9` insert
capture groups, and `\U\1` / `\L\1` change a captured group's case. `$1` is literal
text. Lookaround and non-capturing groups are available; lookbehind must have
bounded length. Each example is checked against the same engine used for editing.

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

CTest covers the document model, language lexers, search/replacement semantics,
and real TLS Gemini/Titan transfers with trust, identity, conflict, cancellation
and interrupted-publication tests.
The first UI test uses a private real DomeD compositor and verifies mandatory
Wayland, menu association and actual keyboard editing/saving. Its native editor
fixture also checks split-pane highlighting, language changes, the regex helper,
replacement callbacks and viewport behavior, with light/dark GPU captures. The opt-in X11
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
