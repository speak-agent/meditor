# Runtime directory and child environment services

`meditor.runtime` is a named module in the application package. It resolves
installation and data locations without creating files. A desktop portable
descriptor is recognized beside the executable, or at the package root when
the executable directory is named `bin`:

```json
{"schemaVersion":1,"dataDirectory":"data"}
```

Markers in both locations are ambiguous and rejected. Unknown schemas/fields,
absolute paths and traversal are rejected. Portable data must remain inside the
installation root; canonical path checks reject data entries that resolve out of
their boundary. An explicit installed data root may live elsewhere. Without an
override, installed data uses the platform user configuration directory.

Portable mode and an explicit `--data-root` separate `settings.json`, `mcpp/`,
`cache/mcppls/` and `temp/` under the selected data root. The installed default
keeps settings under the platform configuration directory and tool state under
the platform cache directory: XDG cache on Linux, Library/Caches on macOS and
local application data on Windows. Configuration and cache paths resolve
independently; Windows reads both through the wide-character environment API. Preparing tool directories is an explicit operation. The environment
mapping sets `MCPP_HOME`, `MCPPLS_CACHE_DIR`, `TMPDIR`, `TEMP` and `TMP` for the
selected root. Environment path values use UTF-8, including on Windows.

`Process::start` accepts per-child environment overrides and removals. Its default
behavior continues to inherit the environment. Explicit overrides require an
absolute executable and reject malformed names/NUL values. Windows builds a
Unicode environment block with case-insensitive keys; POSIX builds the environment
before fork and passes it to execve. Neither implementation changes parent
environment variables. Executable argument quoting and environment values remain
separate operations.

Tests cover actual child override/removal, parent preservation, invalid inputs,
portable marker validation, relocation of settings data, data confinement and
explicit installed roots. The application tests are included in native CI.

GUI and CLI now share this layout through the application core. `--data-root DIR`
selects its data root; `--portable` requests portable mode, and a valid
`portable.json` also selects it automatically. Settings use this root unless an
explicit store is selected; a portable settings override must remain inside its
data directory. Android GUI obtains application-private paths from HuxerUI.
`doctor` reports the mode and that tool homes are managed, without printing the
machine paths. Read-only doctor/configuration queries do not create the data root.

Build tasks and the mcppls process receive the same managed tool environment;
mcppls passes the selected mcpp driver and inherits its home. Fixtures record
these values in isolated test receipts. The CLI regression also copies a
development executable into a marked test layout, preserves settings across a
directory move, verifies the new build home and rejects traversal. This is a
same-machine configuration probe, not a clean-machine binary-relocation test.

The real pinned-backend Linux regression has passed Modules build/run/test and
LSP symbols/diagnostics/graph under the new installed data root. The local probe
counted about 1.50 GB of logical build-home data and 0.52 MB of language-cache
files after preparation, with no links escaping that data root. These include
prepared tools/caches, not just the GUI binary; they are observations for this
development run, not release size/performance acceptance or proof of every I/O
boundary. CI retains the managed build home alongside its build-tool cache.

Required work still includes portable archive assembly, complete file-write
observation, read-only/storage-failure UX, native loader/library/resource
relocation, cache invalidation after relocation, update/recovery and clean-machine
installation evidence. Copying a development binary or passing these tests does
not prove portable product delivery.

The Linux GUI keyboard regression passes on a private Xvfb display with the
managed layout. The active desktop-session probe lost X11 keyboard focus before
typing and remains unverified there. The test now checks focus before each key,
stops injection on focus loss, releases its pressed keys during cleanup and
captures a failure screenshot when requested. This isolates automated window
interaction from the desktop session; it does not certify native IME behavior.

The first installed-home integration run passed Linux, Windows and both Android
compile jobs, but failed the macOS real-backend build. Its default configuration
path included `Application Support`; mcpp 2026.10.21.1 emitted an unquoted scanner
program path. A fresh Linux tool home with spaces reproduced the same failure.
The fresh Linux real-backend regression passes with the new installed cache
layout, including Modules build/run/test and LSP symbols/diagnostics/graph.
Installed tool state now uses the platform cache location, while settings keep
the configuration location. This also prevents large compiler caches from being
stored in roaming configuration on Windows. Explicit and portable roots remain
self-contained, with no fallback write outside their selected boundary.

**Open release requirement:** the pinned mcpp does not yet build reliably from
tool homes containing spaces. The editor must not rewrite generated Ninja files,
patch tool binaries or silently redirect portable state outside its package.
Installation paths, user names and custom roots containing spaces remain a
real-backend compatibility gate pending an upstream-compatible solution and
retest. Configuration-only portable relocation tests do not cover this defect.
