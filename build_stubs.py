"""Generate preFlight.py type stub from PrintConfig.hpp + API stubs.

Produces a single self-contained preFlight.py file that IDEs use for
autocomplete. Contains the full API (enums, Move, Layer, GCode, etc.)
plus a Settings class with all PrintConfig keys.

Placed alongside user scripts so autocomplete works with zero config.

Usage: python generate_settings_stubs.py <PrintConfig.hpp> <api_stubs.py> <output_dir> [<output_dir2> ...]
"""

import os
import re
import sys

TYPE_MAP = {
    "ConfigOptionFloat": "str  # float",
    "ConfigOptionInt": "str  # int",
    "ConfigOptionBool": "str  # bool (0/1)",
    "ConfigOptionString": "str",
    "ConfigOptionStrings": "str  # semicolon-separated",
    "ConfigOptionFloats": "str  # semicolon-separated floats",
    "ConfigOptionInts": "str  # semicolon-separated ints",
    "ConfigOptionBools": "str  # semicolon-separated bools",
    "ConfigOptionPercent": "str  # percentage",
    "ConfigOptionPercents": "str  # semicolon-separated percentages",
    "ConfigOptionFloatOrPercent": "str  # float or percentage",
    "ConfigOptionPoints": "str  # coordinate pairs",
    "ConfigOptionEnum": "str  # enum name",
    "ConfigOptionEnums": "str  # semicolon-separated enum names",
    "ConfigOptionFloatsOrPercentsNullable": "str",
    "ConfigOptionIntsNullable": "str",
}

PRINT_CONFIG_CLASSES = {
    "MachineEnvelopeConfig",
    "GCodeConfig",
    "PrintConfig",
    "PrintObjectConfig",
    "PrintRegionConfig",
}


def extract_options(hpp_content):
    """Extract config option names and types from PrintConfig.hpp."""
    options = []
    pattern = r'PRINT_CONFIG_CLASS_(?:DERIVED_)?DEFINE\d*\(\s*(\w+)'

    for match in re.finditer(pattern, hpp_content):
        class_name = match.group(1)
        if class_name not in PRINT_CONFIG_CLASSES:
            continue

        start = match.start()
        depth = 0
        pos = hpp_content.index('(', start)
        for i in range(pos, len(hpp_content)):
            if hpp_content[i] == '(':
                depth += 1
            elif hpp_content[i] == ')':
                depth -= 1
                if depth == 0:
                    block = hpp_content[pos:i+1]
                    break

        opt_pattern = r'\((\w+),\s*([a-z_][a-z_0-9]*)\)'
        for opt_match in re.finditer(opt_pattern, block):
            opt_type = opt_match.group(1)
            opt_name = opt_match.group(2)
            if opt_type.startswith("ConfigOption"):
                options.append((opt_type, opt_name))

    return options


def generate_settings_class(options):
    """Generate the Settings class stub."""
    lines = [
        '',
        '',
        'class Settings:',
        '    """All available slicer settings (from PrintConfig).',
        '',
        '    Access via gcode.settings["key_name"]. Values are strings.',
        '    Auto-generated from PrintConfig.hpp at build time.',
        '    """',
        '',
    ]

    seen = {}
    for opt_type, opt_name in options:
        if opt_name not in seen:
            seen[opt_name] = opt_type

    for opt_name in sorted(seen.keys()):
        base_type = re.sub(r'<\w+>', '', seen[opt_name])
        type_hint = TYPE_MAP.get(base_type, "str")
        lines.append(f'    {opt_name}: {type_hint}')

    lines.append('')
    return '\n'.join(lines)


def build_pyi(api_stubs_content, settings_class):
    """Combine the API stubs with the Settings class into a single .py file."""
    lines = []
    for line in api_stubs_content.splitlines():
        if 'from ._settings import' in line:
            continue
        lines.append(line)

    content = '\n'.join(lines)
    content = content.replace("'Settings'", "Settings")

    # Insert Settings class BEFORE GCode class so the reference resolves
    gcode_pos = content.find('\nclass GCode:')
    if gcode_pos != -1:
        content = content[:gcode_pos] + settings_class + '\n' + content[gcode_pos:]
    else:
        content += settings_class

    return content


def write_if_changed(path, content):
    """Only write if content changed to avoid unnecessary rebuilds."""
    try:
        with open(path, 'r', encoding='utf-8') as f:
            if f.read() == content:
                return False
    except FileNotFoundError:
        pass

    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'w', encoding='utf-8', newline='\n') as f:
        f.write(content)
    return True


def main():
    if len(sys.argv) < 4:
        print(f"Usage: {sys.argv[0]} <PrintConfig.hpp> <api_stubs.py> <output_dir> [<output_dir2> ...]",
              file=sys.stderr)
        sys.exit(1)

    hpp_path = sys.argv[1]
    api_path = sys.argv[2]
    output_dirs = sys.argv[3:]

    with open(hpp_path, 'r', encoding='utf-8') as f:
        hpp_content = f.read()

    with open(api_path, 'r', encoding='utf-8') as f:
        api_content = f.read()

    options = extract_options(hpp_content)
    if not options:
        print("WARNING: No config options found in PrintConfig.hpp", file=sys.stderr)
        sys.exit(1)

    settings_class = generate_settings_class(options)
    pyi_content = build_pyi(api_content, settings_class)

    written = 0
    for output_dir in output_dirs:
        output_path = os.path.join(output_dir, "preFlight.py")
        if write_if_changed(output_path, pyi_content):
            written += 1
            print(f"Generated {len(options)} settings -> {output_path}")

    if written == 0:
        print("All stubs up to date")


if __name__ == "__main__":
    main()
