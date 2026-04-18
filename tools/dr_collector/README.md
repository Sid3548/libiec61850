# DR Collector

Runs continuously on the engineering PC. When a relay trips, it automatically downloads the COMTRADE disturbance record (`.cfg` + `.dat`) to your Desktop.

Files land here:

```
Desktop\DRs\<relay-name>\dr_fault_<timestamp>\
```

If the relay reboots or the network drops, it reconnects automatically. No manual restart needed.

---

## Full setup guide (Windows — from scratch)

Follow these steps **once** on the engineering PC. After setup, daily use is just double-clicking the `.exe`.

---

### 1. Install Git

Download and install Git for Windows:
https://git-scm.com/download/win

Accept all defaults during install. When done, open **Git Bash** (search for it in the Start menu) — use this for all commands below.

---

### 2. Install MinGW-w64 (C compiler)

Download the installer:
https://github.com/niXman/mingw-builds-binaries/releases

Pick the latest release, file named something like:
`x86_64-...-release-win32-seh-msvcrt-rt_v12-rev0.7z`

Extract it to `C:\mingw64\`.

Then add the compiler to your PATH:

1. Open **Start → Search → "Edit the system environment variables"**
2. Click **Environment Variables**
3. Under **System variables**, find **Path** → click **Edit**
4. Click **New** → type `C:\mingw64\bin`
5. Click OK on all dialogs

Open a new Git Bash window and verify:

```bash
gcc --version
```

You should see a version number. If not, recheck the path above.

---

### 3. Clone the repository

In Git Bash:

```bash
cd ~/Desktop
git clone https://github.com/Sid3548/libiec61850.git
cd libiec61850
git checkout v1.6
```

---

### 4. Build the library

Still in Git Bash, inside the `libiec61850` folder:

```bash
make
```

This builds `libiec61850.a`. Takes about 1–2 minutes. You should see no errors.

---

### 5. Build dr_collector.exe

```bash
make -C tools/dr_collector
```

This produces `tools/dr_collector/dr_collector.exe`.

---

### 6. Create a working folder

Create a folder for day-to-day use, e.g. `C:\DR_Collector\`:

```bash
mkdir -p /c/DR_Collector
cp tools/dr_collector/dr_collector.exe /c/DR_Collector/
cp tools/dr_collector/dr_collector.sample.json /c/DR_Collector/dr_collector.local.json
```

---

### 7. Edit the config

Open `C:\DR_Collector\dr_collector.local.json` in Notepad (or any text editor).

You **must** fill in two values:

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

| Field | What to change |
|---|---|
| `relay.ip` | Replace `192.168.1.10` with your relay's IP address |
| `relay.trigger_rcb` | Replace with your relay's RCB path — see section below |

Leave everything else as-is.

---

### 8. Run

Open a Command Prompt, navigate to `C:\DR_Collector\` and run:

```
dr_collector.exe
```

Or create a shortcut to `dr_collector.exe` on the Desktop and double-click it.

The window will show:

```
Config: dr_collector.local.json
2026-04-18 14:02:11 [INFO] connect 192.168.1.10:102
2026-04-18 14:02:12 [INFO] baseline marked 4 existing file(s)
2026-04-18 14:02:12 [INFO] waiting for relay report trigger
```

Leave it running. When the relay trips:

```
2026-04-18 14:07:45 [INFO] relay-1: report trigger from PROT1/LLN0.RP.TripRCB01
2026-04-18 14:07:55 [INFO] saved C:\Users\Engineer\Desktop\DRs\relay-1\dr_fault_20260418_140745\fault_001.cfg
2026-04-18 14:07:56 [INFO] saved C:\Users\Engineer\Desktop\DRs\relay-1\dr_fault_20260418_140745\fault_001.dat
```

Press `Ctrl+C` to stop.

---

### 9. Auto-start on Windows login (optional)

To have it start automatically when the PC boots:

1. Press `Win+R`, type `shell:startup`, press Enter
2. Create a shortcut to `C:\DR_Collector\dr_collector.exe` in that folder

---

## Finding trigger_rcb

This is the only value that differs between relay models. Two ways to find it:

**Option A — from the relay ICD/SCD file:**

Open the relay's `.icd` or `.scd` file in a text editor. Search for `ReportControl`. You will see entries like:

```xml
<ReportControl name="TripRCB01" ...>
```

Build the path as: `<LDevice inst>/<LN class inst>.RP.<ReportControl name>`

Example: LDevice `PROT1`, LN `LLN0`, ReportControl `TripRCB01` → `PROT1/LLN0.RP.TripRCB01`

**Option B — from an MMS browser:**

Connect to the relay with an IEC 61850 MMS browser tool and browse the report control blocks. Copy the reference shown there.

---

## Troubleshooting

| Symptom | Fix |
|---|---|
| `connect failed` | Check `relay.ip` is correct and the PC can ping the relay |
| `cannot read config file` | Make sure `dr_collector.local.json` is in the same folder as the `.exe` |
| `fill relay ip and trigger_rcb` | You left the placeholder values in the JSON — fill them in |
| `cannot read trigger RCB` | The `trigger_rcb` path is wrong — re-check against the ICD file |
| No files downloaded after trip | Check `relay.directory` matches the COMTRADE folder on the relay |
| Window closes immediately | Run from a Command Prompt so you can see the error message |

All messages are also written to `Desktop\DRs\dr_collector.log`.

---

## Full config reference

| Field | Default | Meaning |
|---|---|---|
| `output_dir` | `~/Desktop/DRs` | Root folder where DRs are saved |
| `state_file` | `~/Desktop/DRs/dr_state.tsv` | Tracks which files have already been downloaded |
| `log_file` | `~/Desktop/DRs/dr_collector.log` | Log file path |
| `stable_cycles_before_download` | `2` | How many scans a file must be unchanged before downloading |
| `post_trigger_scan_interval_sec` | `10` | Seconds between scans after a trip |
| `post_trigger_scan_attempts` | `18` | Max scans after a trip (18 × 10s = 3 min total) |
| `connect_timeout_ms` | `5000` | Connection timeout in milliseconds |
| `request_timeout_ms` | `20000` | Per-request timeout in milliseconds |
| `baseline_existing_on_start` | `true` | Skip downloading files already on the relay on first run |
| `relay.name` | `relay-1` | Subfolder name under `output_dir` |
| `relay.ip` | — | **Required.** Relay IP address |
| `relay.port` | `102` | IEC 61850 MMS port (almost always 102) |
| `relay.trigger_rcb` | — | **Required.** Report control block reference |
| `relay.directory` | `/COMTRADE/` | Folder on the relay that holds COMTRADE files |

`~` expands to your home directory (`C:\Users\<you>`) on Windows and `/home/<you>` on Linux/macOS.
