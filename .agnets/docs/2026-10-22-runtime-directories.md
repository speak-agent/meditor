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

The layout separates `settings.json`, `mcpp/`, `cache/mcppls/` and `temp/` under
the data root. Preparing tool directories is an explicit operation. The environment
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

These services are foundations; the frontends and task/LSP launch sites have not
yet adopted this layout. No new CLI option or portable archive is advertised as
implemented. Required work includes shared frontend configuration, task/LSP home
propagation, real pinned-tool cache/temporary-write verification, read-only and
storage-failure UX, native bundle relocation, update/recovery and clean-machine
installation evidence. Copying a development binary or passing these unit tests
does not prove portable product delivery.
