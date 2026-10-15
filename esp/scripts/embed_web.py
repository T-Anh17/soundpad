# Nén ../web/index.html (gzip) và nhúng vào include/web_page.h trước mỗi lần build.
import gzip
import os

Import("env")  # noqa: F821 (PlatformIO/SCons cung cấp)

project = env.subst("$PROJECT_DIR")  # noqa: F821
src = os.path.normpath(os.path.join(project, "..", "web", "index.html"))
out = os.path.join(project, "include", "web_page.h")

data = gzip.compress(open(src, "rb").read(), compresslevel=9, mtime=0)
rows = ",\n".join(
    "  " + ",".join(f"0x{b:02x}" for b in data[i:i + 20]) for i in range(0, len(data), 20)
)
header = (
    "#pragma once\n"
    "// Tạo tự động từ web/index.html bởi scripts/embed_web.py - đừng sửa tay\n"
    "#include <stddef.h>\n#include <stdint.h>\n\n"
    f"static const uint8_t WEB_PAGE_GZ[] = {{\n{rows}\n}};\n"
    "static const size_t WEB_PAGE_GZ_LEN = sizeof(WEB_PAGE_GZ);\n"
)

old = open(out, encoding="utf-8").read() if os.path.exists(out) else ""
if header != old:  # chỉ ghi khi web đổi, tránh build lại không cần thiết
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, "w", encoding="utf-8", newline="\n") as f:
        f.write(header)
    print(f"embed_web: {os.path.basename(src)} -> include/web_page.h ({len(data)} byte gzip)")
