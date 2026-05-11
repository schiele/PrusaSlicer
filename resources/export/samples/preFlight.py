"""
preFlight Export to Script - Type Stubs for IDE Auto-Completion

This file is NOT executed by preFlight. It provides type hints so your IDE
(VS Code, PyCharm, etc.) can offer auto-completion and type checking when
writing export scripts.

To use: place this file alongside your export script, or add the
resources/export/ folder to your IDE's extra paths.
"""

from typing import List


class ExportGCode:
    """G-code data object passed to export scripts.

    Attributes:
        data:     List of G-code lines, each including its trailing newline.
                  This is a standard Python list - fully mutable. You can
                  modify, insert, remove, or replace lines freely.
        filename: Suggested output filename (e.g. "my_model.gcode").
                  Read-only.
    """
    data: List[str]
    filename: str


def export(gcode: ExportGCode):
    """Called by preFlight when the user clicks 'Export to Script'.

    Args:
        gcode: ExportGCode object containing the G-code data and filename.

    The script is responsible for all output (saving to disk, uploading, etc.).
    preFlight does not write the G-code file when Export to Script is used.
    """
    pass
