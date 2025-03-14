# logdor
Logdor! The discerninator!

<img src="https://user-images.githubusercontent.com/5616068/173696819-3d5ffdcf-5578-474b-8568-0ea793729328.png" height="400">

Logdor was a man! No... he was a logging man! ...or... maybe he was just a log viewer....

Logdor is a tool to simplify looking at log files by enabling filtering, color coding, and anything else I find useful.

![image](https://github.com/user-attachments/assets/679a40ce-82b1-4c57-af61-1d43c7ad2985)

## Plugins

Logdor provides functionality through a plugin-based architecture. Each plugin specializes in a specific type of log format or viewing capability:

- [Bookmark Viewer](plugins/bookmarkviewer/README.md) - Save and annotate important log lines for future reference
- [CLF Viewer](plugins/clfviewer/README.md) - Specialized parser for Apache and NGINX access logs
- [CSV Viewer](plugins/csvviewer/README.md) - Table view for CSV files with automatic header detection
- [Hexdump Viewer](plugins/hexdumpviewer/README.md) - View binary files in hexadecimal format
- [Logcat Viewer](plugins/logcatviewer/README.md) - Android logcat format viewer with color-coded log levels
- [Map Viewer](plugins/mapviewer/README.md) - Interactive map display for log entries containing coordinates
- [PGN Viewer](plugins/pgnviewer/README.md) - Interactive chess game viewer for PGN files
- [Plain Text Viewer](plugins/plaintextviewer/README.md) - Basic text file viewer with essential features
- [Regex Viewer](plugins/regexviewer/README.md) - Pattern matching with regular expressions and highlighting
- [Selected Line Viewer](plugins/selectedlineviewer/README.md) - Focused view of lines selected from other viewers
- [Syslog Viewer](plugins/syslogviewer/README.md) - Parser for system log files with facility and severity highlighting

## Features

For detailed information about each plugin's features, please visit the individual plugin README files linked above.

## Building from Source

### Requirements
- CMake 3.16 or higher
- Qt 6.8 or higher
- C++17 compliant compiler

### Dependencies
- Qt6::Core
- Qt6::Gui
- Qt6::Widgets

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
The plugins will be in `build/plugins/`.
