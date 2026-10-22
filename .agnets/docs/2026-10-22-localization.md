# Localization implementation

`meditor.i18n` lives in the application package as a reusable named module. Its
interface and implementation are separate, with no GUI, environment or tool
process dependency. Locale choice belongs to each `Localizer` value; only the
immutable built-in catalogs are shared. This implementation was written for
meditor and does not copy xlings source.

English and Simplified Chinese messages are maintained in `resources/i18n/`.
The application build hook uses `meditor.build.catalogs` from `mcpp/distribution`
to validate and embed them as a generated Modules resource. Missing/different
keys, empty values, invalid identifiers and unsupported control characters fail
generation. Inputs are declared to mcpp; unchanged output is not rewritten. No
translation files or scripting interpreter are required at runtime. Public-source
CI also checks matching format arguments and missing literal UI/CLI references.

Locale inputs normalize common tags, underscores, encoding and variant suffixes.
`auto` follows a supplied system/resource locale; an explicit choice takes
priority. Unsupported languages fall back to English; Chinese language variants
use the Simplified Chinese catalog. Missing/empty translated messages fall back
to English; unknown IDs remain visible as IDs. Named `{arguments}` are inserted
literally, with `{{` and `}}` for braces. Missing, unused and malformed arguments
are rejected. This initial formatter does not implement pluralization.

Settings expose `language: auto|en|zh`, initially `auto`. Schema-1 stores without
this field are upgraded in memory without changing their file or revision.
Explicit updates retain the existing revision/lock/atomic-write protocol. GUI
settings provide Automatic, English and 简体中文. Existing workbench labels and
status summaries refresh without recreating application documents or tasks.
Backend output, filenames, source text and protocol identifiers stay unchanged.
The settings panel also handles the normal Back event, including when selecting
a language disables the previously focused button.

`meditor --language TAG` temporarily overrides human-facing help and error
summaries; it does not update preferences. Without an override the CLI uses the
stored preference and OS language (Windows locale API, POSIX LC_ALL/LC_MESSAGES/
LANG). Machine JSON and JSONL responses retain stable commands, fields and error
codes/messages. Use `--format json` for detailed structured error information.

Tests cover independent contexts, normalization, fallback, named formatting,
legacy/no-op settings reads, invalid-setting preservation, generated resource
validation, CLI overrides and stable machine errors. Headless loaded-workspace
tests exercise English/Chinese at three sizes, scrolling to language controls,
explicit/automatic precedence, changes to the supplied resource locale, unsaved
text, caret retention through keyboard refocus and undo/redo after switching.
Native Linux keyboard editing has passed with each preference after explicitly
building the GUI executable. The Chinese desktop screenshot was inspected for
visible text and clipping; the test captures synthetic screenshots; screenshot review is separate from key-event assertions.

Remaining work includes preference-driven localization of framework caption and
platform dialog labels, native CJK font/fallback coverage on every required
platform, and integrating the same messages into the pending tutorial, command
panel and About UI. Framework captions currently follow the framework resource
locale. These tests do not certify native CJK IME or the full product acceptance
matrix.
