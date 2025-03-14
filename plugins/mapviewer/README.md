# Map Viewer Plugin

A plugin for Logdor that parses coordinates from log files and displays them on an interactive map.

## Features

- Automatically detects and parses various coordinate formats:
  - Decimal degrees (e.g., `40.7128, -74.0060`)
  - Degrees with cardinal directions (e.g., `40.7128°N, 74.0060°W`)
  - Degrees, minutes, seconds (e.g., `40°42'51"N, 74°00'21"W`)
- Interactive map display using OpenStreetMap and Leaflet.js
- Split view with coordinate table and map
- Clickable markers with log message popups
- Synchronizes with other plugins' filtering
- Supports selection to focus on specific coordinates

## Supported Coordinate Formats

The plugin attempts to parse coordinates in the following formats:

1. Decimal Degrees:
   - `40.7128, -74.0060`
   - `lat: 40.7128, long: -74.0060`
   - `latitude: 40.7128, longitude: -74.0060`

2. Degrees with Cardinal Directions:
   - `40.7128°N, 74.0060°W`
   - `40.7128N, 74.0060W`

3. Degrees, Minutes, Seconds (DMS):
   - `40°42'51"N 74°00'21"W`
   - `40° 42' 51"N, 74° 0' 21"W`

## Usage

1. Load a log file containing coordinates
2. The plugin will automatically parse any coordinates it finds
3. Coordinates will be displayed in both the table and on the map
4. Click on table rows to center the map on the selected coordinate
5. Click on map markers to view the associated log message

## Sample Log

A sample log file (`sample.log`) is provided to demonstrate the supported coordinate formats:

```
[2025-03-14 10:00:00] System initialized at location: 40.7128°N, 74.0060°W (New York City)
[2025-03-14 10:30:00] Weather station report from 48° 51' 24"N, 2° 21' 03"E (Paris)
[2025-03-14 11:45:00] Monitoring station at lat: -33.9249, long: 18.4241 (Cape Town)
```

The sample includes various real-world locations with coordinates in different formats, demonstrating:
- Decimal degrees with cardinal directions
- Degrees, minutes, seconds (DMS)
- Decimal degrees with lat/long labels
- Various formatting variations (spaces, symbols)
- Invalid and missing coordinates for testing

## Dependencies

- QtWebEngine for map display
- OpenStreetMap (via Leaflet.js) for map tiles
