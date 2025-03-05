# CLF Viewer Plugin

The CLF (Common Log Format) Viewer plugin for Logdor provides specialized viewing capabilities for Apache and NGINX access logs.

## Features

- Parses standard CLF fields:
  - IP address
  - Identity
  - User ID
  - Timestamp
  - HTTP request
  - Status code
  - Bytes transferred
- Structured display of log entries
- Sortable columns for easy data analysis
- Filtering capabilities specific to web server logs

## Usage

1. Open an Apache or NGINX access log file in Logdor
2. Select the CLF Viewer tab to see the structured format
3. Sort by columns to organize data
4. Filter entries to find specific requests or status codes

## Implementation

The plugin parses CLF format log entries and presents them in a structured table model for easier analysis of web server access logs.
