# Problems and Outline panels

The workbench now exposes nonmodal Problems and Outline panels for the active
C++/Modules document. Both use `language.query` and poll the shared application
through the view's task scope. Startup and RPC waits remain in background workers.
The UI offers refresh, cancel, close, empty/error/stale states and source range
selection. Ctrl/Cmd+Shift+O opens Outline, Ctrl/Cmd+Shift+M opens Problems, and Escape closes the panel and returns focus to the editor. Labels and feedback are available in English and Chinese.

Queries capture the application, workspace generation, document revision and a
local request sequence. Switching documents, replacing the workspace or closing
the panel invalidates continuations. Edits invalidate displayed results and
cancel pending queries on the next poll. A cancelled or replaced request cannot
populate a newer panel. Refresh always captures the current unsaved text.

Problems shows diagnostic severity, message and source position. Diagnostics
without confirmation of the requested LSP version remain visible with an explicit
warning and disabled navigation. Outline traverses DocumentSymbol children, with
selectionRange preferred over range. The client advertises hierarchical document-symbol support; pinned native-tool tests require a navigable function symbol after indexing, rather than accepting any array as evidence. SymbolInformation locations await safe URI
resolution and do not navigate. The panel caps output at 200 rows and 32 nested
levels, marks truncation and bounds individual labels. Invalid server data and
invalid source ranges produce explicit feedback rather than editing a document.

Navigation uses the revision-bound UTF-16/raw/display mapping described in
[language navigation](2026-10-22-language-navigation.md). Successful navigation
requests real keyboard/text-input focus on the mounted code editor. The editor
clears its borrowed node reference on replacement/unmount. The generic
`ViewNode::RequestFocus` implementation belongs to HuxerUI's `meditor` branch
(`0e479bf26f90a6560acb8f1b4fb542a4dda72ca8`); it respects effective enabled state,
focusability and the active focus trap. Its complete runtime suite passed 921
cases and 30,097 assertions. The generated public module shell was checked.

GUI tests use the shared `meditor.test.fixture` test-only Modules package. The
headless test executable serves as a native fake LSP child when launched with
tool arguments, exercising actual process transport and protocol messages.
The same fixture powers CLI protocol tests through the feature-gated helper.
Production builds do not depend on this test package. Tests cover trust errors,
English/Chinese desktop/phone layouts, BOM/CRLF selection, input focus, unsaved
edits, stale results, explicit cancellation with late replies, automatic
cancellation after editing, unversioned diagnostics, malformed/empty results,
row limits and editor unmounting.

`python scripts/update_gui_dependency.py` independently configures the current
GUI branch, imports the mcpp-generated lock record and verifies upstream license
bytes before synchronizing the direct notice revision. This avoids the circular
dependency between a removed lock entry and About metadata generation. A changed
upstream license requires review. The manifest continues to reference the branch;
the lock and notice inventory record what was actually resolved.

These panels are an initial usable language UI, not full language/product
acceptance. Further work includes completion/hover/signatures, definitions and
references with safe workspace URI handling, interactive Modules graphs,
automatic refresh/debounce, richer issue details and accessible presentation,
production styling/animation, real-backend native GUI coverage and the four
platforms' complete A01–A13 evidence. Native mobile tool hosting remains separate
from headless phone-size layout tests. Cross-process GUI/CLI sessions and recovery
also remain in scope.
