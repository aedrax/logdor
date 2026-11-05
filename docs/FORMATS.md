# Format specs

A format spec is a JSON file that teaches Logdor a new log format. You
don't have to compile anything. The spec has one regular expression with
named groups, and a list of columns built from those groups. Logdor loads
specs at startup. A spec takes part in auto-detection and shows up in the
format list next to the built-in formats.

You can also make a spec in the **Custom Format Viewer**: write a regex
against the open file, watch the columns fill in, and click
*Save as Format*. This page describes the file that button writes, so you
can write or edit specs by hand.

## A minimal spec

```json
{
    "id": "myapp",
    "displayName": "My App",
    "pattern": "^(?<time>\\S+) (?<msg>.*)$",
    "fields": [
        { "name": "Time", "capture": "time", "type": "datetime", "hint": "timestamp" },
        { "name": "Message", "capture": "msg", "hint": "message" }
    ]
}
```

That is a complete spec. The rest of this page covers each key and the
optional `severity` and `specificity` keys.

## Top-level keys

| Key | Required | Value |
|---|---|---|
| `id` | yes | Unique identifier (non-empty string). Logdor keys specs by `id`, not by file name. |
| `displayName` | yes | Name shown in the format list. |
| `pattern` | yes | A regular expression with named groups. See [Pattern](#pattern). |
| `fields` | yes | Non-empty array of columns. See [Fields](#fields). |
| `severity` | no | Maps a captured value to a severity for row coloring. See [Severity](#severity). |
| `specificity` | no | Detection weight in (0, 1]. Default `0.8`. See [Auto-detection](#auto-detection-and-specificity). |

Logdor ignores keys it doesn't know.

## Pattern

The pattern is a Perl-compatible regular expression (PCRE2, through
`QRegularExpression`). It runs on each line as UTF-8 text, without the line
terminator or a trailing `\r`. Columns get their values from named groups:
`(?<name>...)`.

- **Anchor it.** The pattern isn't anchored for you. Logdor looks for a
  match anywhere in the line. Without `^...$`, a loose pattern matches
  lines it shouldn't, which also skews auto-detection.
- **Double every backslash.** The pattern is a JSON string, so the regex
  `\d+` is written `"\\d+"`, and a literal `[` is `"\\["`.
- **Optional parts are fine.** A group that doesn't take part in the match
  (`(?:\\[(?<pid>\\d+)\\])?`) gives an empty cell.
- **Lines that don't match still show.** The whole raw line goes into the
  `message` column, the other columns stay empty, and the row has no
  severity. A file that mixes formats still shows every line.

## Fields

Each entry in `fields` is one column, in display order.

| Key | Required | Value |
|---|---|---|
| `name` | yes | Column header. Must be unique, ignoring case. |
| `capture` | yes | Name of a named group in `pattern`. Several fields may use the same group. |
| `type` | no | `string` (default), `integer`, or `datetime`. |
| `hint` | no | `none` (default), `numeric`, `timestamp`, `severityname`, `identifier`, or `message`. |
| `timeFormat` | no | Only on `datetime` fields. How to read the values as times. See [Timestamps](#timestamps). |

**Exactly one field must have `"hint": "message"`.** It is the wide column
that takes the extra space, and it holds the raw line when the pattern
doesn't match.

### Types

The type sets how the column filters and sorts:

- `string`: text comparison and text sort.
- `integer`: numeric comparison in the query language (`pid>=100`) and
  numeric sort. A query that compares an integer field with something
  that isn't a number is an error.
- `datetime`: values are read as instants (see [Timestamps](#timestamps)).
  This lets you filter by time range, use the timeline, and sort by time.
  If no value in the column can be read as a time, the column sorts as
  text.

### Hints

Hints tell the viewer what a column means:

- `message`: the wide fallback column described above. It is required, and
  only one field can have it.
- `timestamp`: marks the main time column. The `@time` query field, the
  time-range picker, and the timeline use the first `timestamp` column.
  If there is none, they use the first `datetime` column.
- `severityname`: the column holds a level name. In queries, a level name
  compares by level order, so `level>=warning` matches warnings, errors,
  and fatals. Sorting this column sorts by the row's severity.
- `numeric`: right-aligns the column. Pair it with `"type": "integer"`
  to also get numeric filtering and sorting.
- `identifier`: marks a name-like column (host, process, tag, logger).
  For now this is only a label. Nothing behaves differently because of
  it.
- `none`: no special treatment.

### Query field names

In the query language, you refer to a field by its `name`, ignoring case
and with spaces removed. A column named `Thread ID` is queried as
`threadid:1234`.

## Severity

Without a `severity` object, rows are not colored. With one, Logdor reads
one captured value and maps it to a severity:

```json
"severity": {
    "capture": "level",
    "map": { "E": "error", "W": "warning", "I": "info", "D": "debug" },
    "default": "none"
}
```

| Key | Required | Value |
|---|---|---|
| `capture` | yes | A named group in `pattern`. It doesn't have to be a displayed field. |
| `map` | no | Captured value to severity. Keys ignore case. |
| `default` | no | Severity for values that aren't in `map`. Default `none`. |

Severity names: `none` (or `unknown`), `verbose`, `debug`, `info`,
`warning` (or `warn`), `error`, `fatal`. They ignore case.

Every value you want colored has to be in `map`, even when it already
matches a severity name: `"ERROR": "error"`. Usually you also show the
same group as a column with `"hint": "severityname"`. That way queries
like `level:error` and severity sorting work too.

## Timestamps

A `datetime` field's `timeFormat` says how to read its text:

| `timeFormat` | Meaning |
|---|---|
| `iso8601` | `2026-07-01T12:30:45.123+02:00`. The separator can be `T` or a space. Seconds, fraction, and zone are optional. A date alone works too. |
| `epoch-s` / `epoch-ms` / `epoch-us` | Unix time in seconds (a fraction is allowed), milliseconds, or microseconds. |
| `uptime` | Seconds since boot, as in dmesg and Xorg. These sort in order but have no calendar date, so they can't be compared with calendar dates. |
| anything else | A [Qt date/time pattern](https://doc.qt.io/qt-6/qdatetime.html#fromString-1), e.g. `"yyyy-MM-dd HH:mm:ss.zzz"` or `"dd/MMM/yyyy:HH:mm:ss"`. |

When you leave out `timeFormat`, Logdor samples the column's values and
picks the format that reads the most of them. Set it explicitly when you
know it. This avoids guesses on ambiguous values.

Qt patterns:

- **No year (`y`)** in the pattern: Logdor takes the year from the file's
  modification date. A month later than the file's month counts as the
  year before. This way a log from December that was last written in
  January gets the right years.
- **No zone (`t`)** in the pattern: values are local times in the zone set
  in Logdor's time settings.
- **More than 3 fraction digits**: Qt patterns can't read more than
  milliseconds (`zzz`). Logdor reads microseconds only for these patterns:
  `"MMdd HH:mm:ss.zzzzzz"` (klog/glog) and `"MM/dd-HH:mm:ss.zzzzzz"`
  (Snort). For other microsecond timestamps, use `iso8601` if it fits.
  Otherwise, capture only the first three fraction digits.

## Auto-detection and specificity

When a file opens, Logdor takes up to 200 non-empty lines from the first
1 MiB. It scores each format:

```
score = (fraction of sample lines the pattern matches) × specificity
```

The format with the highest score wins. Use `specificity` when your
pattern competes with others. Give strict, distinctive patterns a high
value. Give loose patterns (anything with a lot of `.*` or `\\S+`) a low
value, so they don't take files from a stricter format. For reference,
the built-in formats use:

| Format | Specificity |
|---|---|
| Plain text (matches everything) | 0.05 |
| CSV | 0.6 |
| Spec default | 0.8 |
| logcat, W3C, klog | 0.9 |
| JSON Lines | 0.95 |
| Docker JSON, GELF, NetLog | 0.97 |
| Apache/NCSA combined (CLF) | 1.0 |

You can always choose a format by hand from the format list, whatever
auto-detection picks.

## Where specs live

Logdor reads every `*.json` file from these directories, in this order:

1. `<app dir>/../formats`: bundled specs in a development build.
2. `<app dir>/../share/logdor/formats`: bundled specs when installed.
3. Your user formats directory:
   - Linux: `~/.local/share/logdor/formats`
   - macOS: `~/Library/Application Support/logdor/formats`
   - Windows: `%APPDATA%\logdor\formats`

Within a directory, files load in name order. A later directory wins when
two specs share an `id`. To override a bundled spec, copy it into your
user directory and keep its `id`. The bundled specs are in
[`core/formats/`](../core/formats/).

Logdor reads specs only at startup. After you add or change a spec,
restart Logdor (or `logdor-tui`).

## Troubleshooting

Logdor skips a spec that fails to load and doesn't show an error dialog.
It only prints a warning on stderr. Start `logdor` from a terminal to
see it:

```
format spec "/home/me/.local/share/logdor/formats/myapp.json" : "fields[1].capture: named group 'mesg' not found in pattern"
```

Common errors:

| Message | Fix |
|---|---|
| `invalid JSON at offset N: ...` | A JSON syntax error. Often it's a single backslash in `pattern`. |
| `pattern: invalid regex at offset N: ...` | The regex doesn't compile. |
| `fields[i].capture: named group 'x' not found in pattern` | Spell the group the same in `pattern` and `capture`. |
| `fields[i].name: duplicate field name 'x'` | Column names must be unique, ignoring case. |
| `fields: exactly one field must have the "message" hint ...` | Mark exactly one column `"hint": "message"`. |
| `fields[i].timeFormat: only valid on datetime fields` | Add `"type": "datetime"` or remove `timeFormat`. |
| `fields[i].type: unknown value 'x'` / `fields[i].hint: unknown value 'x'` | Use one of the values listed above. |
| `severity.map['x']: unknown severity 'y'` | Use one of the severity names listed above. |
| `specificity: must be in (0, 1]` | Use a number greater than 0 and at most 1. |

*Save as Format* in the Custom Format Viewer runs the same checks and
shows any error in a dialog. You can use it to test a regex before you
edit a spec by hand.

## A complete example

Lines like these:

```
24.07.2026 06:15:02.481 | W | cache   | 1423 | eviction took 412 ms
24.07.2026 06:15:02.907 | E | server  | 1423 | bind failed: port 8080 in use
```

go with this spec:

```json
{
    "id": "example-app",
    "displayName": "Example App",
    "pattern": "^(?<time>\\d{2}\\.\\d{2}\\.\\d{4} \\d{2}:\\d{2}:\\d{2}\\.\\d{3}) \\| (?<level>[DIWEF]) \\| (?<component>\\S+) +\\| (?<pid>\\d+) \\| (?<msg>.*)$",
    "fields": [
        { "name": "Time", "capture": "time", "type": "datetime", "hint": "timestamp",
          "timeFormat": "dd.MM.yyyy HH:mm:ss.zzz" },
        { "name": "Level", "capture": "level", "hint": "severityname" },
        { "name": "Component", "capture": "component", "hint": "identifier" },
        { "name": "PID", "capture": "pid", "type": "integer", "hint": "numeric" },
        { "name": "Message", "capture": "msg", "hint": "message" }
    ],
    "severity": {
        "capture": "level",
        "map": { "D": "debug", "I": "info", "W": "warning", "E": "error", "F": "fatal" },
        "default": "none"
    },
    "specificity": 0.9
}
```

Save it as `example-app.json` in your user formats directory and restart
Logdor. Files in this format now open with five columns and colored rows.
Queries like `level>=warning component:server` and time-range filters work on
them.
