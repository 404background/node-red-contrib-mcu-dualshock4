# Attribution and License Notice

The source code in this directory (`native/esp32/ps4/`) is derived from the **PS4-esp32** library, created by [Albert III (aed3)](https://github.com/aed3). 
Repository: https://github.com/aed3/PS4-esp32

The **PS4-esp32** library is heavily based on the earlier work of Jeffery Pernis to connect a PS3 controller to an ESP32.
Repository: https://github.com/jvpernis/esp32-ps3

The files in this directory contain modifications made specifically for integration into Node-RED MCU (Moddable SDK / XS Engine) via the `ds4_native.c` XS-in-C bridge. Features such as MAC address filtering (`ps4SetFilterAddress()`, `ps4MatchesFilterAddress()`) were added, and `ps4_int.h` was updated to accept the ESP-IDF 5.x Kconfig names and to reject non-ESP32 targets.

Full attribution is given to the original authors. The presence of this code does not imply endorsement by the original authors. 

For the overall license of the Node-RED MCU Node wrapper code (`@background404/node-red-contrib-mcu-dualshock4`), please refer to the `LICENSE` file at the root of the repository.