# Shared UI sources

The FLTK appearance, BDF discovery/scaling and GPU widget hooks are from the
MIT-licensed de-xr DomeD/qimgvr projects, retaining their SPDX headers.
The integer bitmap enlargement follows Francesco149/bdf2x (Unlicense), credited
in bdf_font.hpp. The StatusNotifier/DBusMenu window association code is from the
same suite and retains its MIT notice. These hooks use the separately installed
patched FLTK library; no outline fonts or toolkit binaries are vendored here.

The native editor frontend and document integration are GPL-2.0-or-later.
The original XNEdit/NEdit sources keep their existing notices. See LICENSE and
README-UPSTREAM.md for upstream licensing, including Henry Spencer's regex terms.
