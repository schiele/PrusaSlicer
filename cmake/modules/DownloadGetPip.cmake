#/|/ Copyright (c) preFlight 2025+ oozeBot, LLC
#/|/
#/|/ Released under AGPLv3 or higher
#/|/
# Downloads get-pip.py to the bundled Python directory if not already present.
# Arguments: -DPYTHON_DIR=<path_to_python_dir>
set(_getpip "${PYTHON_DIR}/get-pip.py")
if (NOT EXISTS "${_getpip}")
    message(STATUS "Downloading get-pip.py to ${PYTHON_DIR}...")
    # URL is a rolling latest - no hash pin (would break on every pypa release).
    # HTTPS provides transport integrity. This runs at build time, not end-user.
    file(DOWNLOAD
        "https://bootstrap.pypa.io/get-pip.py"
        "${_getpip}"
        STATUS _dl_status
        TIMEOUT 30
    )
    list(GET _dl_status 0 _dl_code)
    if (NOT _dl_code EQUAL 0)
        message(STATUS "get-pip.py download failed (status: ${_dl_status}). "
                       "Users can download it manually from https://bootstrap.pypa.io/get-pip.py")
    else ()
        message(STATUS "get-pip.py downloaded successfully")
    endif ()
endif ()
