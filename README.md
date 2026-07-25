# logdor
Logdor! The discerninator!

<img src="https://user-images.githubusercontent.com/5616068/173696819-3d5ffdcf-5578-474b-8568-0ea793729328.png" height="400">

Logdor was a man! No... he was a logging man! ...or... maybe he was just a log viewer....

Logdor is a tool to simplify looking at log files by enabling filtering, color coding, and anything else I find useful.

![image](https://github.com/user-attachments/assets/679a40ce-82b1-4c57-af61-1d43c7ad2985)

## Plugins

Logdor provides functionality through a plugin-based architecture. Each plugin specializes in a specific type of log format or viewing capability:

- **Plain Text Viewer** - the generic viewer for any format, with auto-detection and a format selector (all built-in and user-defined formats)
- **Merged Timeline** - merge events from multiple log files (same or different formats) into one time-sorted view; add files by dialog, drag-and-drop, or from the current file, toggle newest-first, and filter through the shared bar
- **Annotations** - every note on the open log in one panel: jump, edit, re-anchor, share
- **Custom Format Viewer** - author log formats interactively with a live regex; save them as shareable format specs
- **CSV Viewer** - table view for CSV files with header-derived, type-sniffed columns
- **CLF Viewer** - Apache/NGINX access logs
- **Logcat Viewer** - Android logcat with level toggles and tag filtering
- **NetLog Viewer** - Chrome `chrome://net-export` captures with decoded event/source types and tick-to-UTC timestamps
- **Hex Dump Viewer** - hex dump of the selected lines
- **Selected Line Viewer** - focused view of lines selected in other viewers
- **Map Viewer** - extracts coordinates (decimal, labeled, DMS) from log lines and plots them on an OpenStreetMap map (requires Qt WebEngine and network access for tiles)

Removed in the core migration (all preserved in git history): *Bookmark Viewer* (replaced by Annotations), *Syslog Viewer* (superseded by the bundled `syslog-rfc3164`/`syslog-iso` format specs), *Regex Viewer* (replaced by the Custom Format Viewer), *PGN Viewer* (retired).

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for how it all fits together and how to extend it.

## Architecture

Logdor is split into two layers:

- **`core/` (`logdor-core`)** - the non-GUI functional core: file access
  (`FileSource`, mmap with buffered fallback), line indexing (`LineIndex`,
  ~4 bytes/line), cancellable background index building (`buildLineIndex`),
  format parsers (`FormatParser`: plaintext, Android logcat, Apache CLF,
  JSON Lines (`journalctl -o json`, pino/bunyan-style app logs),
  Docker `json-file` container logs, GELF (Graylog Extended Log Format),
  plus user-writable **declarative JSON format specs** - drop a `.json` into
  `~/.local/share/logdor/formats` to parse a new format without compiling;
  bundled specs cover syslog in both the traditional RFC 3164 and modern
  ISO 8601/rsyslog flavors - the latter also parses `journalctl >
  file` exports - plus dpkg.log, dmesg, apt history.log/term.log,
  cloud-init(-output).log, apport.log, Xorg.0.log, alternatives.log,
  Kubernetes CRI container and klog control-plane logs,
  logfmt-style key=value, Apache and nginx error logs (nginx access
  logs in the default `combined` format already parse as Apache CLF),
  Amazon S3 server access logs, and the CEF and LEEF security event
  formats; W3C Extended Log Format (IIS) files get their columns from
  the `#Fields:` directive via the W3C Log Viewer plugin),
  sample-based auto-detection, off-thread filtering (`scanFilter` over a
  `RowSet`), a **field query language** (`level:error tag:Wifi* pid>=100
  "free text"` with AND/OR/NOT - toggle the `Q` button), and off-thread
  column sorting. It links QtCore/QtConcurrent only - never
  QtGui/QtWidgets - enforced by a configure-time link check and CTest
  guards, so a future TUI can reuse it.
- **`app/` + `plugins/`** - the Qt Widgets shell and viewer plugins. Ported
  plugins (plaintext, selected-line, CLF, logcat) are thin wrappers over one
  shared schema-driven lazy view (`LogViewerWidget`/`LogTableModel`) that
  parses only visible rows; opening a file costs the line index, not the file
  size (a 1 GB / 10M-line file adds ~44 MB of owned memory).

## Working with multiple logs

**File -> Open Folder...** (Ctrl+Shift+O) opens a folder as a recursive file
tree in a dock; click a file (or cycle with Ctrl+PgUp/Ctrl+PgDn) to jump
between logs. Each file keeps its annotations (sidecars, see below) and its
view state - filter, selection, sort, scroll, even per-viewer chrome like
logcat level/tag filters - for the whole session, so cycling back picks up
exactly where you left off. **File -> Open Recent** remembers the last ten
files and folders (Ctrl+1...Ctrl+9).

The filter bar's ★ button saves the current filter as a named preset and
recalls presets in one click. **File -> Highlight Rules...** colors lines
matching your patterns in every viewer (right-click a line for "Highlight
lines like this"). Each file's filter and view state now also persist
across app restarts (sessions are dropped automatically if the file's
content changed).

Right-click any log view and enable **Show Timeline** for a severity-colored
histogram of the visible rows over time: hover for counts, click to jump to
that moment, drag to restrict the filter to a time range (Esc clears it).
The Merged Timeline shows the same strip summed across its files.

**File -> Search in Folder...** (Ctrl+Shift+F) greps a whole directory tree
(gzipped logs included) and jumps straight to any match. Any viewer's
context menu can **Export Visible Rows** (text or CSV, in the current sort
order), and the filter bar's Time picker opens pre-seeded with the current
file's actual time span. Gzip-compressed logs (`*.gz`) open directly.

**File -> Follow File** (F8) tails the current file: new lines appear as
they are written (filters keep applying to them), a view scrolled to the
bottom stays pinned there, and a rotated or truncated file reloads
automatically and keeps following.

### Keyboard shortcuts

| Shortcut | Action |
|---|---|
| Ctrl+O | Open file |
| Ctrl+Shift+O | Open folder |
| Ctrl+1 ... Ctrl+9 | Open a recent file/folder |
| Ctrl+PgDn / Ctrl+PgUp | Next / previous file in the open folder |
| Ctrl+S | Save annotations now |
| Ctrl+Shift+S | Save annotations to a chosen file (one-time copy) |
| Ctrl+L | Focus the filter input |
| F8 | Follow the current file (live tail) |

## Annotations

Right-click any line (or selection) in a viewer to add a note with an
optional color and tag; the **Annotations** panel lists every note - click
to jump all viewers there. Notes are saved automatically (Ctrl+S forces a
save immediately; Ctrl+Shift+S writes a copy wherever you like without
retargeting the autosave) to a sidecar file
next to the log (`<logfile>.logdor.json`; app-data fallback when the
directory is read-only) keyed by content, so they survive restarts, log
growth, renames, and - via content re-anchoring - log rotation (notes whose
lines vanish are flagged orphaned, never dropped). **Share** by sending the
sidecar: a colleague drops it next to their copy of the log, or uses
File -> Import Annotations to merge it (conflicts resolve by most-recent
edit). File -> Export Annotations produces a self-contained HTML report or
CSV. No benchmarks gate this path by design: annotation counts are
human-scale and re-anchoring is bounded (<=32 MiB per note, off-thread,
cancellable).

## Building from Source

### Requirements
- CMake 3.22 or higher
- Qt 6.8 or higher
- C++20 compliant compiler

### Build Instructions

1. Configure with CMake:
```bash
cmake -B build
# if the system Qt6 is not the one you want to use, you can specify the path to the Qt6 you want to use
# e.g.:
# /path/to/Qt/6.8.0/gcc_64/bin/qt-cmake -B build
```

1. Build the project:
```bash
cmake --build build/
```

The built executable and plugins will be in the `build` directory. Logdor will be in `build/app`
The plugins will be in `build/plugins/`. You can open a file directly with
`build/app/logdor /path/to/file.log`.

## Tests and Benchmarks

Unit tests run with CTest:
```bash
ctest --test-dir build -L unit --output-on-failure
```

Performance benchmarks are opt-in (they generate a 1 GiB corpus with the
bundled `loggen` tool on first run):
```bash
cmake -B build -DLOGDOR_ENABLE_BENCH=ON   # add -DLOGDOR_BENCH_LARGE=ON for the 5 GiB corpus
ctest --test-dir build -L bench --output-on-failure
```
Gates: >=1000 MB/s warm indexing throughput, <=4.5 bytes/line index memory,
<=100 ms cancellation latency.
