Import("env")

import glob
from os.path import join

# LittleFS content for the ESP-IDF env (used by the `buildfs`/`uploadfs` targets).
# Keep it separate from other data folders so we can curate what goes into the
# limited filesystem partition.
env["PROJECT_DATA_DIR"] = join(env["PROJECT_DIR"], "data_littlefs")

# Ensure ESP-IDF CMake toolchain binaries are on PATH for the active target.
platform = env.PioPlatform()
mcu = env.BoardConfig().get("build.mcu", "")
toolchains_by_mcu = {
    # Support both old (6.5.x) and new (6.13.x) package names.
    "esp32": ["toolchain-xtensa-esp32", "toolchain-xtensa-esp-elf"],
    "esp32s2": ["toolchain-xtensa-esp32s2", "toolchain-xtensa-esp-elf"],
    "esp32s3": ["toolchain-xtensa-esp32s3", "toolchain-xtensa-esp-elf", "toolchain-riscv32-esp", "toolchain-riscv32-esp-elf"],
    "esp32c3": ["toolchain-riscv32-esp"],
    "esp32c6": ["toolchain-riscv32-esp"],
    "esp32h2": ["toolchain-riscv32-esp"],
}

for pkg in toolchains_by_mcu.get(mcu, ["toolchain-xtensa-esp32", "toolchain-xtensa-esp-elf"]):
    try:
        toolchain_dir = platform.get_package_dir(pkg)
    except KeyError:
        toolchain_dir = None
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
