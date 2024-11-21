# proc-load

`proc-load` is a lightweight Linux command-line application written in C that monitors global CPU usage and the CPU usage of specific processes. It is configurable via a JSON file, allowing users to specify the list of processes to monitor based on their `cmdline`.

---

## Features

- Tracks global CPU usage percentage.
- Monitors CPU usage of specific processes based on their command-line invocation.
- Configurable via a JSON file.
- Command-line arguments are generated using `gengetopt`.

---

## Requirements

Before building and running the program, ensure the following tools and libraries are installed:

- `gcc` (for building the application)
- `cmake` (for managing the build process)
- `pkg-config` (for handling library dependencies)
- `libcjson-dev` (for parsing JSON files)
- `gengetopt` (for managing command-line arguments)

Install these requirements on Debian-based systems using the following command:
```bash
sudo apt update
sudo apt install gcc cmake pkg-config libcjson-dev gengetopt
```


## Adding new arguments

```bash
gengetopt < proc-load.ggo
```

## Running the Tool

```bash
./proc-load -c config.json
```