# Log4j Viewer Plugin

A plugin for viewing and analyzing Log4j formatted log files with support for custom pattern layouts.

## Features

- Support for custom Log4j pattern layouts
- Color-coded log levels (TRACE, DEBUG, INFO, WARN, ERROR, FATAL)
- Logger-based filtering
- Level-based filtering
- Sortable columns
- Stack trace display
- MDC (Mapped Diagnostic Context) support

## Pattern Layout Support

The plugin supports standard Log4j pattern layout elements including:

- `%d{pattern}` - Date with custom pattern (e.g., `%d{yyyy-MM-dd HH:mm:ss}`)
- `%p` or `%-5p` - Log level with optional width
- `%t` - Thread name
- `%c` or `%c{precision}` - Logger name with optional precision
- `%m` - Log message
- `%X` - MDC (Mapped Diagnostic Context)
- `%n` - Platform-specific line separator

### Default Pattern

The default pattern is: `%d{yyyy-MM-dd HH:mm:ss} %-5p [%t] %c{1} - %m%n`

You can modify this pattern through the UI to match your log format.

## Usage

1. Load a Log4j formatted log file
2. Adjust the pattern layout if it differs from the default
3. Use the toolbar buttons to filter by log level
4. Use the logger dropdown to filter by specific logger
5. Use the search functionality to filter messages
6. Click column headers to sort

## Examples

### Common Log4j Patterns

- Basic: `%d{ISO8601} [%t] %p %c - %m%n`
- With MDC: `%d [%t] %p %c %X - %m%n`
- Detailed: `%d{yyyy-MM-dd HH:mm:ss,SSS} [%t] %p %c{1.} - %m%n`

### Supported Log Formats

The plugin supports two common Log4j formats:

1. Standard format with class name:
```
%d{yyyy-MM-dd HH:mm:ss} %-5p [%t] %c{1} - %m%n

Example:
2024-03-14 12:00:00 INFO [main] com.example.App - Application starting up
```

2. Format with file and line information (default):
```
%d{yyyy-MM-dd HH:mm:ss,SSS} [%t] %-5p (%F:%L) - %m%n

Example:
2024-03-14 12:00:00,123 [main] INFO (Application.java:89) - Application starting up
```

### Features Demonstrated in Sample Log

The sample file (`sample.log`) demonstrates:
- All log levels (TRACE through FATAL)
- Millisecond precision timestamps
- Thread names in brackets
- Source file and line numbers
- Stack traces with exception details
- Multi-line log entries
- Various application components (Config, Service, Controller)

### Pattern Elements

- `%d{pattern}` - Date/time with optional format (supports both '.' and ',' for milliseconds)
- `%p` or `%-5p` - Log level with optional width
- `%t` - Thread name
- `%F` - Source file name
- `%L` - Line number
- `%c` - Logger name (fully qualified class)
- `%m` - Log message
- `%n` - Platform-specific line separator

You can modify the pattern through the UI to match your specific log format.
