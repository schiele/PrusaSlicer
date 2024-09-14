
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <unordered_set>
#include <vector>

#include "ClipboardXX/include/clipboardxx.hpp"

#include <libslic3r/Preset.hpp>
#include <libslic3r/PresetBundle.hpp>
#include <libslic3r/LocalesUtils.hpp>
#include <libslic3r/libslic3r.h>
#include <libslic3r/PresetBundle.hpp>
#include <libslic3r/Utils.hpp>
#include <libslic3r/Model.hpp>
#include <libslic3r/format.hpp>
#include <libslic3r/PrintConfig.hpp>

#include <boost/filesystem.hpp>
#include <boost/algorithm/clamp.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/nowide/cenv.hpp>
#include <boost/nowide/cstdio.hpp>
#include <boost/nowide/fstream.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/locale.hpp>
#include <boost/log/trivial.hpp>

#include <LibBGCode/core/core.hpp>

using namespace Slic3r;
using namespace std;


// 1) For the group given by group_name, initialize the presets.
struct Prst {
    Prst(const std::string &section_name, const std::string &name, boost::property_tree::ptree *node) : section_name(section_name), name(name), node(node) {}
    // Name of this preset. If the name starts with '*', it is an intermediate preset,
    // this section name: [section_name: name]\nnode
    const std::string           section_name;
    // which will not make it into the result.
    const std::string           name;
    std::string                 name_with_template;
    // Link to the source boost property tree node, owned by tree.
    boost::property_tree::ptree *node;
    DynamicPrintConfig        config;
    DynamicPrintConfig        config_before_prusa;
    DynamicPrintConfig        config_before_legacy_composite;
    //DynamicPrintConfig        config_no_prusa_convert;
    DynamicPrintConfig        config_diff;
    // Link to the presets, from which this preset inherits.
    std::vector<std::shared_ptr<Prst>>          inherits;
    // Link to the presets, for which this preset is a direct parent.
    std::vector<std::shared_ptr<Prst>>          parent_of;
    // When running the Kahn's Topological sorting algorithm, this counter is decreased from inherits.size() to zero.
    // A cycle is indicated, if the number does not drop to zero after the Kahn's algorithm finishes.
    size_t                      num_incoming_edges_left = 0;
    // Sorting by the name, to be used when inserted into std::set.
    bool operator==(const Prst &rhs) const { return this->name == rhs.name; }
    bool operator< (const Prst &rhs) const { return this->name < rhs.name; }
};
typedef std::shared_ptr<Prst> PrstPtr;

std::string get_tech_str(PrinterTechnology tech) {
    return ((tech == PrinterTechnology::ptFFF)       ? "FFF" :
                (tech == PrinterTechnology::ptSLA)   ? "DLP(/fake SLA)" :
                (tech == PrinterTechnology::ptSLS)   ? "SLS" :
                (tech == PrinterTechnology::ptMill)  ? "CNC" :
                (tech == PrinterTechnology::ptLaser) ? "SLA(laser-based)" :
                                                     "unknown");
}


t_config_option_keys config_diffs(
    const DynamicPrintConfig &current_config,
    const DynamicPrintConfig &new_full_config)
{
    const std::vector<std::string> &extruder_retract_keys = print_config_def.extruder_retract_keys();
    const std::string               filament_prefix       = "filament_";
    t_config_option_keys            print_diff;
    for (const t_config_option_key &opt_key : current_config.keys()) {
        const ConfigOption *opt_old = current_config.option(opt_key);
        assert(opt_old != nullptr);
        const ConfigOption *opt_new = new_full_config.option(opt_key);
        // assert(opt_new != nullptr);
        if (opt_new == nullptr)
            //FIXME This may happen when executing some test cases.
            continue;
        if (*opt_new != *opt_old)
            print_diff.emplace_back(opt_key);
    }

    return print_diff;
}

void load_preset(const VendorProfile &vendor_profile, PrstPtr preset_to_load)
{

    std::cout << " --------- now loading preset ["<<preset_to_load->section_name<<":"<<preset_to_load->name<<"]  --------- \n";


    ConfigSubstitutionContext  substitution_context { ForwardCompatibilitySubstitutionRule::Enable };
    PresetsConfigSubstitutions substitutions;
    //std::string section_name, boost::property_tree::ptree section_data

    std::string path = "[file]";
    PresetBundle::LoadConfigBundleAttributes flags = PresetBundle::LoadConfigBundleAttribute::LoadSystem | PresetBundle::LoadConfigBundleAttribute::ConvertFromPrusa;
    PresetCollection         *presets = nullptr;
    PresetBundle default_bundle; //bundle with only default presets
    PhysicalPrinterCollection *ph_printers = nullptr;
    std::string               ph_printer_name;
    
    std::vector<std::string> loaded_fff_prints;
    std::vector<std::string> loaded_filaments;
    std::vector<std::string> loaded_sla_prints;
    std::vector<std::string> loaded_sla_materials;
    std::vector<std::string> loaded_printers;
    std::vector<std::string> loaded_physical_printers;
    std::string              active_print;
    std::vector<std::string> active_filaments;
    std::string              active_sla_print;
    std::string              active_sla_material;
    std::string              active_printer;
    std::string              active_physical_printer;
    size_t                   presets_loaded = 0;
    size_t                   ph_printers_loaded = 0;
    bool is_print = false;
    bool is_printer = false;

    if (preset_to_load->section_name == "print") {
        presets = &default_bundle.fff_prints;
        is_print = true;
    } else if (preset_to_load->section_name == "filament") {
        presets = &default_bundle.filaments;
        if (vendor_profile.templates_profile) {
            preset_to_load->name_with_template += " @Template";
        }
    } else if (preset_to_load->section_name == "sla_print") {
        presets = &default_bundle.sla_prints;
        is_print = true;
    } else if (preset_to_load->section_name == "sla_material") {
        presets = &default_bundle.sla_materials;
    } else if (preset_to_load->section_name == "printer") {
        is_printer = true;
        presets = &default_bundle.printers;
    } else if (preset_to_load->section_name == "physical_printer") {
        assert(false);
        ph_printers = &default_bundle.physical_printers;
    }
    //else if (preset_to_load->section_name == "presets") {
    //    // Load the names of the active presets.
    //    for (auto &kvp : *preset_to_load->node) {
    //        if (kvp.first == "print") {
    //            active_print = kvp.second.data();
    //        } else if (boost::starts_with(kvp.first, "filament")) {
    //            int idx = 0;
    //            if (kvp.first == "filament" || sscanf(kvp.first.c_str(), "filament_%d", &idx) == 1) {
    //                if (int(active_filaments.size()) <= idx)
    //                    active_filaments.resize(idx + 1, std::string());
    //                active_filaments[idx] = kvp.second.data();
    //            }
    //        } else if (kvp.first == "sla_print") {
    //            active_sla_print = kvp.second.data();
    //        } else if (kvp.first == "sla_material") {
    //            active_sla_material = kvp.second.data();
    //        } else if (kvp.first == "printer") {
    //            active_printer = kvp.second.data();
    //        } else if (kvp.first == "physical_printer") {
    //            active_physical_printer = kvp.second.data();
    //        }
    //    }
    //}
    //else if (preset_to_load->section_name == "obsolete_presets") {
    //    // Parse the names of obsolete presets. These presets will be deleted from user's
    //    // profile directory on installation of this vendor preset.
    //    for (auto &kvp : *preset_to_load->node) {
    //        std::vector<std::string> *dst = nullptr;
    //        if (kvp.first == "print")
    //            dst = &default_bundle.obsolete_presets.fff_prints;
    //        else if (kvp.first == "filament")
    //            dst = &default_bundle.obsolete_presets.filaments;
    //        else if (kvp.first == "sla_print")
    //            dst = &default_bundle.obsolete_presets.sla_prints;
    //        else if (kvp.first == "sla_material")
    //            dst = &default_bundle.obsolete_presets.sla_materials;
    //        else if (kvp.first == "printer")
    //            dst = &default_bundle.obsolete_presets.printers;
    //        if (dst)
    //            unescape_strings_cstyle(kvp.second.data(), *dst);
    //    }
    //}
    //else if (section_name == "settings") {
    //    // Load the settings.
    //    for (auto &kvp : *preset_to_load->node) {
    //        if (kvp.first == "autocenter") {
    //        }
    //    }
    //}
    else {
        // Ignore an unknown section.
        BOOST_LOG_TRIVIAL(error) << "Error, unknown section: " <<preset_to_load->section_name;
        return;
    }

    // Load the print, filament or printer preset.
    DynamicPrintConfig          default_config;
    //DynamicPrintConfig&       config = preset_to_load->con;
    //DynamicPrintConfig        config_no_prusa_convert;
    //DynamicPrintConfig        config_no_legacy_composite;
    std::string 			  alias_name;
    std::vector<std::string>  renamed_from;
    std::map<std::pair<t_config_option_key, std::string>, std::vector<std::pair<t_config_option_key, std::string>>> opts_prusa_transformed;
        std::map<t_config_option_key, std::string> opts_deleted;
    std::map<std::string, std::string> written_ini;
    std::vector<std::string> ini_ordered_keys;
    try {
        //auto parse_config_section = [ &preset_to_load, &written_ini, &ini_ordered_keys, &alias_name, &renamed_from, &substitution_context, &path, &flags,
        //&merged_config]
                //(DynamicPrintConfig &config) {
        //set base as the default
        if (is_printer) {
            default_config = DynamicPrintConfig();
            //find printer_technology field
            for (auto &kvp : *preset_to_load->node) {
                if (kvp.first == "printer_technology") {
                    default_config.set_deserialize(kvp.first, kvp.second.data(), substitution_context);
                }
            }
            if (!default_config.has("printer_technology")) {
                //from inheritance?
                for (auto &preset_parent_ptr : preset_to_load->inherits) {
                    if (preset_parent_ptr->config.has("printer_technology")) {
                        default_config.set_key_value("printer_technology", preset_parent_ptr->config.option("printer_technology")->clone());
                    }
                }
            }
            if (!default_config.has("printer_technology")) {
                BOOST_LOG_TRIVIAL(error) << "Error in a Vendor Config Bundle \"" << path << "\": The preset \""
                                         << preset_to_load->section_name << ":" << preset_to_load->name
                                         << "\" does not contains a printer_technology field, and this is mandatory "
                                            "for a printer section.";
                return;
            }
            default_config = presets->default_preset_for(default_config).config;
        } else {
            default_config = presets->default_preset().config;
        }
        preset_to_load->config = default_config;
        //apply parents
        // Iterate in a reverse order, so the last change will be placed first in merged.
        for (auto it_inherits = preset_to_load->inherits.rbegin(); it_inherits != preset_to_load->inherits.rend(); ++it_inherits) {
            preset_to_load->config.apply((*it_inherits)->config_diff);
        }
        substitution_context.clear();
        std::unordered_map<t_config_option_key, std::pair<t_config_option_key, std::string>> dict_opt;
        for (auto &kvp : *preset_to_load->node) {
            if (kvp.first == "alias") {
                alias_name = kvp.second.data();
                BOOST_LOG_TRIVIAL(error) << "Error in a Vendor Config Bundle \"" << path << "\": The preset \""
                                            << preset_to_load->section_name << ":" << preset_to_load->name
                                            << "\" contains invalid \"alias\" key, which is being ignored.";
            } else if (kvp.first == "renamed_from") {
                if (!unescape_strings_cstyle(kvp.second.data(), renamed_from)) {
                    BOOST_LOG_TRIVIAL(error) << "Error in a Vendor Config Bundle \"" << path << "\": The preset \""
                                             << preset_to_load->section_name << ":" << preset_to_load->name
                                             << "\" contains invalid \"renamed_from\" key, which is being ignored.";
                }
            }
            // Throws on parsing error. For system presets, no substituion is being done, but an exception is thrown.
            t_config_option_key opt_key = kvp.first;
            std::string value = kvp.second.data();
            if (written_ini.find(opt_key) != written_ini.end()) {
                BOOST_LOG_TRIVIAL(error) << "Error in a Vendor Config Bundle \"" << path << "\": The preset \""
                                         << preset_to_load->section_name << ":" << preset_to_load->name
                                         << "\" contains duplicate " << opt_key << " field.";
            } else {
                written_ini[opt_key] = value;
                ini_ordered_keys.push_back(opt_key);
            }
            if ("gcode_label_objects" == opt_key) {
                std::string opt_key2 = kvp.first;
                std::string value2 = kvp.second.data();
            }
            dict_opt[opt_key] = {opt_key, value};
        }
        PrintConfigDef::handle_legacy_map(dict_opt, true);
        for (auto &[saved_key, key_val] : dict_opt) {
            auto &[opt_key, value] = key_val;
            // don't throw for an unknown key, just ignore it
            if (!opt_key.empty()) {
                 preset_to_load->config.set_deserialize(opt_key, value, substitution_context);
            } else {
                opts_deleted[saved_key] = value;
            }
        }
        preset_to_load->config_before_prusa = preset_to_load->config;

        //prusa: first try the unkown keys
        {
            std::unordered_map<t_config_option_key, std::pair<t_config_option_key, std::string>> dict_opt_from_prusa;
            std::set<t_config_option_key> not_deleted_anymore;
            for (auto &[key, value] : opts_deleted) {
                t_config_option_key opt_key = key;
                std::string opt_value = value;
                std::map<t_config_option_key, std::string> result = PrintConfigDef::from_prusa(opt_key, opt_value,
                                                                                               preset_to_load->config);
                std::pair<t_config_option_key, std::string> old_pair = {key, value};
                if (!opt_key.empty() && (opt_key != key || opt_value != value)) {
                    dict_opt_from_prusa[opt_key] = {opt_key, opt_value};
                    not_deleted_anymore.insert(key);
                    opts_prusa_transformed[old_pair].push_back({opt_key, opt_value});
                }
                for (auto &[k, v] : result) {
                    assert(!k.empty());
                    dict_opt_from_prusa[k] = {k, v};
                    not_deleted_anymore.insert(key);
                    opts_prusa_transformed[old_pair].push_back({k, v});
                }
            }
            for (auto &key : not_deleted_anymore) {
                opts_deleted.erase(key);
            }
            PrintConfigDef::handle_legacy_map(dict_opt_from_prusa, true);
            for (auto &[saved_key, key_val] : dict_opt_from_prusa) {
                auto &[opt_key, value] = key_val;
                // don't throw for an unknown key, just ignore it
                if (!opt_key.empty()) {
                    preset_to_load->config.set_deserialize(opt_key, value, substitution_context);
                } else {
                    opts_deleted[saved_key] = value;
                }
            }
            if (flags.has(PresetBundle::LoadConfigBundleAttribute::ConvertFromPrusa))
                preset_to_load->config.convert_from_prusa(true);
        }
        preset_to_load->config_before_legacy_composite = preset_to_load->config;
        preset_to_load->config.handle_legacy_composite(opts_deleted);
        //apply preset_to_load-> to no-prusa config
        //preset_to_load->config_no_prusa_convert = preset_to_load->config_before_prusa;
        //std::vector<std::pair<t_config_option_key, std::string>> opts_deleted_donot_care;
        //preset_to_load->config_no_prusa_convert.handle_legacy_composite(opts_deleted_donot_care);
        //};
        //if (presets == &default_bundle.printers) {
        //    // Select the default config based on the printer_technology field extracted from kvp.
        //    DynamicPrintConfig config_src;
        //    parse_config_section(config_src);
        //    default_config = &presets->default_preset_for(config_src).config;
        //    config = *default_config;
        //    config.apply(config_src);
        //} else {
        //    default_config = &presets->default_preset().config;
        //    config = *default_config;
        //    parse_config_section(config);
        //}
    } catch (const ConfigurationError &e) {
        throw ConfigurationError(format("Invalid configuration bundle \"%1%\", section [%2%]: ", path, preset_to_load->section_name + ":"s + preset_to_load->name) + e.what());
    }
    
    Preset::normalize(preset_to_load->config);
    //Preset::normalize(preset_to_load->config_no_prusa_convert);
    // Report configuration fields, which are misplaced into a wrong group.
    std::string incorrect_keys = Preset::remove_invalid_keys(preset_to_load->config, default_config);
    if (! incorrect_keys.empty())
        BOOST_LOG_TRIVIAL(error) << "Error in a Vendor Config Bundle \"" << path << "\": The printer preset \"" << 
            preset_to_load->section_name << ":" << preset_to_load->name << "\" contains the following incorrect keys: " << incorrect_keys << ", which were removed";
    if (flags.has(PresetBundle::LoadConfigBundleAttribute::LoadSystem) && presets == &default_bundle.printers) {
        // Filter out printer presets, which are not mentioned in the vendor profile.
        // These presets are considered not installed.
        auto printer_model   = preset_to_load->config.opt_string("printer_model");
        if (printer_model.empty()) {
            BOOST_LOG_TRIVIAL(error) << "Error in a Vendor Config Bundle \"" << path << "\": The printer preset \"" << 
                preset_to_load->section_name << ":" << preset_to_load->name << "\" defines no printer model, it will be ignored.";
            return;
        }
        auto printer_variant = preset_to_load->config.opt_string("printer_variant");
        if (printer_variant.empty()) {
            BOOST_LOG_TRIVIAL(error) << "Error in a Vendor Config Bundle \"" << path << "\": The printer preset \"" << 
                preset_to_load->section_name << ":" << preset_to_load->name << "\" defines no printer variant, it will be ignored.";
            return;
        }
        auto it_model = std::find_if(vendor_profile.models.cbegin(), vendor_profile.models.cend(),
            [&](const VendorProfile::PrinterModel &m) { return m.id == printer_model; }
        );
        if (it_model == vendor_profile.models.end()) {
            BOOST_LOG_TRIVIAL(error) << "Error in a Vendor Config Bundle \"" << path << "\": The printer preset \"" << 
                preset_to_load->section_name << ":" << preset_to_load->name << "\" defines invalid printer model \"" << printer_model << "\", it will be ignored.";
            return;
        }
        auto it_variant = it_model->variant(printer_variant);
        if (it_variant == nullptr) {
            BOOST_LOG_TRIVIAL(error) << "Error in a Vendor Config Bundle \"" << path << "\": The printer preset \"" << 
                preset_to_load->section_name << ":" << preset_to_load->name << "\" defines invalid printer variant \"" << printer_variant << "\", it will be ignored.";
            return;
        }
        const Preset *preset_existing = presets->find_preset(preset_to_load->section_name + ":"s + preset_to_load->name, false);
        if (preset_existing != nullptr) {
            BOOST_LOG_TRIVIAL(error) << "Error in a Vendor Config Bundle \"" << path << "\": The printer preset \"" << 
                preset_to_load->section_name << ":" << preset_to_load->name << "\" has already been loaded from another Confing Bundle.";
            return;
        }
    } else if (! flags.has(PresetBundle::LoadConfigBundleAttribute::LoadSystem)) {
        // This is a user config bundle.
        const Preset *existing = presets->find_preset(preset_to_load->name_with_template, false);
        if (existing != nullptr) {
            if (existing->is_system) {
    			assert(existing->vendor != nullptr);
                BOOST_LOG_TRIVIAL(error) << "Error in a user provided Config Bundle \"" << path << "\": The " << presets->name() << " preset \"" << 
    				existing->name << "\" is a system preset of vendor " << existing->vendor->name << " and it will be ignored.";
                return;
            } else {
                assert(existing->vendor == nullptr);
                BOOST_LOG_TRIVIAL(trace) << "A " << presets->name() << " preset \"" << existing->name << "\" was overwritten with a preset from user Config Bundle \"" << path << "\"";
            }
        } else {
			BOOST_LOG_TRIVIAL(trace) << "A new " << presets->name() << " preset \"" << preset_to_load->name_with_template << "\" was imported from user Config Bundle \"" << path << "\"";
        }
    }
//    // Decide a full path to this .ini file 
//    auto file_name = boost::algorithm::iends_with(preset_to_load->name_with_template, ".ini") ? preset_to_load->name_with_template : preset_to_load->name_with_template + ".ini";
//    auto file_path = (boost::filesystem::path(data_dir()) 
//#ifdef SLIC3R_PROFILE_USE_PRESETS_SUBDIR
//        // Store the print/filament/printer presets into a "presets" directory.
//        / "presets" 
//#else
//        // Store the print/filament/printer presets at the same location as the upstream Slic3r.
//#endif
//        / presets->section_name() / file_name).make_preferred();
//    // Load the preset into the list of presets, save it to disk.
//    Preset &loaded = presets->load_preset(file_path.string(), preset_to_load->name_with_template, std::move(config), false);
//    if (flags.has(PresetBundle::LoadConfigBundleAttribute::SaveImported))
//        loaded.save();
//    if (flags.has(PresetBundle::LoadConfigBundleAttribute::LoadSystem)) {
//        loaded.is_system = true;
//        loaded.vendor = &vendor_profile;
//    }
//
//    // Derive the profile logical name aka alias from the preset name if the alias was not stated explicitely.
//    if (alias_name.empty()) {
//        size_t end_pos = preset_to_load->name_with_template.find_first_of("@");
//	    if (end_pos != std::string::npos) {
//	        alias_name = preset_to_load->name_with_template.substr(0, end_pos);
//	        if (renamed_from.empty())
//	            // Add the preset name with the '@' character removed into the "renamed_from" list.
//	            renamed_from.emplace_back(alias_name + preset_to_load->name_with_template.substr(end_pos + 1));
//            boost::trim_right(alias_name);
//	    }
//	}
//	if (alias_name.empty())
//	    loaded.alias = preset_to_load->name_with_template;
//	else 
//	    loaded.alias = std::move(alias_name);
//	loaded.renamed_from = std::move(renamed_from);
    if (! substitution_context.empty())
        substitutions.push_back({preset_to_load->name_with_template, presets->type(), PresetConfigSubstitutions::Source::ConfigBundle,
                                    std::string(), std::move(substitution_context).data()});

    std::vector<std::string> diff_keys;

    //populate config diff
    diff_keys = config_diffs(preset_to_load->config, default_config);
    for (const std::string &key : diff_keys) {
        if (preset_to_load->config.has(key)) {
            preset_to_load->config_diff.set_deserialize(key, preset_to_load->config.option(key)->serialize());
        }
    }

    //compare the ini settigns to the fixed profile
    std::cout << " ===== list of legacy vconverted settings: ===== \n";
    for (const std::string &key : ini_ordered_keys) {
        if (!preset_to_load->config.option(key)) {
            std::string opt_key = key;
            std::string value = written_ini[key];
            std::cout << "'" << opt_key << " = " << value << "'";
            PrintConfigDef::handle_legacy_pair(opt_key, value, true);
            if (opt_key.empty()) {
                if (opts_deleted.find(key) != opts_deleted.end()) {
                    std::cout << " => legacy delete. please remove this deprecated setting.";
                } else {
                    std::cout << " => transformed into something (see below), please delete this legacy value when you've written the new one.";
                }
            } else if (opt_key != key || value != written_ini[key]) {
                std::cout << " => legacy convert:'" << opt_key << " = " << value << "'";
            }
            std::cout << "\n";
        }
    }
    diff_keys = config_diffs(preset_to_load->config_before_legacy_composite, preset_to_load->config_before_prusa);
    std::cout << " ===== list of settings converted from prusa: " << diff_keys.size() << " ===== \n";
    for (const std::string &key : diff_keys) {
        std::cout << "'" << key << " = " << (preset_to_load->config_before_prusa.option(key) ? preset_to_load->config_before_prusa.option(key)->serialize() : "") << "'";
        std::cout << "=> '" << key << " = " << (preset_to_load->config_before_legacy_composite.option(key) ? preset_to_load->config_before_legacy_composite.option(key)->serialize() : "") << "'";
        std::cout << "\n";
    }
    for (auto &entry : opts_prusa_transformed) {
        auto &old_pair = entry.first;
        auto &vector = entry.second;
        std::cout << "'" << old_pair.first << " = " << old_pair.second << "'";
        if (vector.size() == 1) {
            std::cout << "=> '" << vector.front().first << " = " << vector.front().second << "'";
            std::cout << "\n";
        } else {
            std::cout << "=> [\n";
            for (auto &new_pair : vector) {
                std::cout << "        '" << new_pair.first << " = " << new_pair.second << "'\n";
            }
            std::cout<<"    ]\n";
        }

    }
    diff_keys = config_diffs(preset_to_load->config, preset_to_load->config_before_legacy_composite);
    std::cout << " ===== list of finals changes need to move from an old setting: " << diff_keys.size() << " ===== \n";
    for (const std::string &key : diff_keys) {
        std::cout << "'" << key << " = " << (preset_to_load->config_before_legacy_composite.option(key) ? preset_to_load->config_before_legacy_composite.option(key)->serialize() : "") << "'";
        std::cout << "=> '" << key << " = " << (preset_to_load->config.option(key) ? preset_to_load->config.option(key)->serialize() : "") << "'";
        std::cout << "\n";
    }
    if (!preset_to_load->config_before_legacy_composite.option("print_version")) {
        std::cout << "'print_version = '";
        std::cout << "=> 'print_version = " << ConfigOptionStringVersion().serialize() << "'";
        std::cout << "\n";
    }

}

std::vector<PrstPtr> get_configbundle_hierarchy(boost::property_tree::ptree &tree, const std::string &group_name)
{
    namespace pt = boost::property_tree;

    struct ComparePrstPtr
    {
        bool operator()(const PrstPtr &a, const PrstPtr &b) const { return *a < *b; }
    };
    // Find the presets, store them into a std::map, addressed by their names.
    std::set<PrstPtr, ComparePrstPtr> presets;
    std::string group_name_preset = group_name + ":";
    for (auto &section : tree)
        if (boost::starts_with(section.first, group_name_preset) && section.first.size() > group_name_preset.size())
            presets.emplace(std::make_shared<Prst>(group_name, section.first.substr(group_name_preset.size()), &section.second));
    // Fill in the "inherits" and "parent_of" members, report invalid inheritance fields.
    for (const PrstPtr &prst : presets) {
        // Parse the list of comma separated values, possibly enclosed in quotes.
        std::vector<std::string> inherits_names;
        std::vector<std::string> inherits_system;
        if (Slic3r::unescape_strings_cstyle(prst->node->get<std::string>("inherits", ""), inherits_names)) {
            // Resolve the inheritance by name.
            std::vector<PrstPtr> &inherits_nodes = prst->inherits;
            for (const std::string &node_name : inherits_names) {
                auto temp = std::make_shared<Prst>(group_name, node_name, nullptr);
                auto it = presets.find(temp);
                if (it == presets.end())
                    BOOST_LOG_TRIVIAL(error) << "flatten_configbundle_hierarchy: The preset " << prst->name << " inherits an unknown preset \"" << node_name << "\"";
                else {
                    inherits_nodes.push_back(*it);
                    inherits_nodes.back()->parent_of.push_back(prst);
                }
            }
        } else {
            BOOST_LOG_TRIVIAL(error) << "flatten_configbundle_hierarchy: The preset " << prst->name << " has an invalid \"inherits\" field";
        }
        // Remove the "inherits" key, it has no meaning outside of the config bundle.
        prst->node->erase("inherits");
        if (! inherits_system.empty()) {
            // Loaded a user config bundle, where a profile inherits a system profile.
			// User profile should be derived from a single system profile only.
			assert(inherits_system.size() == 1);
			if (inherits_system.size() > 1)
				BOOST_LOG_TRIVIAL(error) << "flatten_configbundle_hierarchy: The preset " << prst->name << " inherits from more than single system preset";
			prst->node->put("inherits", Slic3r::escape_string_cstyle(inherits_system.front()));
        }
    }

    // 2) Create a linear ordering for the directed acyclic graph of preset inheritance.
    // https://en.wikipedia.org/wiki/Topological_sorting
    // Kahn's algorithm.
    std::vector<PrstPtr> sorted;
    {
        // Initialize S with the set of all nodes with no incoming edge.
        std::deque<PrstPtr> S;
        for (const PrstPtr &prst : presets)
            if (prst->inherits.empty())
                S.push_back(prst);
            else
               prst->num_incoming_edges_left = prst->inherits.size();
        while (! S.empty()) {
            PrstPtr n = S.front();
            S.pop_front();
            sorted.push_back(n);
            for (PrstPtr m : n->parent_of) {
                assert(m->num_incoming_edges_left > 0);
                if (-- m->num_incoming_edges_left == 0) {
                    // We have visited all parents of m.
                    S.push_back(m);
                }
            }
        }
        if (sorted.size() < presets.size()) {
            for (const PrstPtr &prst : presets)
                if (prst->num_incoming_edges_left)
                    BOOST_LOG_TRIVIAL(error) << "flatten_configbundle_hierarchy: The preset " << prst->name << " has cyclic dependencies";
        }
    }

    return sorted;

    //// Apply the dependencies in their topological ordering.
    //for (PrstPtr prst : sorted) {
    //    // Merge the preset nodes in their order of application.
    //    // Iterate in a reverse order, so the last change will be placed first in merged.
    //    for (auto it_inherits = prst->inherits.rbegin(); it_inherits != prst->inherits.rend(); ++ it_inherits)
    //        for (auto it = (*it_inherits)->node->begin(); it != (*it_inherits)->node->end(); ++ it)
				//if (it->first == "renamed_from") {
    //        		// Don't inherit "renamed_from" flag, it does not make sense. The "renamed_from" flag only makes sense for a concrete preset.
    //        		if (boost::starts_with((*it_inherits)->name, "*"))
			 //           BOOST_LOG_TRIVIAL(error) << boost::format("Nonpublic intermediate preset %1% contains a \"renamed_from\" field, which is ignored") % (*it_inherits)->name;
				//} else if (prst->node->find(it->first) == prst->node->not_found())
    //                prst->node->add_child(it->first, it->second);
    //}

    //// Remove the "internal" presets from the ptree. These presets are marked with '*'.
    //group_name_preset += '*';
    //for (auto it_section = tree.begin(); it_section != tree.end(); ) {
    //    if (boost::starts_with(it_section->first, group_name_preset) && it_section->first.size() > group_name_preset.size())
    //        // Remove the "internal" preset from the ptree.
    //        it_section = tree.erase(it_section);
    //    else
    //        // Keep the preset.
    //        ++ it_section;
    //}
}

void update_preset(const VendorProfile &vendor_profile, PrstPtr preset_to_update) {
    if (!preset_to_update->config.empty()) {
        //alread updated
        return;
    }
    DynamicPrintConfig merged_config;
    // Merge the preset nodes in their order of application.
    // Iterate in a reverse order, so the last change will be placed first in merged.
    for (auto it_inherits = preset_to_update->inherits.rbegin(); it_inherits != preset_to_update->inherits.rend(); ++it_inherits) {
        //first ensure parents are updated
        update_preset(vendor_profile, (*it_inherits));
        for (auto it = (*it_inherits)->node->begin(); it != (*it_inherits)->node->end(); ++it) {
            if ((it)->first == "renamed_from") {
                // Don't inherit "renamed_from" flag, it does not make sense. The "renamed_from" flag only makes sense
                // for a concrete preset.
                if (boost::starts_with((*it_inherits)->name, "*"))
                    BOOST_LOG_TRIVIAL(error) << boost::format("Nonpublic intermediate preset %1% contains a "
                                                              "\"renamed_from\" field, which is ignored") %
                            (*it_inherits)->name;
            }
            // else if (preset_to_update->node->find((*it)->first) == preset_to_update->node->not_found()) {
            //    preset_to_update->node->add_child((*it)->first, (*it)->second);
            //}
        }
        merged_config.apply((*it_inherits)->config);
    }
    // now we have the parent config
    // use it to parse our config
    load_preset(vendor_profile, preset_to_update);
    
}

std::pair<VendorProfile, std::vector<PrstPtr>> load_sections(const std::string &path) {
    
    // 1) Read the complete config file into a boost::property_tree.
    namespace pt = boost::property_tree;
    pt::ptree tree;
    {
        boost::nowide::ifstream ifs(path);
        try {
            pt::read_ini(ifs, tree);
        } catch (const boost::property_tree::ini_parser::ini_parser_error &err) {
            BOOST_LOG_TRIVIAL(error) << "Failed loading config bundle \""
                     <<path
                     <<"\"\nError: \""
                     << err.message()
                     <<"\" at line "
                     <<err.line()
                     <<"\n";
        }
    }

    VendorProfile vp = VendorProfile::from_ini(tree, path);
    if (vp.models.size() == 0 && !vp.templates_profile) {
        BOOST_LOG_TRIVIAL(error) << boost::format("Vendor bundle: `%1%`: No printer model defined.") % path;
        return {};
    } else if (vp.num_variants() == 0 && !vp.templates_profile) {
        BOOST_LOG_TRIVIAL(error) << boost::format("Vendor bundle: `%1%`: No printer variant defined") % path;
        return {};
    }

    cout<<"Vendor profile '"<<vp.name<<"' loaded\n";
    cout << "full name: '" << vp.full_name << "'\n";
    cout << "id: '" << vp.id << "'\n";
    cout << "config_update_url: '" << vp.config_update_url << "'\n";
    cout << "changelog_url: '" << vp.changelog_url << "'\n";
    cout << "technologies:";
    for (PrinterTechnology pt : vp.technologies)
        cout << " '" << get_tech_str(pt) << "'";
    cout << "\n";
    cout << "templates_profile ? " << vp.templates_profile << "\n";

    cout<<"families: \n";
    for (auto [familiy, line_size] : vp.family_2_line_size) {
        cout << "  " << familiy << "\n";
    }
    cout<<"models: \n";
    for (const VendorProfile::PrinterModel& model : vp.models) {
        cout << "id:" << model.id<<", name: "<<model.name<<", tech:"<<get_tech_str(model.technology)<<", family:"<<model.family<< "\n";
        cout << "    bed_model:"<<model.bed_model<<", bed texture:"<<model.bed_texture<<", bed_grid:"<<model.bed_with_grid<<", thumbnail:"<<model.thumbnail<<"\n";
        cout << "    variants:[";
        for(auto var : model.variants) cout<<" '"<<var.name<<"'";
        cout<<"]\n";
        cout << "    default materials:[";
        for(auto mat : model.default_materials) cout<<" '"<<mat<<"'";
        cout<<"]\n";
    }
    

    std::vector<PrstPtr> ordered_presets_print = get_configbundle_hierarchy(tree, "print");
    // Apply the dependencies in their topological ordering.
    for (PrstPtr &prst : ordered_presets_print) {
        update_preset(vp, prst);
    }
    std::vector<PrstPtr> ordered_presets_sla_print = get_configbundle_hierarchy(tree, "sla_print");
    // Apply the dependencies in their topological ordering.
    for (PrstPtr &prst : ordered_presets_sla_print) {
        update_preset(vp, prst);
    }
    std::vector<PrstPtr> ordered_presets_printer = get_configbundle_hierarchy(tree, "printer");
    // Apply the dependencies in their topological ordering.
    for (PrstPtr &prst : ordered_presets_printer) {
        update_preset(vp, prst);
    }
    std::vector<PrstPtr> ordered_presets_filament = get_configbundle_hierarchy(tree, "filament");
    // Apply the dependencies in their topological ordering.
    for (PrstPtr &prst : ordered_presets_filament) {
        update_preset(vp, prst);
    }
    std::vector<PrstPtr> ordered_presets_material = get_configbundle_hierarchy(tree, "material");
    // Apply the dependencies in their topological ordering.
    for (PrstPtr &prst : ordered_presets_material) {
        update_preset(vp, prst);
    }
    //load_preset(vp, std::string section_name, boost::property_tree::ptree section_data)

    std::vector<PrstPtr> all;
    append(all, ordered_presets_print);
    append(all, ordered_presets_sla_print);
    append(all, ordered_presets_printer);
    append(all, ordered_presets_filament);
    append(all, ordered_presets_material);

    return {vp, all};
}


void save(const std::string &file, VendorProfile &vp, std::vector<PrstPtr> &presets, const Semver &ver_susi)
{
    boost::nowide::ofstream c;
    c.open(file, std::ios::out | std::ios::trunc);
    c << "# " << Slic3r::header_slic3r_generated() << std::endl;
    c << "# Generated to " SLIC3R_APP_NAME;

    c << "\n";
    c << "[vendor]\n";
    c << "id = " << vp.id << "\n";
    c << "name = " << vp.name << "\n";
    c << "full_name = " << vp.full_name << "\n";
    c << "config_version = " << ver_susi << "\n";
    c << "config_update_url = \n# " << vp.config_update_url << "\n";
    c << "changelog_url = " << vp.changelog_url << "\n";
    c << "technologies = ";
    for (PrinterTechnology &tech : vp.technologies) {
        if (tech != vp.technologies.front())
            c <<"; ";
        c << to_string(tech);
    }
    c << "\n";

    // note: default_filaments & default_sla_materials are not used!

    //printer_model sections
    for (VendorProfile::PrinterModel &pmodel : vp.models) {
        c << "\n[printer_model:"<<pmodel.id<<"]\n";
        c << "name = " << pmodel.name << "\n";
        c << "variants = ";
        for (VendorProfile::PrinterVariant &variant : pmodel.variants) {
            if (variant.name != pmodel.variants.front().name)
                c <<"; ";
            c << variant.name;
        }
        c << "\n";
        c << "technology = " << to_string(pmodel.technology) << "\n";
        c << "family = " << pmodel.family << "\n";
        c << "bed_model = " << pmodel.bed_model << "\n";
        c << "bed_texture = " << pmodel.bed_texture << "\n";
        c << "bed_with_grid = " << (pmodel.bed_with_grid?"1":"0") << "\n";
        c << "thumbnail = " << pmodel.thumbnail << "\n";
        c << "default_materials = ";
        for (std::string &material_id : pmodel.default_materials) {
            if (material_id != pmodel.default_materials.front())
                c <<"; ";
            c << material_id;
        }
        c << "\n";
    }

    // config presets sections
    for (PrstPtr preset : presets) {
        c << "\n";
        if (!preset->name_with_template.empty()) {
            c << "#name_with_template = " << preset->name_with_template << "\n";
        }
        c << "[" << preset->section_name << ":" << preset->name << "]\n";
        // inherits
        if (!preset->inherits.empty()) {
            c << "inherits = ";
            DynamicPrintConfig        config_inherit;
            for (std::shared_ptr<Prst> &inherit_preset : preset->inherits) {
                assert(inherit_preset);
                if (inherit_preset != preset->inherits.front())
                    c <<"; ";
                c << inherit_preset->name;
                for (std::string &key : inherit_preset->config.keys()) {
                    config_inherit.set_key_value(key, inherit_preset->config.option(key)->clone());
                }
            }
            c << "\n";
            //on ne met que le diff
            t_config_option_keys diff = preset->config.diff(config_inherit);
            for (const std::string &opt_key : diff)
                c << opt_key << " = " << preset->config.opt_serialize(opt_key) << "\n";
        } else {
            for (const std::string &opt_key : preset->config.keys())
                c << opt_key << " = " << preset->config.opt_serialize(opt_key) << "\n";
        }
    }
    c.close();
}

int main(int argc, char const *argv[])
{
    Semver slic3r_2_7_61("2.7.61-alpha+UNKNOWN");
    Semver slic3r_2_7_alpha("2.7-alpha+UNKNOWN");
    Semver slic3r_2_6_1_rc2("2.6.1-rc2");
    Semver slic3r_2_6_2_alpha0("2.6.2-alpha0");
    Semver slic3r_2_7_0_alpha2("2.7.0-alpha2");
    Semver slic3r_2_7_0_beta1("2.7.0-beta1");

    auto test = [](Semver& s1, Semver& s2){
        std::cout << ((s1 == s2) ? "equal" : ((s1 < s2) ? "lower" : "higher")) << "\n"; };
    
    test(slic3r_2_6_1_rc2,slic3r_2_6_2_alpha0);
    test(slic3r_2_6_2_alpha0,slic3r_2_7_0_alpha2);
    test(slic3r_2_7_0_alpha2,slic3r_2_7_0_beta1);
    std::cout<<"test 2.7-alpha+UNKNOWN\n";
    test(slic3r_2_7_alpha,slic3r_2_6_1_rc2);
    test(slic3r_2_7_alpha,slic3r_2_6_2_alpha0);
    test(slic3r_2_7_alpha,slic3r_2_7_0_alpha2);
    test(slic3r_2_7_alpha,slic3r_2_7_0_beta1);
    test(slic3r_2_7_alpha,slic3r_2_7_61);
    std::cout<<"2.7.61-alpha+UNKNOWN\n";
    test(slic3r_2_7_61,slic3r_2_6_1_rc2);
    test(slic3r_2_7_61,slic3r_2_6_2_alpha0);
    test(slic3r_2_7_61,slic3r_2_7_0_alpha2);
    test(slic3r_2_7_61,slic3r_2_7_0_beta1);


    clipboardxx::clipboard clipboard;
    if (argc != 2) {
        std::cout<<"usage: test_config \"path/to/vendor/config.ini\"\n";
        return 0;
    }
    std::string path_str = argv[1];
    if(path_str.front() == '\"' && path_str.back() == '\"')
        path_str = path_str.substr(1,path_str.size()-2);
    boost::filesystem::path path_in(path_str);
    
    //PresetsConfigSubstitutions  substitutions;
    //std::pair<PresetsConfigSubstitutions, size_t> res = 
    //    this->load_configbundle(path_str, PresetBundle::LoadSystem, ForwardCompatibilitySubstitutionRule::);
    auto [vp, presets] = load_sections(path_str);

    // get path & name
    boost::filesystem::path path_out = path_in.parent_path();
    path_out /= "converted";
    boost::filesystem::create_directory(path_out);
    path_out /= path_in.filename();
    std::cout<<path_in.string()<<"\n";
    std::cout<<path_out.string()<<"\n";

    //copy directory
    boost::filesystem::path path_dir = path_in.parent_path();
    boost::filesystem::path path_dir_out = path_in.parent_path();
    path_dir_out /= "converted";
    path_dir /= path_in.stem();
    path_dir_out /= path_in.stem();
    std::filesystem::copy(path_dir.string(), path_dir_out.string(), std::filesystem::copy_options::recursive);

    //ver
    Semver ver_susi  = vp.config_version;
    if (ver_susi.has_patch()) {
        ver_susi.set_patch(ver_susi.patch()+1);
    } else if (ver_susi.has_counter()) {
        ver_susi.set_counter(ver_susi.counter()+1);
    } else if (ver_susi.has_min()) {
        ver_susi.set_min(ver_susi.min()+1);
    } else if (ver_susi.has_maj()) {
        ver_susi.set_maj(ver_susi.maj()+1);
    }
    ver_susi.set_prerelease("susi");
    ver_susi.set_metadata(nullptr);

    //copy idx
    boost::filesystem::path path_idx = path_in.parent_path();
    boost::filesystem::path path_idx_out = path_in.parent_path();
    path_idx_out /= "converted";
    path_idx /= path_in.stem();
    path_idx_out /= path_in.stem();
    path_idx += ".idx";
    path_idx_out += ".idx";
    boost::nowide::ifstream ifs(path_idx.string());
    boost::nowide::ofstream c;
    c.open(path_idx_out.string(), std::ios::out | std::ios::trunc);
    string line;
    std::istream *ok = &std::getline(ifs, line);
    while (ok && boost::starts_with(line, "min_slic3r_version")) {
        c << line << endl;
        ok = &std::getline(ifs, line);
    }
    c << ver_susi.to_string() << " auto-convert to susi profile" << endl;
    c << line << endl;
    while (std::getline(ifs, line)) {
        c << line << endl;
    }

    save(path_out.string(), vp, presets, ver_susi);
    //Slic3r::Model model;
    //bool result = load_stl(path_str.c_str(), &model, "obj");
    //if (!result) {
    //    std::cout << "error, can't read '" << path_str << "'\n";
    //    return 0;
    //}
    //
    ////TriangleMesh tm2 = TriangleMesh(std::vector<Vec3f>{{-5, -5, -0.1}},std::vector<Vec3i32>{{1,4,3}});
    //std::stringstream out_cpp;
    //int idx_obj = 0;
    //for (Slic3r::ModelObject* obj : model.objects) {
    //    int idx_vol = 0;
    //    for(Slic3r::ModelVolume *vol : obj->volumes) {
    //        Slic3r::TriangleMesh mesh = vol->mesh();
    //        Slic3r::AABBMesh indexed_mesh(mesh); // more user-friendly
    //        out_cpp << "AABBMesh vol_"<< idx_obj << "_" << idx_vol <<" = AABBMesh(std::vector<Vec3f>{";
    //        int ptidx= 0;
    //        for(const Slic3r::Vec3f &pt : indexed_mesh.vertices())
    //            out_cpp << (0==ptidx++?"{":",{") << Slic3r::to_string_nozero(pt.x(), 7)
    //                     << ',' << Slic3r::to_string_nozero(pt.y(), 7)
    //                     << ',' << Slic3r::to_string_nozero(pt.z(), 7) << '}';
    //        out_cpp << "},std::vector<Vec3i32>{";
    //        ptidx= 0;
    //        for(const Slic3r::Vec3i32 &tri : indexed_mesh.indices())
    //            out_cpp << (0==ptidx++?"{":",{") << tri(0) << ',' << tri(1) << ',' << tri(2) << '}';
    //        out_cpp << "});\n";

    //        idx_vol++;
    //    }
    //    out_cpp << "\n";
    //    idx_obj++;
    //}

    //clipboard << out_cpp.str();
    //std::cout << out_cpp.str();

    return 0;
}
