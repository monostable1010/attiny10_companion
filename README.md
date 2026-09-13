# ATtiny10Programmer companion frontend

This project is a companion command-line frontend for
[`bdpdx/ATtiny10Programmer`](https://github.com/bdpdx/ATtiny10Programmer), the
upstream Arduino sketch that turns an Arduino into an ATtiny10 programmer. It
uses that sketch's serial command interface to identify the target, erase it,
inspect programmer status, dump memory, and upload an Intel HEX file without
requiring a serial monitor.

The upstream Arduino sketch is the actual programmer and remains a required
part of the setup. This repository provides a more convenient POSIX command-
line and Makefile-oriented way to drive it. Please refer to the
[upstream project's README](https://github.com/bdpdx/ATtiny10Programmer) for
the original wiring diagram, Arduino setup, programmer behavior, and project
history.

The frontend is written in C++17 and uses POSIX `termios`. It currently targets
Linux and macOS.

## Requirements

- An Arduino programmed with the `ATtiny10Programmer` sketch
- An ATtiny10 connected to that programmer according to the sketch's wiring
  instructions
- A POSIX system with:
  - a C++17 compiler
  - `make`
  - `avr-gcc` and `avr-binutils` when building ATtiny10 firmware

The programmer communicates at **115200 baud** by default. The supported
serial devices are `/dev/ttyACM*`, `/dev/ttyUSB*`, macOS `/dev/cu.usbmodem*`,
and `/dev/cu.usbserial*`.

## Build the frontend

From this directory:

```sh
make
```

This creates the `attiny10` executable. To run the command-line smoke test:

```sh
make test
```

To remove the executable:

```sh
make clean
```

The compiler and flags can be overridden on the command line, for example:

```sh
make CXX=clang++ CXXFLAGS='-std=c++17 -O2 -Wall -Wextra -Wpedantic'
```

## Manual use

### Find the programmer

If the Arduino is connected and its sketch is running, let the frontend scan
candidate serial ports:

```sh
./attiny10 --probe
```

To check one known port:

```sh
./attiny10 --probe --port /dev/ttyACM0
```

On macOS, the port may look like `/dev/cu.usbmodemXXXX`.

### Upload firmware

Build an Intel HEX image with `avr-gcc` and upload it with:

```sh
./attiny10 --port /dev/ttyACM0 --upload build/firmware.hex
```

The HEX file is validated before it is sent. It must contain valid Intel HEX
records, checksums, and an end-of-file record.

### Other operations

Every operation except `--probe` requires `--port`:

```sh
./attiny10 --port /dev/ttyACM0 --identify
./attiny10 --port /dev/ttyACM0 --erase
./attiny10 --port /dev/ttyACM0 --dump
./attiny10 --port /dev/ttyACM0 --free-memory
./attiny10 --port /dev/ttyACM0 --version
```

Short options are also available:

| Long option | Short option | Purpose |
| --- | --- | --- |
| `--upload FILE` | `-u FILE` | Upload an Intel HEX file |
| `--erase` | `-e` | Erase the ATtiny10 flash |
| `--identify` | `-i` | Identify the connected device |
| `--dump` | `-d` | Dump target memory |
| `--free-memory` | `-m` | Show free memory on the Arduino programmer |
| `--version` | `-v` | Show the programmer sketch version |
| `--port PORT` | `-p PORT` | Select a serial port |
| `--baud BAUD` | `-b BAUD` | Select baud rate; default is `115200` |
| `--timeout SEC` | `-t SEC` | Set response timeout; default is `10` seconds |
| `--probe` | `-P` | Find connected programmers |
| `--help` | `-h` | Show command-line help |

For example:

```sh
./attiny10 -p /dev/ttyACM0 -b 115200 -t 20 -u firmware.hex
```

The program returns a non-zero exit status when it cannot open the port, times
out, rejects the HEX file, or the programmer reports a failed operation.

## Use from an `avr-gcc` Makefile

The C++ frontend is separate from the firmware build. A firmware Makefile can
compile an ATtiny10 program with `avr-gcc`, convert the ELF file to Intel HEX,
and call this frontend for an `upload` target.

The following is a complete minimal example. Adjust `PROGRAMMER_BIN` and
`PROGRAMMER_PORT` for your setup:

```make
MCU              := attiny10
TARGET           := firmware
BUILD_DIR        := build

CC               := avr-gcc
OBJCOPY          := avr-objcopy
PROGRAMMER_BIN   := /path/to/ATtiny10ProgrammerCpp/attiny10
PROGRAMMER_PORT  := /dev/ttyACM0

CFLAGS           := -mmcu=$(MCU) -Os -Wall -Wextra

ELF              := $(BUILD_DIR)/$(TARGET).elf
HEX              := $(BUILD_DIR)/$(TARGET).hex

.PHONY: all hex upload erase clean

all: $(HEX)

$(BUILD_DIR):
	mkdir -p $@

$(ELF): main.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $< -o $@

$(HEX): $(ELF)
	$(OBJCOPY) -O ihex -R .eeprom $< $@

hex: $(HEX)

upload: $(HEX)
	$(PROGRAMMER_BIN) --port $(PROGRAMMER_PORT) --upload $(HEX)

erase:
	$(PROGRAMMER_BIN) --port $(PROGRAMMER_PORT) --erase

clean:
	rm -rf $(BUILD_DIR)
```

Run it as follows:

```sh
make                 # build build/firmware.hex
make upload          # build, then upload it
make erase           # erase the target flash
```

For C++ firmware, replace `avr-gcc` with `avr-g++`, add the required C++
flags, and change `main.c` to `main.cpp`. The important parts are
`-mmcu=attiny10`, generating an Intel HEX file with `avr-objcopy`, and passing
that HEX file to `attiny10 --upload`.

If the frontend is built as part of the same project, you can make the upload
target depend on it instead:

```make
PROGRAMMER_DIR := /path/to/ATtiny10ProgrammerCpp
PROGRAMMER_BIN := $(PROGRAMMER_DIR)/attiny10

$(PROGRAMMER_BIN):
	$(MAKE) -C $(PROGRAMMER_DIR)
```

Then keep the `upload` recipe from the example above.

## Troubleshooting

- Run `./attiny10 --probe` to see which serial ports are detected.
- If no port is found, check the USB connection and that the Arduino is
  running the programmer sketch.
- If the port is found but not recognized, verify that the connected Arduino
  is running the expected sketch and that the baud rate matches.
- On Linux, the user may need permission to access the serial device, commonly
  by belonging to the `dialout` group.
- Use `--timeout` to allow more time for a slow or large memory dump.

## Licensing and attribution

This frontend is an independent companion program and does not include or
redistribute the upstream Arduino sketch. It communicates with the sketch's
serial interface as documented by the upstream project.

The upstream [`bdpdx/ATtiny10Programmer`](https://github.com/bdpdx/ATtiny10Programmer)
project is released under the MIT License and contains its own author credits.
The applicable upstream notice is preserved in
[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).

This companion repository currently does not declare a license for its own
code. Do not assume that the companion code is MIT-licensed merely because the
upstream project is; add a project license before redistributing or reusing
this repository's code.
