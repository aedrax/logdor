# Syslog Viewer Plugin

A plugin for viewing and analyzing syslog files with advanced filtering capabilities.

## Features

- View and parse syslog entries in various formats:
  - RFC5424 (Modern format)
  - RFC3164 (Legacy/BSD format)
  - Simple format
- Color-coded severity levels:
  - Emergency (Dark Red)
  - Alert (Red)
  - Critical (Red-Orange)
  - Error (Dark Orange)
  - Warning (Gold)
  - Notice (Light Sea Green)
  - Info (Cornflower Blue)
  - Debug (Dark Gray)
- Filter by:
  - Severity levels
  - Facilities (kern, user, mail, daemon, etc.)
  - Text search with regex support
- Interactive facility selection
- Sortable columns:
  - Line number
  - Timestamp
  - Hostname
  - Process[PID]
  - Facility
  - Severity
  - Message
- Context lines support for filtered results
- Synchronized selection with other plugins
- Monospace font for better log readability

## Supported Syslog Facilities

- kern (Kernel messages)
- user (User-level messages)
- mail (Mail system)
- daemon (System daemons)
- auth (Security/authorization)
- syslog (Syslogd messages)
- lpr (Line printer subsystem)
- news (Network news subsystem)
- uucp (UUCP subsystem)
- cron (Clock daemon)
- authpriv (Security/authorization)
- ftp (FTP daemon)
- ntp (NTP subsystem)
- logaudit (Log audit)
- logalert (Log alert)
- clock (Clock daemon)
- local0-7 (Local use)

## Example Log Formats

### RFC5424 Format
```
<34>1 2003-10-11T22:14:15.003Z mymachine.example.com su - ID47 - BOM'su root' failed for lonvick on /dev/pts/8
```

### RFC3164 Format
```
<34>Oct 11 22:14:15 mymachine su[230]: 'su root' failed for lonvick on /dev/pts/8
```

### Simple Format
```
Oct 11 22:14:15 mymachine su[230]: 'su root' failed for lonvick on /dev/pts/8
