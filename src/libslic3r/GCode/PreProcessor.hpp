///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/
#ifndef slic3r_PreProcessor_hpp_
#define slic3r_PreProcessor_hpp_

#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <unordered_set>

namespace Slic3r
{

struct GCodeProcessorResult;
class VirtualGCodeFile;

struct MoveModification
{
    float feedrate;
    float fan_speed;
    float temperature;
    float delta_e;
    float original_feedrate;
    float original_fan_speed;
    float original_temperature;
    float original_delta_e;
    std::string annotation;
};

struct PreProcessorResult
{
    // gcode_id -> full set of modified values for that move
    std::unordered_map<unsigned int, MoveModification> modifications;

    // Raw G-code insertions: line_id -> G-code text to insert AFTER that line
    std::map<unsigned int, std::string> gcode_insertions;

    // Raw G-code insertions: line_id -> G-code text to insert BEFORE that line
    std::map<unsigned int, std::string> gcode_insertions_before;

    // Raw G-code line replacements: line_id -> replacement text
    std::unordered_map<unsigned int, std::string> line_replacements;

    // Layer boundary insertions: gcode_id of first move in layer -> prepend text
    std::map<unsigned int, std::string> layer_prepends;
    // gcode_id of last move in layer -> append text
    std::map<unsigned int, std::string> layer_appends;

    // Move removal: set of gcode_ids to skip during output
    std::unordered_set<unsigned int> removed_lines;

    int scripts_executed = 0;

    // Errors and warnings collected during script execution
    std::vector<std::string> warnings;
    std::vector<std::string> errors;

    bool has_modifications() const { return !modifications.empty(); }
    bool has_insertions() const { return !gcode_insertions.empty() || !gcode_insertions_before.empty(); }
    bool has_line_replacements() const { return !line_replacements.empty(); }
    bool has_layer_injections() const { return !layer_prepends.empty() || !layer_appends.empty(); }
    bool has_removals() const { return !removed_lines.empty(); }
    bool has_any_changes() const
    {
        return has_modifications() || has_insertions() || has_line_replacements() || has_layer_injections() ||
               has_removals();
    }
};

class Print;

// Run pre-processor Python scripts from an explicit ordered list.
// After all scripts complete, virtual_file points to the fully-materialized output.
PreProcessorResult run_pre_processor_scripts(GCodeProcessorResult &result, VirtualGCodeFile *&virtual_file,
                                             const std::vector<std::string> &script_paths,
                                             const std::string &resources_dir, const Print *print = nullptr);

} // namespace Slic3r

#endif // slic3r_PreProcessor_hpp_
