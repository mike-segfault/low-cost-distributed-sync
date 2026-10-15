# Tools

Utility scripts used throughout the project. These tools support development, testing, analysis, and documentation tasks that are shared across multiple examples.

Additional utilities may be added over time as the project grows.

## Files

| File | Purpose |
|---|---|
| `convert-log.ps1` | Converts raw Arduino Serial Monitor logs into a standardized ISO 8601 communication log format |

## convert-log.ps1

This PowerShell script converts mixed ESP32/ESP8266 communication logs into a consistent, machine-readable format using ISO 8601 timestamps.

It is intended for projects where multiple boards exchange UDP messages and their Serial Monitor outputs are combined into a single log file.

### Features

- Converts timestamps to ISO 8601 format
- Processes combined logs from multiple boards
- Identifies devices automatically from log content

### Configuration

The script is configured through variables near the top of the file:

```powershell
$LogDate    = "2026-10-01"
$TZOffset   = "-04:00"

$InputFile  = "log-non-std.txt"
$OutputFile = "multi-board-log.txt"
```

Adjust these values to match the date, timezone, and filenames associated with the captured logs.

### Usage

Place the source log file in the same directory as the script and run:

```
powershell "C:\Path\To\convert-log.ps1"
```

If you encounter an error stating that script execution is disabled, bypass the policy for the current session by adding the ```-ExecutionPolicy Bypass``` flag:

```
powershell -ExecutionPolicy Bypass -File "C:\Path\To\script.ps1"
```

After processing, the converted log is written to the configured output file.

### Example

Input:

```text
12:00:01.123 -> Broadcasted: CMD:1:1:5
12:00:01.456 -> ACK received for seq = 1
12:00:03.789 -> Completed seq = 1
```

Output:

```text
2026-10-01T12:00:01.123-04:00 ESP32_AP TX UDP ...
2026-10-01T12:00:01.456-04:00 ESP32_AP RX UDP ...
2026-10-01T12:00:03.789-04:00 ESP32_AP RX UDP ...
```

## Future additions

The `tools` directory is intended to hold reusable utilities that support the project. Future additions may be added.