# Python Log Viewer Plugin

A plugin for viewing Python log files in Logdor. This plugin provides a specialized viewer for Python logging output with features tailored for Python's logging format.

## Features

- Parses standard Python logging format:
  ```
  LEVEL:module:message
  ```
- Color-coded log levels (Debug, Info, Warning, Error, Critical)
- Module-based filtering
- Interactive level filtering through toolbar buttons
- Sortable columns
- Context-aware line selection
- Support for regex and plain text search
- Line number tracking

## Log Level Colors

- Debug: Light Green
- Info: Light Blue
- Warning: Orange
- Error: Tomato Red
- Critical: Medium Purple
- Unknown: White

## Columns

1. No. - Line number in the log file
2. Level - Log level (DEBUG, INFO, WARNING, ERROR, CRITICAL)
3. Module - Python module that generated the log
4. Message - The actual log message

## Usage

1. Load a Python log file in Logdor
2. Select "Python Log Viewer" from the available viewers
3. Use the toolbar buttons to filter by log level
4. Use the module combobox to filter by specific Python modules
5. Click column headers to sort entries
6. Use the search bar for text/regex filtering
