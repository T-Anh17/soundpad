# Embed the pre-encoded WiFi ding as a small ADPCM asset in firmware.
import os
import zlib

Import("env")  # noqa: F821 (PlatformIO/SCons cung cấp)

project = env.subst("$PROJECT_DIR")  # noqa: F821
src = os.path.join(project, "assets", "wifi_ding.adp")
out = os.path.join(project, "include", "wifi_sound.h")

with open(src, "rb") as f:
    data = f.read()
rows = ",\n".join(
    "  " + ",".join(f"0x{b:02x}" for b in data[i : i + 20])
    for i in range(0, len(data), 20)
)
header = (
    "#pragma once\n"
    "// Generated from assets/wifi_ding.adp by scripts/embed_wifi_sound.py.\n"
    "#include <stddef.h>\n#include <stdint.h>\n\n"
    f"static const uint8_t WIFI_SOUND_ADP[] = {{\n{rows}\n}};\n"
    "static const size_t WIFI_SOUND_ADP_LEN = sizeof(WIFI_SOUND_ADP);\n"
    f"static const uint32_t WIFI_SOUND_ADP_CRC32 = 0x{zlib.crc32(data):08x}u;\n"
)

old = open(out, encoding="utf-8").read() if os.path.exists(out) else ""
if header != old:
    with open(out, "w", encoding="utf-8", newline="\n") as f:
        f.write(header)
    print(f"embed_wifi_sound: assets/wifi_ding.adp -> include/wifi_sound.h ({len(data)} bytes)")
