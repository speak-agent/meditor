# Build tooling

This directory contains meditor's mcpp build plugins, rules, build configuration
and dependencies used only during the build. Editor runtime components live in
`modules/`.

`distribution/` validates complete tool-payload inventories and produces their
placement plans. The root build program consumes it through
`[build-dependencies]`. Build plugins execute on the build host, including when
building Android or another target; they are not editor runtime dependencies.
