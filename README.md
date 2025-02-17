# logdor
Logdor! The discerninator!

<img src="https://user-images.githubusercontent.com/5616068/173696819-3d5ffdcf-5578-474b-8568-0ea793729328.png" height="400">

Logdor was a man! No... he was a logging man! ...or... maybe he was just a log viewer....

Logdor is a tool to simplify looking at log files by enabling filtering, color coding, and anything else I find useful.

![image](https://github.com/user-attachments/assets/679a40ce-82b1-4c57-af61-1d43c7ad2985)

## Features

### Plain Text Viewer
- Line numbers
- Text filtering
- Line highlighting

### Logcat Viewer
- Specialized viewer for Android logcat files
- Color-coded log levels (Verbose, Debug, Info, Warning, Error, Fatal)
- Structured columns:
  - Line numbers
  - Timestamp
  - Process ID (PID)
  - Log Level
  - Tag
  - Message
- Quick filtering by log level with toolbar buttons
- Text filtering across tags and messages
- Sortable columns
- Row-based selection
- Clean, grid-less display with alternating row colors

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

1. Create a build directory:
```bash
mkdir build
cd build
```

2. Configure with CMake:
```bash
cmake ..
```

3. Build the project:
```bash
cmake --build .
```

The built executable and plugins will be in the `build` directory. Logdor will be in `build/app`
The plugins will be in `build/plugins/`.
