# meditor

A lightweight modern editor for mcpp and C++ Modules, built with C++23 Modules and HuxerUI.

GUI and CLI share one application core. Linux, Windows, Android and macOS are the required product targets.

Version 0.0.1 is under development in [PR #1](https://github.com/speak-agent/meditor/pull/1), on `release-0.0.1`.

Build the current CLI with [mcpp](https://github.com/mcpp-community/mcpp):

```sh
mcpp build
mcpp run -p meditor-gui -- --workspace .
```

[Design and acceptance criteria](.agents/docs/2026-10-22-meditor-project-design.md) · [Development rules](AGENTS.md)
