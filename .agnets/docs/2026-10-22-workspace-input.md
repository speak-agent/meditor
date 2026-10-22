# Workspace input regression

The headless workbench test opens an actual synthetic `.cppm` document through
startup configuration and exercises the mounted editor, application document
service and disk save path. It commits the queued startup lifecycle before
interaction. At 390×844, 800×600 and 1200×800 it checks both touch and mouse input:

- Hide the explorer on compact screens, then tap the actual editor surface.
- Require an active input session, select all and commit Chinese/emoji UTF-8 text.
- Verify typing leaves the disk unchanged until the save shortcut.
- Save, undo/save and redo/save through routed shortcuts and verify file contents.
- Open settings, require the editor session to end, return, refocus and save again.

The editor pane now has a stable reconciliation key. Previously, omitting the
explorer left an empty view before an unkeyed pane; refreshing selection remounted
the pane and ended its input session. This regression failed before the key was
added and passes with the fix. The editor surface also has a stable selector key.

These checks cover view routing and application state. Native composition,
accessibility, grapheme behavior, touch scrolling/selection handles and physical
phone input still require their separate acceptance evidence. They are not
certified by synthetic text commits or by the native X11 Latin keyboard test.
