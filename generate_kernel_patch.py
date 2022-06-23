# SPDX-License-Identifier: MIT

from pathlib import Path

patch_header = Path("patch_header.patch.in").read_text()
patch_contents = open("panel-sitronix-st7701s.c", "rt").readlines()

with open("patch-sitronix-st7701s.patch", "wt") as patch_file:
    patch_file.write(patch_header)
    patch_file.write(f"@@ -0,0 +1,{len(patch_contents)} @@\n")
    for line in patch_contents:
        patch_file.write(f"+{line}")
