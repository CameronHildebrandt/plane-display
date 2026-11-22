Import("env")

import os
import re

# GitHub Actions supplies its monotonically increasing workflow run number.
# Local development builds intentionally use 0, which is older than every
# published release and therefore never masquerades as a deployable version.
version = os.environ.get("PLANE_DISPLAY_RELEASE_VERSION", "0")
if not re.fullmatch(r"[0-9]{1,10}", version):
    print("Invalid PLANE_DISPLAY_RELEASE_VERSION; expected an unsigned integer")
    Exit(1)

env.Append(CPPDEFINES=[("LED_PLANE_FIRMWARE_VERSION", version)])
