# Logcat Viewer Plugin

The Logcat Viewer plugin for Logdor provides specialized viewing capabilities for Android logcat log files.

## Features

- Specialized viewer for Android logcat files
- Color-coded log levels:
  - Verbose (V)
  - Debug (D)
  - Info (I)
  - Warning (W)
  - Error (E)
  - Fatal (F)
- Quick filtering by log level with toolbar buttons
- Text filtering across tags and messages
- Sortable columns for priority, time, PID, and tag
- Row-based selection
- Clean, grid-less display with alternating row colors
- Tag highlighting for easier identification

## Usage

1. Open an Android logcat file in Logdor
2. Use the Logcat Viewer tab to see the structured log view
3. Filter by log level using the toolbar buttons
4. Search within specific columns or across the entire log
5. Sort by time, priority, PID, or tag as needed

## Implementation

The plugin parses Android logcat format and presents it in an easily navigable interface with visual cues for different log levels.
