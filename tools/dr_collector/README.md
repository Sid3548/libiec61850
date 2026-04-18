# DR Collector

Runs continuously on the engineering PC. When a relay trips, it automatically downloads the COMTRADE disturbance record (`.cfg` + `.dat`) to your Desktop.

Files land here:

```
Desktop\DRs\<relay-name>\dr_fault_<timestamp>\
```

---

## Quick start

### Step 1 — Get the two files

Put these in the same folder (e.g. `C:\DR_Collector\`):

- `dr_collector.exe` — built by your engineer (see Build below)
- `dr_collector.local.json` — your config file (copy from `dr_collector.sample.json`)

### Step 2 — Edit the config

Open `dr_collector.local.json` and fill in **two values**:

```json
{
  "relay": {
    "ip": "192.168.1.10",
    "trigger_rcb": "PROT1/LLN0.RP.TripRCB01"
  }
}
```

| Field | What to put |
|---|---|
| `relay.ip` | IP address of the relay |
| `relay.trigger_rcb` | Report control block path — see below |

Everything else is pre-filled and works for most relays. You can leave it alone.

### Step 3 — Run

Double-click `dr_collector.exe`, or from a command prompt:

```
dr_collector.exe
```

It runs forever. When the relay trips, files appear on your Desktop under `DRs\`. If the relay reboots or the network drops, it reconnects automatically.

Press `Ctrl+C` to stop.

---

## Finding trigger_rcb

This is the only value that differs between relay models. It comes from the relay's ICD or SCD file.

**Option A — from the ICD/SCD file:**

Open the relay's `.icd` or `.scd` file in a text editor. Search for `ReportControl`. You will see entries like:

```xml
<ReportControl name="TripRCB01" ...>
```

The full path is built as: `<LDevice inst>/<LN prefix+class inst>.RP.<ReportControl name>`

Example: LDevice `PROT1`, LN `LLN0`, ReportControl `TripRCB01` → `PROT1/LLN0.RP.TripRCB01`

**Option B — from an MMS browser:**

Connect to the relay with an IEC 61850 MMS browser tool (e.g. the libiec61850 `iec61850_client_example1` demo or any relay configuration tool). Browse to the logical node that handles trip reports and copy the RCB reference shown there.

---

## Full config reference

```json
{
  "output_dir": "~/Desktop/DRs",
  "state_file": "~/Desktop/DRs/dr_state.tsv",
  "log_file": "~/Desktop/DRs/dr_collector.log",
  "stable_cycles_before_download": 2,
  "post_trigger_scan_interval_sec": 10,
  "post_trigger_scan_attempts": 18,
  "connect_timeout_ms": 5000,
  "request_timeout_ms": 20000,
  "baseline_existing_on_start": true,
  "relay": {
    "name": "relay-1",
    "ip": "192.168.1.10",
    "port": 102,
    "trigger_rcb": "PROT1/LLN0.RP.TripRCB01",
    "directory": "/COMTRADE/"
  }
}
```

| Field | Default | Meaning |
|---|---|---|
| `output_dir` | `~/Desktop/DRs` | Root folder where DRs are saved |
| `state_file` | `~/Desktop/DRs/dr_state.tsv` | Tracks which files have already been downloaded |
| `log_file` | `~/Desktop/DRs/dr_collector.log` | Log file path |
| `stable_cycles_before_download` | `2` | How many scans a file must be unchanged before downloading |
| `post_trigger_scan_interval_sec` | `10` | Seconds between scans after a trip |
| `post_trigger_scan_attempts` | `18` | Max scans to attempt after a trip (18 × 10s = 3 min) |
| `connect_timeout_ms` | `5000` | Connection timeout in milliseconds |
| `request_timeout_ms` | `20000` | Per-request timeout in milliseconds |
| `baseline_existing_on_start` | `true` | Mark files already on the relay as known so they are not downloaded on first run |
| `relay.name` | `relay-1` | Subfolder name under `output_dir` |
| `relay.ip` | — | **Required.** Relay IP address |
| `relay.port` | `102` | IEC 61850 MMS port (almost always 102) |
| `relay.trigger_rcb` | — | **Required.** Report control block reference |
| `relay.directory` | `/COMTRADE/` | Folder on the relay that holds COMTRADE files |

`~` expands to your home directory on both Windows (`%USERPROFILE%`) and Linux/macOS (`$HOME`). You can also write `%USERPROFILE%\Desktop\DRs` directly.

---

## Build

### Linux / macOS

```sh
make -C tools/dr_collector
```

### Windows (MinGW)

From a MinGW shell at the repo root:

```sh
make BUILD_TARGET=WIN32 -C tools/dr_collector
```

### Windows (Visual Studio / MSVC)

Build the libiec61850 solution, then add `tools/dr_collector/dr_collector.c` as a new console application project and link against `iec61850.lib`.

---

## Test a single download

Build and exit after the first completed download (useful for commissioning):

```sh
make -C tools/dr_collector once
# or
dr_collector.exe --once
```

---

## Log

All status messages are written to the terminal and appended to `log_file`. Check this file if something is not working.

```
2026-04-18 14:02:11 [INFO] connect 192.168.1.10:102
2026-04-18 14:02:12 [INFO] baseline marked 4 existing file(s)
2026-04-18 14:02:12 [INFO] waiting for relay report trigger
2026-04-18 14:07:45 [INFO] relay-1: report trigger from PROT1/LLN0.RP.TripRCB01
2026-04-18 14:07:55 [INFO] saved C:\Users\Engineer\Desktop\DRs\relay-1\dr_fault_20260418_140745\fault_001.cfg
2026-04-18 14:07:56 [INFO] saved C:\Users\Engineer\Desktop\DRs\relay-1\dr_fault_20260418_140745\fault_001.dat
```
