# Embed the pre-encoded chimes in firmware: WiFi on/off (IMA ADPCM, *_ADP) and the power-on
# ding (PCM 16-bit little-endian 22321 Hz mono, BOOT_P16, converted from assets/boot_ding.mp3).
import os

Import("env")  # noqa: F821 (PlatformIO/SCons cung cấp)

project = env.subst("$PROJECT_DIR")  # noqa: F821
out = os.path.join(project, "include", "wifi_sound.h")
SOUNDS = [("WIFI_ON_ADP", "wifi_on.adp"), ("WIFI_OFF_ADP", "wifi_off.adp"), ("BOOT_P16", "boot_ding.p16")]

parts = [
    "#pragma once\n"
    "// Generated from assets/wifi_on.adp, wifi_off.adp, boot_ding.p16 by scripts/embed_wifi_sound.py.\n"
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
        f"\nstatic const uint8_t {name}[] = {{\n{rows}\n}};\n"
        f"static const size_t {name}_LEN = sizeof({name});\n"
    )
    sizes.append(f"{file} {len(data)} bytes")
header = "".join(parts)

old = open(out, encoding="utf-8").read() if os.path.exists(out) else ""
if header != old:
    with open(out, "w", encoding="utf-8", newline="\n") as f:
        f.write(header)
    print(f"embed_wifi_sound: {', '.join(sizes)} -> include/wifi_sound.h")
