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

## Candidate source and privacy checks

```sh
python3 scripts/prepare_candidate.py --target x86_64-linux-gnu
python3 scripts/check_artifact_privacy.py --directory <candidate-package-directory>
```

Candidate preparation exports the selected committed Git tree into the ignored `target/candidates` directory, verifies its pinned tool payload and copies that payload into the candidate workspace. It generates graph-wide compiler path mappings for application code, dependency compilation and the standard-library BMI. Only this local generated manifest contains the build paths. The adjacent candidate receipt records the source commit and manifest/lockfile hashes without recording those paths. Existing candidates are never overwritten; uncommitted work is deliberately excluded.

Build and package from the exported `source` workspace with mcpp, then scan the actual resulting package directory before sharing it. The scanner checks current machine identities, personal build paths in UTF-8/UTF-16LE and escaping symlinks. It reports relative filenames and failure categories without printing matched private values. This is one release check, not a certificate that an artifact contains no other sensitive data.

The current release-mode Linux GUI probe maps application source paths successfully but still fails the artifact scan: the staged CLI retains its loader/RPATH, and some bundled libraries and upstream tool binaries retain build paths. No candidate is approved for publication. Compiler mappings do not rewrite prebuilt dependencies; these failures require proper runtime packaging or corrected upstream inputs. Do not alter licenses, remove the affected tools, or relax the check to obtain a passing result.

Android, iOS and Web do not use these native desktop archives as executable tools. Their verified host/bridge strategy is still required. The Android APK startup probe is separate from native desktop tool delivery. Tool supply through xim-pkgindex and the default source example archives remain implementation work; this preparation does not replace the index or create another package manager.
