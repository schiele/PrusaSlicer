#/|/ Copyright (c) preFlight 2025+ oozeBot, LLC
#/|/
#/|/ Released under AGPLv3 or higher
#/|/
# Uncomments "import site" in a Python ._pth file to enable site-packages.
# Called by PythonRuntime.cmake during the dep install step.
file(READ "${PTH_FILE}" content)
string(REPLACE "#import site" "import site" content "${content}")
file(WRITE "${PTH_FILE}" "${content}")
