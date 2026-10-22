# Definition and reference navigation

The workbench now offers Definition and References actions, including F12 and
Shift+F12. Results appear in the existing nonmodal language panel. Selecting a
workspace location opens or reuses its document, selects the returned range and
returns keyboard/text-input focus after the editor mounts. A reference list
remains usable while navigating between its captured files. Editing a captured
document invalidates the list. Results do not implicitly save files.

The shared GUI/CLI service adds the following operations. Query IDs belong to a
persistent `meditor serve --stdio` session or GUI application core; they are not
identifiers shared by independent one-shot processes.

| Interface | Behavior |
| --- | --- |
| `language.query` with `definition` / `references` | Captures and synchronizes opened C++ documents; also captures other opened text for target reuse |
| `language.result` | Adds normalized `locations` and `locationsTruncated` alongside the compatible backend result |
| `language.result` with `includeResult: false` | Returns query state and stale metadata without copying result payloads |
| `language.navigate` with `query` and `location` index | Validates the completed query and exact target text, then opens/reuses the target and returns its summary and range |
| `language.release` with `query` | Releases a terminal query and its retained payload; running queries must first finish or be cancelled |

Locations expose workspace-relative paths and validated UTF-16 ranges, never the
private target-text cache. Unsupported or outside-workspace locations are marked
unavailable without exporting their original absolute path in the normalized
entry. The original backend result continues through the existing redactor.
Location and LocationLink responses are supported. Invalid ranges, malformed
URI escapes, remote authorities, query/fragment suffixes and escaping symlinks
are rejected. Windows stream paths are rejected; directory identity checks
support case aliases without accepting different case-sensitive directories.
Known macOS `/var` versus `/private/var` temporary-path aliases are normalized.

Target text is captured after receiving the backend response. Opened documents
use their captured unsaved buffers; unopened targets use bounded file reads.
Navigation compares the complete current buffer or file bytes with that capture,
rather than relying on byte counts or timestamps. Opening a target extends only
the owning query's document context, allowing further navigation within the same
result list. A rejected navigation does not open a replacement document or
overwrite a buffer. A successful new open reports a `document-open` effect.

Capture is bounded to 200 locations, 32 target files and 64 MiB of target text
per query. Retained result accounting includes normalized entries and target
text, within the existing shared 64 MiB retained payload budget. These are
logical data budgets, not a total process heap guarantee. Cancelling an active
query discards partial capture; releasing a terminal query, retention pruning,
service stop and workspace replacement release retained data. The GUI releases
its previous completed result on refresh, close or context replacement. Backend
initialization/RPC/capture waits run outside the editing thread;
opening a target still performs synchronous storage validation in the shared
application service.

Validation covers:

- URI containment, Unicode/percent encoding, valid and invalid ranges, symlink
  escape rejection where symlink creation is supported, cancellation and limits.
- Native CLI/LSP fixtures reject target edits even when size and timestamp are
  unchanged, preserve unsaved target buffers, support repeated navigation and
  invalidate stopped query IDs. Lightweight status omits payloads.
- Actual GUI trees and attributed paint verify English/Chinese desktop/phone
  layouts, cross-file mounting and focus, reference-list reuse, unsaved edits and
  stale unopened targets without injecting application state.
- Pinned real mcpp/mcppls tests require navigable workspace definition/reference
  results and open them through `meditor`; public source and packed document
  consumers exercise the reverse UTF-16 position helper.

This is workspace navigation, not full A05 or four-platform acceptance.
Read-only dependency/SDK source access through explicit source capabilities,
navigation history, completion/hover/signatures, automatic invalidation from
filesystem watching, cancellable storage I/O, performance budgets and native
device interaction remain in scope. Capturing target bytes does not prove which
on-disk version the server indexed before its response. Canonical checks and
content comparison also do not provide an operating-system filesystem sandbox
against concurrent external path replacement. GUI/CLI cross-process sessions,
recovery, interactive Modules graphs and the remaining product requirements are
unchanged.
