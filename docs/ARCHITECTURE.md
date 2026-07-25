# Logdor Architecture

Logdor follows **Functional Core, Imperative Shell**: everything that parses,
indexes, filters, sorts, or anchors lives in `core/` (`logdor-core`), a
static library that links **QtCore and QtConcurrent only - never
QtGui/QtWidgets**. The Qt Widgets application in `app/` + `plugins/` is a
thin shell that translates gestures into core calls and core results into
pixels. A future TUI reuses the core unchanged.

The no-GUI rule is enforced mechanically:
- a configure-time check of `logdor-core`'s link list,
- a CTest that `ldd`s a link-guard binary for `libQt6(Gui|Widgets|Qml|Quick)`,
- a CTest that greps core sources for GUI includes.

Core also has a **no-mutable-globals rule**: it is statically linked into
the app *and* every plugin, so a singleton would exist once per module.
Registries are value-returning free functions.

## Core layers (`core/include/logdor/`)

| Layer | Types | Role |
|---|---|---|
| Bytes | `FileSource` | mmap-first read-only file owner; buffered 4 MiB LRU fallback when mapping fails; `shared_ptr` lifetime so cancelled background work can outlive a file switch |
| Lines | `LineIndex`, `buildLineIndex` | block-delta line offsets (~4 B/line); cancellable off-thread scan with permille progress |
| Parsing | `FormatParser`, `PlainTextParser`/`LogcatParser`/`ClfParser`/`CsvParser`/`JsonLinesParser`/`DockerJsonParser`/`GelfParser`, `DeclarativeParser` + `FormatSpec`, `FormatRegistry` | schema + stateless thread-safe per-line parse; JSON-defined formats; sample-scored auto-detection |
| Filtering | `RowSet`, `scanFilter`, `CompiledQuery`, `ColumnScan`/`ColumnCache`, `TimestampParse` | chunk-parallel cancellable scans; field-query language over extracted columns (temporal comparison on datetime fields via per-column codecs parsing to UTC epoch ms); empty filter costs zero bytes |
| Sorting | `sortRows` | stable off-thread sort of visible rows by cached keys |
| Annotations | `Annotation`/`AnnotationSet`, `FileIdentity`, `AnnotationScan` | versioned sidecar JSON, LWW merge, content-hash identity, bounded re-anchoring |

**Threading contract**: every potentially slow core operation returns a
`QFuture<T>` from a `QPromise`-based task - cancellable, with permille
progress - and is consumed on the GUI thread through a `QFutureWatcher`.
Nothing in the shell ever blocks on file size.

## The shell

- `logdor_interface` (shared lib): `PluginInterface` (below),
  `LogTableModel` + `LogViewerWidget` (the one generic lazy view: parses
  only visible rows behind an 8k-row LRU, off-thread filter/query/sort,
  annotation markers, echo-guarded selection sync, per-file view-state
  save/restore), `AnnotationHub` (one per app: shared note state +
  autosave hooks), `AnnotationDialog`, `annotationexporter`,
  `formatcatalog` (builtins + spec directories), `FolderView` (recursive
  file tree for Open Folder; hides sidecars, debounced selection-follow,
  wrap-around next/previous), `recentitems` (pure recents-list policy),
  `TimeSettings` (assumed zone for zone-less timestamps + per-file
  reference dates; viewers re-extract timestamp columns when it changes).
- `logdor` (executable): `MainWindow` owns the open flow (non-blocking
  index build), the filter bar, annotation persistence
  (`<log>.logdor.json` sidecars, app-data fallback, import/export,
  explicit Save / Save-As), the recents menu (`recentItems` in
  QSettings), **per-file sessions** (an in-memory map of filter +
  per-plugin view state, captured before a file switch and replayed when
  the file returns - annotations need no session, sidecars already
  persist them), and the plugin docks. `PluginManager` loads plugins and
  fans out `setCoreSource`/`setFilter`/`setAnnotationHub`/
  `saveViewState`/`restoreViewState` and the `LinesSelected` event
  (never echoed to the sender).

## Plugin API v3 (`app/src/plugininterface.h`)

```cpp
QString name() / version() / description();
QWidget* widget();
void setCoreSource(shared_ptr<FileSource>, shared_ptr<const LineIndex>);
void setAnnotationHub(AnnotationHub*);
void setFilter(const FilterOptions&);
QJsonObject saveViewState();                  // per-file session capture
void restoreViewState(const QJsonObject&);    // after setCoreSource, before setFilter
slots:   void onPluginEvent(PluginEvent, const QVariant&);
signals: void pluginEvent(PluginEvent, const QVariant&);   // LinesSelected
```

Everything is delivered on the GUI thread. Plugins run their own
background work - most just wrap `LogViewerWidget`, which already does
(including view-state capture: selection, sort, top-of-viewport source
line; restores apply when the plugin's own scans land). The iid
(`com.logdor.PluginInterface/3.1`) is bumped on any interface change so
stale binaries fail to load instead of crashing.

## Annotation sidecars

`<logfile>.logdor.json` (or `<appdata>/annotations/<hash>.logdor.json` when
the log's directory is read-only). Versioned JSON; file identity =
SHA-256 of the first 64 KiB (size stored outside the hash, so appended
logs still match); each note anchors to the SHA-256 of its line's first
256 bytes plus a display snippet. On load, anchors are verified
off-thread; moved lines are found by a bounded nearest-first search
(rotation's uniform shift is detected and fast-pathed); unmatched notes
are flagged *orphaned*, never dropped. Merging (auto or File -> Import)
is a union by UUID with last-write-wins by modification time.

## Extending Logdor

**A new format without compiling** - drop a JSON spec into
`~/.local/share/logdor/formats/` (see `core/formats/*.json` for the
schema), or build it interactively in the **Custom Format Viewer** and
click *Save as Format*. Specs participate in auto-detection and appear
in the Plain Text Viewer's format list. Bundled specs live in
`core/formats/`: `syslog-rfc3164`, `syslog-iso` (modern
rsyslog/Ubuntu 24.04+ high-precision timestamps), `dpkg`, `dmesg`,
`keyvalue`, `apt-history`, `apt-term`, `cloud-init`,
`cloud-init-output`, `apport`, `xorg`, `alternatives`, `cri`
(Kubernetes container logs, `kubectl logs --timestamps` /
`/var/log/containers`), and `klog` (Kubernetes control-plane /
glog-style `I0203 12:34:56.789012 ...`) — all golden-tested against
real captured lines in `core/tests/tst_systemformats.cpp`.

A `datetime` field may declare `"timeFormat"` — `"iso8601"`,
`"epoch-s"`/`"epoch-ms"`/`"epoch-us"`, `"uptime"` (monotonic seconds
since boot, e.g. dmesg/Xorg), or a Qt date/time pattern — so its values
parse into comparable instants for temporal filtering and sorting (see
`core/include/logdor/TimestampParse.h`). Omitted formats are
auto-detected from sample values; year-less formats (RFC 3164, logcat)
infer the year from the file's modification date.

**Binary logs** (systemd journal, wtmp/btmp/lastlog) have no newline
record boundaries, so they don't fit the `LineIndex` pipeline; a
record-index abstraction is future core work. Until then, export to
text: `journalctl -o short-iso-precise > out.log` is parsed by
`syslog-iso`, `journalctl -o json > out.log` by the `jsonlines`
builtin (which handles the journal's arbitrary field order, `PRIORITY`
levels, and µs-epoch timestamps), and `last -f /var/log/wtmp > out.log`
is plain text.

**A new C++ parser** - implement `FormatParser` (stateless, thread-safe
`parseLine`; fill every schema field even on mismatch; keep
`matchesStructure` honest for detection) and add it to
`builtinParsers()` in `core/src/FormatRegistry.cpp`. Golden-test it like
`core/tests/tst_formatparsers.cpp`.

**A new view plugin** - copy `plugins/csvviewer/` (the smallest full
example): subclass `PluginInterface`, wrap a `LogViewerWidget`, forward
`setCoreSource`/`setFilter`/`setAnnotationHub`, and wire
`linesSelected`/`selectSourceLines` for cross-view selection sync.

## Tests and benchmarks

`ctest -L unit` runs every suite (core + app, offscreen). Benchmarks are
opt-in (`-DLOGDOR_ENABLE_BENCH=ON`, label `bench`) and gate indexing
throughput (>=1 GB/s warm), filter throughput (>=800 MB/s substring),
field-query extraction (>=50 MB/s) and warm queries (<=500 ms on ~9M
lines), and cancellation latencies. Annotation paths have no benchmark
by design: counts are human-scale and re-anchoring is bounded.
