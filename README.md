# proc-load

`proc-load` is a lightweight Linux command-line application written in C that monitors global CPU usage and the CPU usage of specific processes. It is configurable via a JSON file, allowing users to specify the list of processes to monitor based on their `cmdline`.

It will output the following stats to DLT using two contexts:

| App ID | Context ID | Payload  |
|-----------|-----------|-----------|
| "LOAD" | "CPU" | **global** %CPU load and kBytes **available** (reclaimable, buffer cache, physical RAM remaining) |
| "LOAD" | "PROC" | **per process** %CPU load and kBytes **used** (rss = resident set size) |

---

## Features

- Tracks global CPU usage percentage 
- Monitors CPU usage of specific processes based on their command-line invocation.
- Configurable via a JSON file.
- Command-line arguments are generated using `gengetopt`.

---

## Requirements

Install these requirements on Debian-based systems using the following command:
```bash
sudo apt update
sudo apt install gcc cmake pkg-config json-c gengetopt
```

## Adding new arguments

```bash
gengetopt < proc-load.ggo
```

## Running the Tool, Options

* Verbose:      option -V/--verbose will output the stats also on stdout
* Interval:     option -i/--interval will set the integration time, the longer the more accurate. default is 3s.
* Config file:  option -c/--config-file will provide the patch to the json vector, of the command lines to monitor.

```bash
./proc-load -c example.json --verbose -i 1
```

## Example of Outputs

### Example output on stdout (verbose mode)

```bash
global: 0.69(%cpu)      47943280(kBytes available)
/usr/bin/gnome-shell:   5.00(%cpu)      565176(kBytes)
```

### Example on iCore9'12th, kernel 5.15.0

output on DLT (ecu, appid, contextid, payload) : in this example global CPU load is almost < 1%, gnome-shell is taking 0.6%. Available is in kbytes, on a large machine with large swap.
Figures are correct, consistent with _top_ command.

```bash
ECU1 LOAD CPU 0 47955624
ECU1 LOAD PROC /usr/bin/gnome-shell 0.66445184 565140
```

### Example output on ESG, kernel 5.4.3

output on DLT, 45% cpu load (3 rx, no tx), 168MiB reclaimable/avail. audio app uses 31% cpu, and 4MiB RAM.

```bash
WIV2 LOAD CPU 45 168344
WIV2 LOAD PROC /usr/bin/mco-audioApp 30.66666603 4332
WIV2 LOAD PROC /usr/bin/mqtt-uart-bridge 0.33333334 4492
```

## Systemd Integration

in yocto, you may copy the recipe from here : meta-vokkero-core/recipes-support/load-proc

the service file looks typically like this:

```
[Unit]
Description=Proc Load Trace Service
After=mosquitto.service dlt-daemon.service
Before=mco-service-local.service

[Service]
Type=simple
ExecStart=/usr/bin/proc-load -c /etc/proc-load/config.json
ExecReload=/bin/kill -HUP $MAINPID
Restart=on-failure
RestartSec=5

[Install]
WantedBy=multi-user.target
```

