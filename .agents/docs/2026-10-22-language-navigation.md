# Language position mapping

LSP ranges address UTF-16 line/character positions in the raw text sent to the
language server. The editor displays BOM-free text with CRLF normalized to LF.
Navigation must convert between these representations before selecting text.

`utf8_offset_at` resolves positions against a UTF-8 string without constructing a
temporary `Document`. `Document::offset_at` uses the same implementation.
`TextPresentation::display_offset` maps raw byte boundaries back to the display,
using a binary search over recorded CRLF pairs. The beginning of a hidden BOM and
the boundary after it both map to display offset zero. UTF-8 continuation bytes,
out-of-range offsets and the interior of a CRLF pair are rejected.

`EditorState::select_lsp_range` refreshes the document snapshot before checking the
captured revision. It rejects stale revisions, invalid UTF-16 positions and
reversed ranges. Successful navigation updates the existing selection and caret
reveal state without editing or saving the document. Callers must also check the
query's workspace generation and stale/version metadata before navigation.

Tests cover mixed LF/CRLF round trips, BOM aliases, Chinese and emoji boundaries,
surrogate splits, reversed ranges, external unsaved core edits and unchanged disk
contents. The standalone source/packed document consumer exercises both new
public mapping functions. These tests prove coordinate and selection behavior;
they do not prove GUI diagnostic/symbol panels or native keyboard focus transfer.

Remaining work includes the asynchronous Problems/Outline UI, focus transfer,
workspace-owned URI resolution, unversioned diagnostic presentation and native
platform interaction evidence. No full A01–A13 product criterion is completed by
this mapping layer alone.
