# CSV Viewer Plugin

A plugin for viewing CSV (Comma-Separated Values) files in a table format. The plugin automatically uses the first row of the CSV file as column headers.

## Features

- Automatic header detection from first row
- Sortable columns (click on column headers to sort)
- Smart handling of quoted CSV values
- Automatic right-alignment for numeric columns
- Full text search with regex support
- Context lines for search results
- Line selection and synchronization with other plugins
- Alternating row colors for better readability

## Usage

1. Load a CSV file into Logdor
2. The first row will be used as column headers
3. Click column headers to sort by that column
4. Use the search bar to filter rows:
   - Regular text search
   - Regex mode for advanced patterns
   - Context lines to show surrounding data
5. Select rows to highlight them in other plugins

## CSV Format Support

- Standard comma-separated values
- Quoted values (e.g., `"value with, comma"`)
- Mixed numeric and text data with automatic detection
- UTF-8 encoded text

## Example

For a CSV file like:
```csv
Name,Age,City,Score
John Doe,30,"New York, NY",95.5
Jane Smith,28,Boston,92.0
```

The plugin will:
- Use "Name", "Age", "City", and "Score" as column headers
- Right-align the "Age" and "Score" columns (numeric)
- Handle the comma in "New York, NY" correctly due to quotes
- Allow sorting by any column
