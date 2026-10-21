# Native tool inputs

`tools.json` fixes the complete upstream mcpp and mcppls release archives by SHA-256, including their helpers, libraries, semantic kit, resources and licenses. Preparation requires Python 3.13; the installed editor does not require Python.

```sh
python3 scripts/prepare_payloads.py --target x86_64-linux-gnu
mcpp build --features test-support
python3 scripts/run_cli_tests.py --native-tools
mcpp build -p meditor-gui
```

Other native payload targets are `aarch64-linux-gnu`, `aarch64-macos` and `x86_64-windows-msvc`. A prepared tree is stored under `target/payloads/<target>`. The inventory contains only relative entries and source URLs; preparation verifies the pinned archive and subsequent builds verify every file against the inventory. A changed tree is rejected; prepare a new directory when updating inputs.

The root `build.mcpp` uses the independently testable `meditor-distribution` placement planner and `mcpp::deploy`. It places the tools beside the CLI. The GUI declares the matching CLI as an mcpp dependency artifact and inherits its runtime tool placements. mcpp remains responsible for platform bundles, runtime closure and installer formats. Test fixtures require `test-support` and are absent from ordinary product builds.

A development build without prepared tools is allowed and reports their absence through `meditor doctor`. Native packaging requires prepared tools. The current Linux directory-package probe found that the dependency CLI artifact retains development interpreter/RPATH values; relocation on the build machine is insufficient evidence until this is corrected and tested without those paths. Preparing a payload does not certify an installer: clean-machine runtime, signatures, upgrade/uninstall, source examples and artifact privacy remain required release checks.

Android, iOS and Web do not use these native desktop archives as executable tools. Their verified host/bridge strategy is still required. The Android APK startup probe is separate from native desktop tool delivery. Tool supply through xim-pkgindex and the default source example archives remain implementation work; this preparation does not replace the index or create another package manager.
