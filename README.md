# 🐉 Fuzzy CSR!

A DKMS module to help with fuzzing the CSR space of various LoongArch cores.

## Build

```sh
cd /path/to/this/clone

# the DIY way:
make -C "/lib/modules/$(uname -r)/build" M="$(pwd)" modules

# or deploy via DKMS:
sudo dkms add "$(pwd)"
sudo dkms install fuzzycsr/0.1
```

## Usage

**Warning: USING THIS MODULE WILL CAUSE DATA LOSS.**

This is meant for developers tinkering with LoongArch internals only, *not* for general public use. You have been warned.

```sh
sudo modprobe fuzzycsr

# first, configure a mask
echo 0xffffffff00000000 > /sys/kernel/debug/loongarch/fuzzycsr/mask
# then poke a CSR of your choice
cat /sys/kernel/debug/loongarch/fuzzycsr/poke-12345
# will perform a csrxchg with the configured mask, both as a mask and
# the swapped-in value itself, then immediately restore the original
# value to the CSR and return the transient new value to userland
```

With luck, you can now discover undocumented CSRs and writable bits
implemented in your favorite LoongArch core!

## License

Copyright (C) 2025 WANG Xuerui.

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, see <https://www.gnu.org/licenses/>.
