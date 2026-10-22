# Offline About and license records

`meditor about --format json` and `meditor licenses --format json` use the same
application module as the GUI About entry. Human `meditor about` shows a concise,
localized summary. The GUI entry is currently in the bottom bar, on both welcome
and workspace screens. It opens product details and a scrollable dependency list;
license buttons open selectable, read-only original text. Back/Escape return
through the license and About views. Public URLs are selectable text; no browser,
network request or tool process is started by these queries.

The build-only `meditor.build.metadata` module reads an explicit set of product,
lockfile, license and tool-specification inputs. It embeds a named Modules
resource into the application package. Product version comes from mcpp's package
version. `MEDITOR_SOURCE_COMMIT`, when provided, must be a lowercase 40-character
commit ID; CI supplies its checkout SHA. Without it, corresponding source revision
and URL remain null and the UI says so. A repository homepage is not presented as
a verified corresponding-source archive.

The product license/contact/contributors are checked against LICENSE. Framework
notices must match HuxerUI's resolved `meditor` commit; JSON notices must match the
locked version. Imported MIT texts are preserved byte-for-byte with provenance
and SHA-256 in `resources/licenses/manifest.json`; public-source CI verifies the
hashes and pins. Git attributes preserve raw upstream notice bytes and exempt their original
whitespace from source-style checks. Project/license materials and the two direct
library notices are also staged beside the CLI/GUI, including mobile GUI builds.
Native wrapper tests compare deployed files with source bytes.

Tool versions are reported from a matching pinned bundle inventory, with readable
license files, rather than inferred from executable names or the mcpp version.
A missing binary has state `missing`; a binary without matching source records is
`unverified`; both have null actual version and a separate expectedVersion. A
matching record is labeled `recorded`. The query does not rehash or execute the
binary: `integrity: not-rechecked` makes that limit explicit. Executable/notice
paths must stay within the selected tool tree to use its records. Build-time
payload verification remains a separate operation.

**This is a direct-dependency record, not the complete distribution declaration.**
`completeDistributionNotices`, `licenses.complete` and `releaseReady` remain false.
Nested xlings/clangd inventories, transitive runtime notices, corresponding source
and build-material assembly still need work. Apple builds explicitly expose the
additional libc++ entry with unresolved version/notice material rather than
inventing a resolved version. These missing records must be completed before
release; the product acceptance gate remains unproven.

Tests cover matching and mismatched receipts, missing/unverified tools, offline
queries without child processes, stale source/notice pins, unknown/private input
fields and metadata without developer paths. Headless tests read GPL and MIT text
in English/Chinese at three sizes; workspace tests preserve unsaved text/history
across About navigation. Real backend tests check recorded tool versions and
available license material. The native Linux Chinese About page was also opened and visually reviewed; its
reading area is centered and limited to 800 logical pixels, with content-sized
license buttons. Final-package, complete native/device and all-platform notice
closure checks remain separate acceptance evidence.

Still pending: the planned Help menu/command-panel hierarchy, clickable public
link handling, complete per-platform software inventories, corresponding source
bundles and release packaging gates tied to those complete inventories. The
current About implementation does not certify product release readiness.
