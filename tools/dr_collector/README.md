# DR Collector

Small IEC 61850/MMS disturbance-record collector.

It subscribes to a configured IEC 61850 report control block. When a report arrives, it checks the relay COMTRADE folder and downloads new stable `.cfg/.dat` files into:

```text
Desktop/DRs/<relay-name>/dr_fault_<timestamp>/
```

No GOOSE is used. No remote files are deleted. Only `.cfg` and `.dat` files are downloaded for the GRL200 MVP. The relay `trigger_rcb` value must come from the relay ICD/CID/SCD file or relay browser.

Normal use has one editable file:

```text
tools/dr_collector/dr_collector.local.json
```

`make run` creates it from `dr_collector.sample.json` if it does not exist.

Required relay inputs:

| Field | Meaning |
| --- | --- |
| `relay.ip` | Relay IP address |
| `relay.trigger_rcb` | Report-control-block reference from ICD/CID/SCD or relay browser |

Usually leave these defaults unless the relay differs:

| Field | Meaning |
| --- | --- |
| `relay.name` | Folder name under `Desktop/DRs` |
| `relay.port` | IEC 61850/MMS port, normally `102` |
| `relay.directory` | Relay COMTRADE folder to scan |
| `output_dir` | Local root output folder |
| `state_file` | Tracks files already seen/downloaded |
| `log_file` | Collector log path |

Set relay `directory` to the COMTRADE folder to scan. Default:

```text
/COMTRADE/
```

After the config is loaded and the log file is opened, status lines printed in the terminal are also appended to `log_file`, defaulting to:

```text
Desktop/DRs/dr_collector.log
```

Errors before config loading or log opening can only be printed to the terminal.

## Build

```sh
make -C tools/dr_collector
```

For Windows, build with the existing project Windows toolchain/Visual Studio flow and this source file, or cross-compile through the existing Makefile target if MinGW is available.

## Run

Build and run with the local config:

```sh
make -C tools/dr_collector run
```

Build and exit after the first completed download:

```sh
make -C tools/dr_collector once
```

After building, this also works because `dr_collector.local.json` is now the default config:

```sh
tools/dr_collector/dr_collector
```

Wait for relay report trigger, then exit after the first completed download:

```sh
tools/dr_collector/dr_collector --once
```

Use a different config file only when needed:

```sh
tools/dr_collector/dr_collector path/to/other.json --once
```

Set `baseline_existing_on_start` to `true` so old relay files are marked known only when the state file is empty. Existing files are not downloaded on first run, and later restarts do not re-baseline files that appeared while the app was down.

Numeric config fields are strict: missing keys keep the built-in defaults, but invalid text stops config loading with an error.
