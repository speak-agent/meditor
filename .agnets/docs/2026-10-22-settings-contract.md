# Local settings contract

The application core exposes `settings.get` and `settings.update` to both frontends. The initial settings are `theme: "dark"` and `reducedMotion: false`. Theme accepts `dark` or `light`; unknown settings and invalid values are rejected before writing.

```json
{"command":"settings.update","params":{"revision":0,"changes":{"theme":"light","reducedMotion":true}}}
```

Results contain a monotonically increasing `revision` and a `values` object. An update checks the supplied revision while holding an exclusive native file lock, then uses atomic file replacement. A competing writer receives a conflict or a retryable operation failure; it cannot silently overwrite the successful update. No-op updates retain the revision. Locks are released by the OS when their process exits; the lock file remains to preserve its identity.

`settings-conflict` requires reloading before retrying. Invalid or unsupported configuration returns `invalid-settings` and preserves the original file. Files are limited to 64 KiB. Other I/O failures leave the GUI's last adopted values in place and report failure; saving is never reported successful on a failed write. Atomic replacement does not by itself certify power-loss durability.

Desktop defaults use the platform user configuration directory: Windows roaming application data, macOS Application Support, or Linux XDG configuration with the home configuration fallback. Windows obtains the environment path with the wide-character API. Android GUI storage uses the HuxerUI application-private data directory. Browser persistent storage and the phone host CLI bridge still need their own verified adapters.

`--settings-file FILE` selects an explicit store for the GUI or CLI; automated tests always use isolated synthetic stores. Results omit the file's absolute path. Opening the GUI settings panel reloads preferences; conflicting edits reload the current values without overwriting them. This store does not implement GUI/CLI document-session IPC or continuous cross-process settings notifications.

The bottom Settings entry is available on welcome and workspace screens. Theme changes use the existing short transition; reduced motion bypasses that transition and is inherited by descendant theme controls. The panel supports scrolling, its Back action, the bottom toggle and Escape. More settings, i18n and the command-panel entry remain separate implementation work.
