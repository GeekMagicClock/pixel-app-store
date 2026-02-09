Import("env")

import glob
from os.path import join

# LittleFS content for the ESP-IDF env (used by the `buildfs`/`uploadfs` targets).
# Keep it separate from other data folders so we can curate what goes into the
# limited filesystem partition.
env["PROJECT_DATA_DIR"] = join(env["PROJECT_DIR"], "data_littlefs")

# Ensure ESP-IDF CMake toolchain can find xtensa-esp32-elf-gcc by name.
platform = env.PioPlatform()
toolchain_dir = platform.get_package_dir("toolchain-xtensa-esp32")
if toolchain_dir:
    env.PrependENVPath("PATH", join(toolchain_dir, "bin"))


def _auto_pick_serial_port():
    patterns = [
        "/dev/cu.wchusbserial*",
        "/dev/cu.usbserial*",
        "/dev/cu.SLAB_USBtoUART*",
        "/dev/cu.usbmodem*",
    ]
    candidates = []
    for pat in patterns:
        candidates += glob.glob(pat)
    candidates = sorted(set(candidates))
    return candidates[0] if candidates else None


# Avoid auto-detecting Bluetooth serial ports by providing a better default.
auto_port = _auto_pick_serial_port()
if auto_port:
    env.Replace(UPLOAD_PORT=auto_port, MONITOR_PORT=auto_port)
