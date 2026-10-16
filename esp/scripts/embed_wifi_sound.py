# Embed the pre-encoded WiFi on/off chimes as small ADPCM assets in firmware.
import os
import zlib

Import("env")  # noqa: F821 (PlatformIO/SCons cung cấp)

project = env.subst("$PROJECT_DIR")  # noqa: F821
out = os.path.join(project, "include", "wifi_sound.h")
SOUNDS = [("WIFI_ON", "wifi_on.adp"), ("WIFI_OFF", "wifi_off.adp")]

parts = [
    "#pragma once\n"
    "// Generated from assets/wifi_on.adp, assets/wifi_off.adp by scripts/embed_wifi_sound.py.\n"
    "#include <stddef.h>\n#include <stdint.h>\n"
]
sizes = []
for name, file in SOUNDS:
    with open(os.path.join(project, "assets", file), "rb") as f:
        data = f.read()
    rows = ",\n".join(
        "  " + ",".join(f"0x{b:02x}" for b in data[i : i + 20])
        for i in range(0, len(data), 20)
    )
    parts.append(
        f"\nstatic const uint8_t {name}_ADP[] = {{\n{rows}\n}};\n"
        f"static const size_t {name}_ADP_LEN = sizeof({name}_ADP);\n"
        f"static const uint32_t {name}_ADP_CRC32 = 0x{zlib.crc32(data):08x}u;\n"
    )
    sizes.append(f"{file} {len(data)} bytes")
header = "".join(parts)

old = open(out, encoding="utf-8").read() if os.path.exists(out) else ""
if header != old:
    with open(out, "w", encoding="utf-8", newline="\n") as f:
        f.write(header)
    print(f"embed_wifi_sound: {', '.join(sizes)} -> include/wifi_sound.h")
