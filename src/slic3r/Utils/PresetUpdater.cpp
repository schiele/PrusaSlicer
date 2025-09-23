///|/ Copyright (c) Superslicer 2025 Durand remi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "PresetUpdater.hpp"

#include <algorithm>
#include <unordered_map>
#include <ostream>
#include <utility>
#include <stdexcept>
#include <boost/algorithm/string.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/format.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/log/trivial.hpp>
#include <curl/curl.h>
#include <curl/curl.h>

#include <wx/app.h>
#include <wx/msgdlg.h>
#include <wx/progdlg.h>

#include "libslic3r/libslic3r.h"
#include "libslic3r/format.hpp"
#include "libslic3r/miniz_extension.hpp"
#include "libslic3r/Utils.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "slic3r/Config/Snapshot.hpp"
#include "slic3r/Config/Version.hpp"
#include "slic3r/GUI/ConfigWizard.hpp"
#include "slic3r/GUI/GUI.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/format.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/MsgDialog.hpp"
#include "slic3r/GUI/NotificationManager.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/UpdateDialogs.hpp"
#include "slic3r/Utils/Http.hpp"

namespace fs = boost::filesystem;
using Slic3r::GUI::Config::Index;
using Slic3r::GUI::Config::Version;
using Slic3r::GUI::Config::Snapshot;
using Slic3r::GUI::Config::SnapshotDB;

namespace Slic3r {

wxDEFINE_EVENT(EVT_CONFIG_UPDATER_SYNC_DONE, wxCommandEvent);

PresetUpdater::PresetUpdater() {}

PresetUpdater::~PresetUpdater() {
    if (thread.joinable()) {
        // This will stop transfers being done by the thread, if any.
        // Cancelling takes some time, but should complete soon enough.
        cancel = true;
        thread.join();
    }
}

void PresetUpdater::set_installed_vendors(const PresetBundle *preset_bundle) {
    std::lock_guard<std::recursive_mutex> guard(this->all_vendors_mutex);
    for (const auto& [id, installed_vendor] : preset_bundle->vendors) {
        auto it = all_vendors.find(installed_vendor.id);
        if (it == all_vendors.end()) {
            this->all_vendors[installed_vendor.id] = (VendorSync{installed_vendor, true});
            if (!installed_vendor.config_update_github.empty()) {
                is_synch = false;
            }
        } else {
            it->second.reset(installed_vendor, true);
        }
    }
}

void PresetUpdater::load_unused_vendors(std::set<std::string> &vendors_id, const boost::filesystem::path vendor_dir, bool is_installed) {
    std::vector<boost::filesystem::path> vendors;
    if (boost::filesystem::exists(vendor_dir)) {
        for (const boost::filesystem::directory_entry &prg_dir : boost::filesystem::directory_iterator(vendor_dir)) {
            if (prg_dir.status().type() == boost::filesystem::file_type::regular_file) {
                boost::filesystem::path file_path = prg_dir.path();
                //assert(file_path.extension() == ".ini");
                if (file_path.extension() != ".ini") {
                    continue;
                }
                if (vendors_id.find(file_path.stem().string()) == vendors_id.end()) {
                    vendors.push_back(file_path);
                }
            }
        }
    }
    // read them
    std::lock_guard<std::recursive_mutex> guard(this->all_vendors_mutex);
    for (boost::filesystem::path &path : vendors) {
        VendorProfile vp = VendorProfile::from_ini(path, false);
        auto it = all_vendors.find(vp.id);
        if (it == all_vendors.end() || it->second.available_profiles.empty()) {
            if (!vp.config_update_github.empty()) {
                is_synch = false;
            }
            this->all_vendors[vp.id] = VendorSync{vp, false};
            this->all_vendors[vp.id].is_installed = is_installed;
            if (vp.config_version != Semver()) {
                // this vendor spec has a version
                this->all_vendors[vp.id].available_profiles.emplace_back(
                    VendorAvailable{vp.config_version, vp.slicer_version, path.string(), "", "", "", true, std::string()});
            }
            vendors_id.insert(vp.id);
            // now order by version
            this->all_vendors[vp.id].sort_available();
        } else {
            it->second.reset(vp, is_installed);
        }
    }
}

void PresetUpdater::reload_all_vendors() {
    //this->unused_vendors.clear();
    std::set<std::string> vendors_id;

    boost::filesystem::path configuration_path(Slic3r::data_dir());
    boost::filesystem::path resources_path(Slic3r::resources_dir());


    // read all vendor from configuration
    load_unused_vendors(vendors_id, configuration_path / "vendor", /*is_installed=*/ true);

    {
        std::lock_guard<std::recursive_mutex> guard(this->all_vendors_mutex);
        for (const auto &[id, vendor_synch] : this->all_vendors) {
            if (vendor_synch.is_installed) {
                vendors_id.insert(vendor_synch.profile.id);
            }
        }
    }
    //FIXME: load "available_profiles"
    // read all vendor from cache (added but not installed)
    load_unused_vendors(vendors_id, configuration_path / "cache" / "vendor", /*is_installed=*/ false);

    // get our vendors from resources;
    assert(boost::filesystem::exists(resources_path / "profiles"));
    load_unused_vendors(vendors_id, resources_path / "profiles", /*is_installed=*/ false);

}

void PresetUpdater::download_logs(VendorSync &vendor, std::function<void(bool)> callback_result, bool force) {
    // note: all_vendors shoudln't change since vendor was stored somewhere elese.
    std::lock_guard<std::mutex> guard(this->callback_update_changelog_mutex);
    changelog_synch = vendor.available_profiles.size();
    // for each tag
    for (size_t available_idx = 0; available_idx < vendor.available_profiles.size(); available_idx++) {
        VendorAvailable &version = vendor.available_profiles[available_idx];
        if (version.commit_url.empty()) {
            continue;
        }
        Semver slicer_major_ver = version.slicer_version.no_patch();
        //is there older versions with same slicer major version?
        Semver best_previous_version = Semver::zero();
        size_t best_idx = 0;
        for (size_t i_prev = 0; i_prev < vendor.available_profiles.size(); i_prev++) {
            if (!vendor.available_profiles[i_prev].commit_sha.empty() &&
                vendor.available_profiles[i_prev].config_version < version.config_version) {
                if (best_previous_version < vendor.available_profiles[i_prev].config_version) {
                    if (vendor.available_profiles[i_prev].slicer_version.no_patch() == slicer_major_ver) {
                        best_idx = i_prev;
                        best_previous_version = vendor.available_profiles[i_prev].config_version;
                    }
                }
            }
        }
        if (best_idx == 0) {
            assert(best_previous_version == Semver::zero());
            //can't find a previous version with same slicer version, choose best one
            // find the previous available profile (if any)
            for (size_t i_prev = 0; i_prev < vendor.available_profiles.size(); i_prev++) {
                if (!vendor.available_profiles[i_prev].commit_sha.empty() &&
                    vendor.available_profiles[i_prev].config_version < version.config_version) {
                    if (best_previous_version < vendor.available_profiles[i_prev].config_version) {
                        if (vendor.available_profiles[i_prev].slicer_version.no_patch() <= slicer_major_ver) {
                            best_idx = i_prev;
                            best_previous_version = vendor.available_profiles[i_prev].config_version;
                        }
                    }
                }
            }
        }
        if (best_idx == 0) {
            boost::filesystem::path cache_path = GUI::into_path(data_dir()) / "cache" / "vendor" / "temp" / vendor.profile.id /
                (version.commit_sha + ".json");
            if (boost::filesystem::exists(cache_path) && !force) {
                // reuse the cache file
                boost::nowide::ifstream file(cache_path.string());
                std::string body { std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };
                boost::property_tree::ptree root;
                std::stringstream json_stream(body);
                boost::property_tree::read_json(json_stream, root);
                version.notes = root.get<std::string>("commit.message");
                if (--changelog_synch == 0) {
                    callback_result(true);
                }
            } else if (has_api_request_slot()) {
                // root
                Http::get(version.commit_url)
                    .size_limit(1024 * 64 /*64kio, 100tags should use 27ko*/)
                    .on_error([this, &version, callback_result](std::string body, std::string error,
                                                                unsigned http_status) {
                        if (--changelog_synch == 0) {
                            callback_result(false);
                        }
                    })
                    .on_complete([this, &version, cache_path, callback_result](std::string body,
                                                                                unsigned /* http_status */) {
                        // store
                        FILE *fp = boost::nowide::fopen(cache_path.string().c_str(), "w");
                        if (fp == nullptr) {
                            BOOST_LOG_TRIVIAL(error) << "PresetUpdater::download_logs: Couldn't open "
                                                        << cache_path.string().c_str() << " for writing";
                        } else {
                            fprintf(fp, body.c_str());
                            fclose(fp);
                        }
                        // parse it
                        boost::property_tree::ptree root;
                        std::stringstream json_stream(body);
                        boost::property_tree::read_json(json_stream, root);
                        version.notes = root.get<std::string>("commit.message");
                        if (--changelog_synch == 0) {
                            callback_result(true);
                        }
                    })
                    .perform();
                
            }
        } else {
            boost::filesystem::path cache_path = GUI::into_path(data_dir()) / "cache" / "vendor" / "temp" / vendor.profile.id /
                (vendor.available_profiles[best_idx].tag + "..." + version.tag + ".json");
            if (boost::filesystem::exists(cache_path) && !force) {
                // reuse the cache file
                boost::nowide::ifstream file(cache_path.string());
                std::string body { std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };
                boost::property_tree::ptree root;
                std::stringstream json_stream(body);
                boost::property_tree::read_json(json_stream, root);
                for (boost::property_tree::ptree::value_type &kv : root.get_child("commits")) {
                    if (!version.notes.empty()) {
                        version.notes += "\n";
                    }
                    version.notes += kv.second.get<std::string>("commit.message");
                }
                if (--changelog_synch == 0) {
                    callback_result(true);
                }
            } else if (has_api_request_slot()) {
                std::string url(std::string("https://api.github.com/repos/") + vendor.profile.config_update_github +
                                "/compare/" + vendor.available_profiles[best_idx].tag + "..." + version.tag);
                Http::get(url)
                    .size_limit(1024 * 64 /*64kio, 100tags should use 27ko*/)
                    .on_error(
                        [this, &version, callback_result](std::string body, std::string error, unsigned http_status) {
                            if (--changelog_synch == 0) {
                                callback_result(false);
                            }
                        })
                    .on_complete([this, &version, cache_path, callback_result](std::string body, unsigned /* http_status */) {
                        // store
                        FILE *fp = boost::nowide::fopen(cache_path.string().c_str(), "w");
                        if (fp == nullptr) {
                            BOOST_LOG_TRIVIAL(error) << "PresetUpdater::download_logs: Couldn't open " << cache_path.string().c_str() << " for writing";
                        } else {
                            fprintf(fp, body.c_str());
                            fclose(fp);
                        }
                        // parse it
                        boost::property_tree::ptree root;
                        std::stringstream json_stream(body);
                        boost::property_tree::read_json(json_stream, root);
                        for (boost::property_tree::ptree::value_type &kv : root.get_child("commits")) {
                            if (!version.notes.empty()) {
                                version.notes += "\n";
                            }
                            version.notes += kv.second.get<std::string>("commit.message");
                        }
                        if (--changelog_synch == 0) {
                            callback_result(true);
                        }
                    })
                    .perform();
            }
        }
    }
}

void PresetUpdater::sync_async(std::function<void(int)> callback_update_preset, bool force) {
    if (this->all_vendors.empty()) {
        callback_update_preset(get_profile_count_to_update());
        // already done
        return;
    }
    bool error = false;
    {
        std::lock_guard<std::mutex> guard(this->callback_update_preset_mutex);
        bool already_in_synch = synch_process_ongoing.exchange(true); // atomic isn't necessary if behind lock
        if (already_in_synch) {
            // shouldn't be happening, please caller, test it beforehand.
            assert(false);
            error= true;
        } else {
            this->callback_update_preset = callback_update_preset;
        }
    }
    if (error) {
        callback_update_preset(get_profile_count_to_update());
        return;
    }
    std::lock_guard<std::recursive_mutex> vendors_guard(this->all_vendors_mutex);
    profiles_synch = this->all_vendors.size();
    for (auto& [id, vendor_synch] : this->all_vendors) {
        this->update_vendor(vendor_synch, force);
    }
}

void PresetUpdater::update_vendor(VendorSync &vendor, bool force) {
    // reuse
    if (!vendor.raw_tags.empty()) {
        assert(vendor.is_synch);
        end_updating();
        return;
    }
    boost::filesystem::path cache_path = GUI::into_path(data_dir()) / "cache" / "vendor" / (vendor.profile.id + "_tags.json");
    if (boost::filesystem::exists(cache_path) && !force) {
        std::time_t last_mod  = boost::filesystem::last_write_time(cache_path);
        std::time_t now = std::time(nullptr);
        // wait a day before checking again
        if (last_mod + 24 + 3600 < now) {
            // reuse the cache file
            boost::nowide::ifstream file(cache_path.string());
            std::string body { std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };
            vendor.parse_tags(body);
            // end it
            vendor.synch_in_progress = false;
            end_updating();
            return;
        }
    }
    vendor.synch_in_progress = true;
    if (vendor.profile.config_update_github.size() < 5 || vendor.profile.config_update_github.find('/') == std::string::npos) {
        //not correct config_update_github
        vendor.synch_failed = true;
        vendor.synch_in_progress = false;
        end_updating();
        return;
    }
    // create url to get tags from github
    std::string url(std::string("https://api.github.com/repos/") + vendor.profile.config_update_github +
                    "/tags?per_page=100;page=1");
    if (has_api_request_slot()) {
        Http::get(url)
            .size_limit(1024 * 64 /*64kio, 100tags should use 27ko*/)
            .on_error([&](std::string body, std::string error, unsigned http_status) {
                vendor.synch_failed = true;
                vendor.synch_in_progress = false;

                end_updating();
            })
            .on_complete([this, cache_path, &vendor](std::string body, unsigned /* http_status */) {
                // store it
                FILE *fp = boost::nowide::fopen(cache_path.string().c_str(), "w");
                if (fp == nullptr) {
                    BOOST_LOG_TRIVIAL(error) << "PresetUpdater::install_new_repo: Couldn't open "
                                             << cache_path.string().c_str() << " for writing";
                } else {
                    fprintf(fp, body.c_str());
                    fclose(fp);
                }
                // parse it
                vendor.parse_tags(body);

                // end it
                vendor.synch_in_progress = false;
                end_updating();
            })
            .perform();
    } else {
        vendor.synch_failed = true;
        vendor.synch_in_progress = false;
        end_updating();
    }
}

void VendorSync::reset(const VendorProfile &vprofile, bool installed) {
    this->is_installed = installed;
    this->synch_in_progress = false;
    this->synch_failed = false;
    //available_profiles = {};
    this->profile = vprofile;
    //this->can_upgrade = parse_tags(raw_tags);
    this->can_upgrade = !available_profiles.empty() && available_profiles.front().config_version > profile.config_version;
}

bool VendorSync::parse_tags(const std::string &json) {
    this->raw_tags = json;
    boost::property_tree::ptree root;
    std::stringstream json_stream(json);
    boost::property_tree::read_json(json_stream, root);

    std::set<std::string> versions_here;
    for (VendorAvailable &version : available_profiles) {
        versions_here.emplace(version.config_version.to_string());
    }

    Semver current_version(SLIC3R_VERSION_FULL);
    const std::regex reg_num("([0-9]+)");
    for (auto json_version : root) {
        std::string tag = json_version.second.get<std::string>("name");
        size_t equal_pos = tag.find('=');
        assert(equal_pos != std::string::npos);
        if (equal_pos == std::string::npos) {
            continue;
        }
        std::string profile_version = tag.substr(0, equal_pos);
        std::optional<Semver> config_version = Semver::parse(tag.substr(0, equal_pos));
        std::optional<Semver> slicer_version = Semver::parse(tag.substr(equal_pos+1));
        if (!config_version || !slicer_version) {
            assert(false);
            continue;
        }
        // check if slicer_version is okay
        std::string str_ver = slicer_version->to_string();
        std::string str_curr_ver = SLIC3R_VERSION_FULL;
        if (slicer_version > *Semver::parse(SLIC3R_VERSION_FULL) || versions_here.find(config_version->to_string()) != versions_here.end()) {
            continue;
        }
        // okay, add it
        const boost::property_tree::ptree& commit_node = json_version.second.get_child("commit");
        available_profiles.emplace_back(
            VendorAvailable{*config_version, *slicer_version,
                            json_version.second.get<std::string>("zipball_url"),
                            commit_node.get<std::string>("sha"),
                            commit_node.get<std::string>("url"), 
                            json_version.second.get<std::string>("name"), 
                            false, std::string()});
        versions_here.insert(config_version->to_string());
    }

    // now order by version
    sort_available();

    this->can_upgrade = !available_profiles.empty() && available_profiles.front().config_version > profile.config_version;
    this->is_synch = true;
    return this->can_upgrade;
}

void VendorSync::sort_available() {
    std::sort(available_profiles.begin(), available_profiles.end(),
              [](const VendorAvailable &e1, const VendorAvailable &e2) {
        if (e1.slicer_version == e2.slicer_version) {
            return e1.config_version > e2.config_version;
        } else {
            return e1.slicer_version > e2.slicer_version;
        }
    });
    // choose best (ie first with compatible slicer)
    Semver current_slicer_version = *Semver::parse(SLIC3R_VERSION_FULL);
    for (VendorAvailable &version : available_profiles) {
        if (version.slicer_version <= current_slicer_version) {
            best = &version;
            return;
        }
    }
}

void PresetUpdater::end_updating() {
    int value = --profiles_synch;
    assert(value >= 0);
    if (value == 0) {
        std::lock_guard<std::mutex> guard(this->callback_update_preset_mutex);
        // nobody increase it and the min is 0 so no race condition here.
        // end of synhc, emit callback
        this->callback_update_preset(int(get_profile_count_to_update()));
        //wxCommandEvent *evt = new wxCommandEvent(EVT_CONFIG_UPDATER_SYNC_DONE);
        //evt->SetInt(profiles_to_update);
        //evt_handler->QueueEvent(evt);
        is_synch = true;
        synch_process_ongoing = false;
    }
}

int PresetUpdater::get_profile_count_to_update() {
    int count = 0;
    std::lock_guard<std::recursive_mutex> guard(this->all_vendors_mutex);
    for (const auto &[id, vendor_synch] : this->all_vendors) {
        if (vendor_synch.can_upgrade) {
            count++;
        }
    }
    return count;
}

void PresetUpdater::uninstall_vendor(const VendorSync &const_vendor, std::function<void(bool)> callback_result) {
    // get vendor from map to remove "const"
    std::lock_guard<std::recursive_mutex> guard(this->all_vendors_mutex);
    auto it_mutable_vendor = all_vendors.find(const_vendor.profile.id);
    if (it_mutable_vendor == all_vendors.end() || &it_mutable_vendor->second != &const_vendor) {
        assert(false);
        return;
    }
    const GUI::Config::Snapshot* snapshot = take_config_snapshot_report_error(*GUI::wxGetApp().app_config, GUI::Config::Snapshot::SNAPSHOT_DOWNGRADE,
        format(_u8L("Before removing vendor bundle '%1%'"), const_vendor.profile.full_name));
    if (!snapshot) {
        // fail to snapshot, cancel
        BOOST_LOG_TRIVIAL(error) << "Error: can't take snapshot, cancle preset removal.";
        callback_result(false);
        return;
    }
    bool result = it_mutable_vendor->second.uninstall_vendor_config();
    if (result) {
        // unregister from appconfig
        auto vmap = GUI::wxGetApp().app_config->vendors();
        vmap.erase(it_mutable_vendor->second.profile.id);
        GUI::wxGetApp().app_config->set_vendors(vmap);
        GUI::wxGetApp().app_config->save();
        // unregister from current slicer app
        GUI::wxGetApp().preset_bundle->load_installed_printers(*GUI::wxGetApp().app_config);
        GUI::wxGetApp().preset_bundle->load_presets(*GUI::wxGetApp().app_config, ForwardCompatibilitySubstitutionRule::EnableSystemSilent);
        // update tabs
        GUI::wxGetApp().load_current_presets();
        reload_all_vendors();
        sync_async([callback_result](int nbupdates) { callback_result(true); });
    } else {
        callback_result(false);
    }
}

void PresetUpdater::install_vendor(const VendorSync &const_vendor,
                                   const VendorAvailable &version,
                                   std::function<void(bool)> callback_result) {
    // get vendor from map to remove "const"
    std::lock_guard<std::recursive_mutex> guard(this->all_vendors_mutex);
    auto it_mutable_vendor = all_vendors.find(const_vendor.profile.id);
    if (it_mutable_vendor == all_vendors.end() || &it_mutable_vendor->second != &const_vendor) {
        assert(false);
        callback_result(false); 
        return;
    }
    const GUI::Config::Snapshot* snapshot = take_config_snapshot_report_error(*GUI::wxGetApp().app_config, GUI::Config::Snapshot::SNAPSHOT_UPGRADE,
        format(_u8L("Before installing vendor bundle '%1%' version %2%"), const_vendor.profile.full_name, version.config_version.to_string()));
    if (!snapshot) {
        // fail to snapshot, cancel
        BOOST_LOG_TRIVIAL(error) << "Error: can't take snapshot, cancle preset installation.";
        callback_result(false); 
        return;
    }
    VendorSync &vendor = it_mutable_vendor->second;
    bool result = vendor.install_vendor_config(version, *this);
    auto vmap = GUI::wxGetApp().app_config->vendors();
    if (result && vmap.find(vendor.profile.id) != vmap.end()) {
        // the vendor is already in use, update the slicer
        // read it with models
        if (vendor.profile.models.empty()) {
            assert(vendor.is_installed);
            vendor.profile = VendorProfile::from_ini(GUI::into_path(data_dir()) / "vendor" / (vendor.profile.id + ".ini"), true);
            assert(!vendor.profile.models.empty());
        }
        // update appconfig : remove unfindable printers
        for (auto printer_it = vmap[vendor.profile.id].begin(); printer_it != vmap[vendor.profile.id].end();) {
            // search for PrinterModel
            const VendorProfile::PrinterModel *printer_model = nullptr;
            for (const VendorProfile::PrinterModel &pm : vendor.profile.models) {
                if (pm.id == printer_it->first) {
                    printer_model = &pm;
                    break;
                }
            }
            if (printer_model == nullptr) {
                printer_it = vmap[vendor.profile.id].erase(printer_it);
            } else {
                // same for variant
                for (auto it_variant = printer_it->second.begin(); it_variant != printer_it->second.end();) {
                    bool found_name = false;
                    for (const VendorProfile::PrinterVariant &variant : printer_model->variants) {
                        if (variant.name == *it_variant) {
                            found_name = true;
                            break;
                        }
                    }
                    if (!found_name) {
                        it_variant = printer_it->second.erase(it_variant);
                    } else {
                        it_variant++;
                    }
                }
                // erase if no variant anymore
                if (printer_it->second.empty()) {
                    printer_it = vmap[vendor.profile.id].erase(printer_it);
                } else {
                    // next elt
                    ++printer_it;
                }
            }
        }
        GUI::wxGetApp().app_config->set_vendors(vmap);
        GUI::wxGetApp().app_config->save();
        // reload slicer app
        GUI::wxGetApp().preset_bundle->load_installed_printers(*GUI::wxGetApp().app_config);
        GUI::wxGetApp().preset_bundle->load_presets(*GUI::wxGetApp().app_config,
                                                    ForwardCompatibilitySubstitutionRule::EnableSystemSilent);
        // update tabs
        GUI::wxGetApp().load_current_presets();
    }
    callback_result(result); 
}

bool copy_file_and_icons(boost::filesystem::path dir_in, boost::filesystem::path dir_out, std::string vendor_id, bool copy = true) {

    // copy the file & icons
    assert(boost::filesystem::exists(dir_in));
    if (boost::filesystem::exists(dir_in)) {
        boost::filesystem::path vendor_profile_file_in = dir_in / (vendor_id + ".ini");
        boost::filesystem::path vendor_profile_file_out = dir_out / (vendor_id + ".ini");
        assert(boost::filesystem::exists(vendor_profile_file_in));
        if (!boost::filesystem::exists(vendor_profile_file_in)) {
            return false;
        }
        if (copy) {
            boost::filesystem::copy(vendor_profile_file_in, vendor_profile_file_out);
        } else {
            boost::filesystem::rename(vendor_profile_file_in, vendor_profile_file_out);
        }
        // now copy icons
        boost::filesystem::path icon_dir_in = dir_in / vendor_id;
        boost::filesystem::path icon_dir_out = dir_out / vendor_id;
        if (boost::filesystem::exists(icon_dir_in)) {
            // remove all
            if (boost::filesystem::exists(icon_dir_out)) {
                for (const boost::filesystem::directory_entry &path_entry :
                        boost::filesystem::directory_iterator(icon_dir_out)) {
                    boost::filesystem::remove_all(path_entry.path());
                }
            }
            boost::filesystem::create_directories(icon_dir_out);
            // copy all
            for (const boost::filesystem::directory_entry &path_entry :
                    boost::filesystem::directory_iterator(icon_dir_in)) {
                assert(path_entry.status().type() == boost::filesystem::file_type::regular_file);
                if (copy) {
                    boost::filesystem::copy(path_entry.path(),
                                            icon_dir_out / path_entry.path().lexically_relative(icon_dir_in));
                } else {
                    boost::filesystem::rename(path_entry.path(),
                                              icon_dir_out / path_entry.path().lexically_relative(icon_dir_in));
                }
            }
            if (!copy) {
                boost::filesystem::remove_all(icon_dir_in);
            }
        }
    }
    return true;

}

bool VendorSync::install_vendor_config(const VendorAvailable &to_install, PresetUpdater& api_slot) {
    // rename current venddor file (if any)
    boost::filesystem::path vendor_file = GUI::into_path(data_dir()) / "vendor" / (this->profile.id + ".ini");
    boost::filesystem::path old_vendor_file = GUI::into_path(data_dir()) / "vendor" / (this->profile.id + ".ini.old");
    assert(!this->is_installed || boost::filesystem::exists(vendor_file));
    if (boost::filesystem::exists(vendor_file)) {
        assert(this->is_installed);
        boost::filesystem::rename(vendor_file, old_vendor_file);
    }
    // get new file
    if (to_install.is_file) {
        // file in resource directory
        boost::filesystem::path new_vendor_file(to_install.url_zip);
        assert(new_vendor_file.stem() == profile.id);
        bool copy_okay = copy_file_and_icons(new_vendor_file.parent_path(), GUI::into_path(data_dir()) / "vendor", new_vendor_file.stem().string(), true);
        if (copy_okay) {
            if (boost::filesystem::exists(old_vendor_file)) {
                boost::filesystem::remove(old_vendor_file);
            }
        } else {
            return false;
        }
    } else {
        //test if already dowloaded
        std::string directory_name = profile.config_update_github+"-"+to_install.commit_sha.substr(7);
        std::replace(directory_name.begin(), directory_name.end(), '_', '-');
        std::replace(directory_name.begin(), directory_name.end(), '/', '-');
        // download zip
        boost::filesystem::path temp_dir = GUI::into_path(data_dir()) / "cache" / "vendor" / "temp";
        boost::filesystem::path root_dir = temp_dir / directory_name;
        if (!boost::filesystem::exists(root_dir)) {
            boost::filesystem::create_directories(temp_dir);
            boost::filesystem::path download_zip_file(data_dir());
            download_zip_file = download_zip_file / "cache" / "vendor" / "temp" / (this->profile.id + ".zip");
            if (boost::filesystem::exists(download_zip_file)) {
                boost::filesystem::remove(download_zip_file);
            }
            std::string error_message;
            std::atomic_bool cancel = false;
            bool done = false;
            bool res = false;
            if (!api_slot.has_api_request_slot()) {
                return false;
            }
            Http::get(to_install.url_zip)
                // max 100 mega
                .size_limit(130 * 1024 * 1024)
                .on_error([&](std::string body, std::string error, unsigned http_status) {
                    error_message = GUI::format("Error getting: `%1%`: HTTP %2%, %3%", to_install.url_zip, http_status,
                                                error);
                    BOOST_LOG_TRIVIAL(error) << error_message;
                })
                .on_complete([&](std::string body, unsigned /* http_status */) {
                    FILE *file = boost::nowide::fopen(download_zip_file.string().c_str(), "wb");
                    assert(file != nullptr);
                    if (file == nullptr) {
                        error_message = GUI::format(_u8L("Can't create file at %1%"), download_zip_file.string());
                        res = false;
                        done = true;
                        return;
                    }
                    try {
                        fwrite(body.c_str(), 1, body.size(), file);
                    } catch (const std::exception &e) {
                        error_message = GUI::format(_u8L("Failed to write to file %1% :\n%3%"), download_zip_file,
                                                    e.what());
                        if (file != nullptr) {
                            fclose(file);
                        }
                        res = false;
                        done = true;
                        return;
                    }
                    fclose(file);
                    res = true;
                    done = true;
                })
                // do it here, so we wait the result
                .perform_sync();
            assert(done);
            bool got_root_dir = false;
            if (!res) {
                return false;
            } else {
                // unzip
                // zip reader takes care of opening & closing
                ZipReader zip(download_zip_file.string());
                if (!zip.success()) {
                    BOOST_LOG_TRIVIAL(error) << "Unable to open the preset zip. Maybe the download is corrupted.";
                    return false;
                }
                try {
                    mz_uint num_entries = mz_zip_reader_get_num_files(&zip.archive);
                    mz_zip_archive_file_stat file_stat;
                    // we first loop the entries to read from the archive the .model file only, in order to extract
                    // the version from it
                    bool found_model = false;
                    for (mz_uint i = 0; i < num_entries; ++i) {
                        if (mz_zip_reader_file_stat(&zip.archive, i, &file_stat)) {
                            boost::filesystem::path in_path = file_stat.m_filename;
                            assert(in_path.is_relative());
                            boost::filesystem::path out_path = temp_dir / in_path.lexically_normal();
                            if (!got_root_dir) {
                                assert(file_stat.m_is_directory);
                                if (boost::filesystem::exists(out_path)) {
                                    for (const boost::filesystem::directory_entry &path_entry :
                                         boost::filesystem::directory_iterator(out_path)) {
                                        boost::filesystem::remove_all(path_entry.path());
                                    }
                                }
                                root_dir = out_path.lexically_normal();
                                got_root_dir = true;
                                boost::filesystem::create_directories(out_path);
                            } else {
                                if (file_stat.m_is_directory) {
                                    boost::filesystem::create_directories(out_path);
                                } else {
                                    size_t uncompressed_size = file_stat.m_uncomp_size;
                                    void *p = mz_zip_reader_extract_file_to_heap(&zip.archive, file_stat.m_filename,
                                                                                 &uncompressed_size, 0);
                                    if (!p) {
                                        return false;
                                    }
                                    FILE *file_to_write = fopen(out_path.string().c_str(), "wb");
                                    if (file_to_write == nullptr) {
                                        BOOST_LOG_TRIVIAL(error) << "Fail to unzip downloaded config zip.";
                                        return false;
                                    }
                                    fwrite((const char *) p, 1, uncompressed_size, file_to_write);
                                    fclose(file_to_write);
                                    mz_free(p);
                                }
                            }
                        }
                    }
                } catch (const std::exception &) {
                    BOOST_LOG_TRIVIAL(error) << "Fail to upgrade current app by the content of the downloaded zip.";
                    return false;
                }
                // copy the file & icons
                assert(boost::filesystem::exists(root_dir));
                bool copy_okay = copy_file_and_icons(root_dir / "profiles", GUI::into_path(data_dir()) / "vendor",
                                                     this->profile.id, true);
                if (copy_okay) {
                    if (boost::filesystem::exists(old_vendor_file)) {
                        boost::filesystem::remove(old_vendor_file);
                    }
                } else {
                    return false;
                }
            }
        } else {
            //already downloaded
            // copy the file & icons
            assert(boost::filesystem::exists(root_dir));
            bool copy_okay = copy_file_and_icons(root_dir / "profiles", GUI::into_path(data_dir()) / "vendor",
                                                    this->profile.id, true);
            if (copy_okay) {
                if (boost::filesystem::exists(old_vendor_file)) {
                    boost::filesystem::remove(old_vendor_file);
                }
            } else {
                return false;
            }
        }
    }
    // refresh this
    VendorProfile vp = VendorProfile::from_ini(GUI::into_path(data_dir()) / "vendor" / (this->profile.id + ".ini"), true);
    this->is_installed = true;
    this->profile = vp;
    this->can_upgrade = !available_profiles.empty() && available_profiles.front().config_version > profile.config_version;
    return true;
}

bool VendorSync::uninstall_vendor_config() {
    // move from vendor to cache
    bool move_okay = copy_file_and_icons(GUI::into_path(data_dir()) / "vendor", GUI::into_path(data_dir()) / "cache" / "vendor", this->profile.id, false);
    if (!move_okay) {
        return false;
    }
    // unregister from us
    this->is_installed = false;
    this->is_synch = false;
    this->can_upgrade = false;
    this->available_profiles = {};
    return true;
}

void PresetUpdater::show_synch_window(wxWindow *parent,
                                      const PresetBundle *preset_bundle,
                                      const wxString &message,
                                      std::function<void(bool)> callback_dialog_closed) {
    GUI::UpdateConfigDialog * dialog = new GUI::UpdateConfigDialog(parent, *this, message);
    int res = dialog->ShowModal();

    if (callback_dialog_closed) {
        callback_dialog_closed(res == wxID_OK);
    }
}

size_t PresetUpdater::count_installed() {
    size_t count = 0;
    std::lock_guard<std::recursive_mutex> guard(this->all_vendors_mutex);
    for (const auto& [id, vendor] : this->all_vendors) {
        if (vendor.is_installed) {
            ++count;
        }
    }
    return count;
}

void PresetUpdater::download_new_repo(const std::string &github_org_repo, std::function<void(bool)> callback_result) {
    Http::get(std::string("https://raw.githubusercontent.com/")+github_org_repo+"/refs/heads/main/description.ini")
        .size_limit(1024 * 64 /*64kio, 100tags should use 27ko*/)
        .on_error([&](std::string body, std::string error, unsigned http_status) {
            callback_result(false);
        })
        .on_complete([&](std::string body, unsigned /* http_status */) {
            try { 
                assert(github_org_repo.find('/') != std::string::npos);
                if (github_org_repo.find('/') == std::string::npos) {
                    assert(false);
                    callback_result(false);
                    return;
                }
                //body += "\n";

                std::string id = github_org_repo.substr(github_org_repo.find('/') + 1);
                //parse it to get id (should be at the end of github_org_repo)
                boost::property_tree::ptree root;
                std::stringstream json_stream(body);
                boost::property_tree::read_ini(json_stream, root);
                VendorProfile vp = VendorProfile::from_ini(root, id, false);
                // save it
                boost::filesystem::create_directories(GUI::into_path(data_dir()) / "cache" /"vendor");
                boost::filesystem::path file_path = GUI::into_path(data_dir()) / "cache" /"vendor" / (vp.id + ".ini");
                FILE *fp = boost::nowide::fopen(file_path.string().c_str(), "w");
                if (fp == nullptr) {
                    BOOST_LOG_TRIVIAL(error) << "PresetUpdater::install_new_repo: Couldn't open " << file_path.c_str() << " for writing";
                    callback_result(false);
                    return;
                }
                fprintf(fp, body.c_str());
                fclose(fp);

                callback_result(true);
            } catch (const std::exception&) {
                callback_result(false);
                return;
            }

        })
        .perform();
}

void PresetUpdater::uninstall_all_vendors(std::function<void(bool)> callback_result) {
    const GUI::Config::Snapshot* snapshot = take_config_snapshot_report_error(*GUI::wxGetApp().app_config, GUI::Config::Snapshot::SNAPSHOT_UPGRADE,
        _u8L("Before removing all vendor bundles"));
    if (!snapshot) {
        // fail to snapshot, cancel
        BOOST_LOG_TRIVIAL(error) << "Error: can't take snapshot, cancle preset removal.";
        callback_result(false);
        return;
    }
    std::lock_guard<std::recursive_mutex> guard(this->all_vendors_mutex);
    bool at_leat_a_delete = false;
    auto vmap = GUI::wxGetApp().app_config->vendors();
    for (auto& [id, vendor] : this->all_vendors) {
        if (vendor.is_installed) {
            bool res = vendor.uninstall_vendor_config();
            if (res) {
                at_leat_a_delete = true;
                vmap.erase(vendor.profile.id);
            }
        }
    }
    if (at_leat_a_delete) {
        // unregister from appconfig
        GUI::wxGetApp().app_config->set_vendors(vmap);
        GUI::wxGetApp().app_config->save();
        // unregister from current slicer app
        GUI::wxGetApp().preset_bundle->load_installed_printers(*GUI::wxGetApp().app_config);
        GUI::wxGetApp().preset_bundle->load_presets(*GUI::wxGetApp().app_config,
                                                    ForwardCompatibilitySubstitutionRule::EnableSystemSilent);
        // update tabs
        GUI::wxGetApp().load_current_presets();
        reload_all_vendors();
        sync_async([callback_result](int nbupdates) { callback_result(true); });
    } else {
        callback_result(true);
    }
}

bool PresetUpdater::has_api_request_slot() {
    if (next_time_slot + 3600 < std::time(nullptr)) {
        next_time_slot = std::time(nullptr);
        max_request = 25;
    }
    return (--max_request) > 0;
}

void PresetUpdater::install_all_vendors(std::function<void(bool)> callback_result) {
    const GUI::Config::Snapshot* snapshot = take_config_snapshot_report_error(*GUI::wxGetApp().app_config, GUI::Config::Snapshot::SNAPSHOT_DOWNGRADE,
        _u8L("Before installing all vendor bundles"));
    if (!snapshot) {
        // fail to snapshot, cancel
        BOOST_LOG_TRIVIAL(error) << "Error: can't take snapshot, cancle preset removal.";
        callback_result(false);
        return;
    }
    std::lock_guard<std::recursive_mutex> guard(this->all_vendors_mutex);
    bool at_leat_an_install = false;
    auto vmap = GUI::wxGetApp().app_config->vendors();
    for (auto& [id, vendor] : this->all_vendors) {
        if (!vendor.is_installed && !vendor.available_profiles.empty()) {
            assert(vendor.best);
            bool res = vendor.install_vendor_config(*vendor.best, *this);
            at_leat_an_install = at_leat_an_install || res;
        }
    }
    if (at_leat_an_install) {
        // the vendor is already in use, update the slicer
        // reload slicer app
        GUI::wxGetApp().preset_bundle->load_installed_printers(*GUI::wxGetApp().app_config);
        GUI::wxGetApp().preset_bundle->load_presets(*GUI::wxGetApp().app_config,
                                                    ForwardCompatibilitySubstitutionRule::EnableSystemSilent);
        // update tabs
        GUI::wxGetApp().load_current_presets();
        reload_all_vendors();
        sync_async([callback_result](int nbupdates) { callback_result(true); });
    } else {
        callback_result(true);
    }
}

void PresetUpdater::upgrade_all_installed_vendors(std::function<void(bool)> callback_result) {
    const GUI::Config::Snapshot* snapshot = take_config_snapshot_report_error(*GUI::wxGetApp().app_config, GUI::Config::Snapshot::SNAPSHOT_UPGRADE,
        _u8L("Before upgrading all intalled vendor bundles"));
    if (!snapshot) {
        // fail to snapshot, cancel
        BOOST_LOG_TRIVIAL(error) << "Error: can't take snapshot, cancle preset removal.";
        callback_result(false);
        return;
    }
    std::lock_guard<std::recursive_mutex> guard(this->all_vendors_mutex);
    bool at_leat_an_upgrade = false;
    auto vmap = GUI::wxGetApp().app_config->vendors();
    for (auto& [id, vendor] : this->all_vendors) {
        if (vendor.is_installed && vendor.can_upgrade) {
            assert(!vendor.available_profiles.empty());
            assert(vendor.best);
            bool res = vendor.install_vendor_config(*vendor.best, *this);
            if (res) {
                at_leat_an_upgrade = true;
                // read it with models
                if (vendor.profile.models.empty()) {
                    assert(vendor.is_installed);
                    vendor.profile = VendorProfile::from_ini(GUI::into_path(data_dir()) / "vendor" / (vendor.profile.id + ".ini"), true);
                    assert(!vendor.profile.models.empty());
                }
                // update appconfig : remove unfindable printers
                for (auto printer_it = vmap[vendor.profile.id].begin(); printer_it != vmap[vendor.profile.id].end();) {
                    // search for PrinterModel
                    const VendorProfile::PrinterModel *printer_model = nullptr;
                    for (const VendorProfile::PrinterModel &pm : vendor.profile.models) {
                        if (pm.id == printer_it->first) {
                            printer_model = &pm;
                            break;
                        }
                    }
                    if (printer_model == nullptr) {
                        printer_it = vmap[vendor.profile.id].erase(printer_it);
                    } else {
                        // same for variant
                        for (auto it_variant = printer_it->second.begin(); it_variant != printer_it->second.end();) {
                            bool found_name = false;
                            for (const VendorProfile::PrinterVariant &variant : printer_model->variants) {
                                if (variant.name == *it_variant) {
                                    found_name = true;
                                    break;
                                }
                            }
                            if (!found_name) {
                                it_variant = printer_it->second.erase(it_variant);
                            } else {
                                it_variant++;
                            }
                        }
                        // erase if no variant anymore
                        if (printer_it->second.empty()) {
                            printer_it = vmap[vendor.profile.id].erase(printer_it);
                        } else {
                            // next elt
                            ++printer_it;
                        }
                    }
                }
            }
        }
    }
    if (at_leat_an_upgrade) {
        // the vendor is already in use, update the slicer
        GUI::wxGetApp().app_config->set_vendors(vmap);
        GUI::wxGetApp().app_config->save();
        // reload slicer app
        GUI::wxGetApp().preset_bundle->load_installed_printers(*GUI::wxGetApp().app_config);
        GUI::wxGetApp().preset_bundle->load_presets(*GUI::wxGetApp().app_config,
                                                    ForwardCompatibilitySubstitutionRule::EnableSystemSilent);
        // update tabs
        GUI::wxGetApp().load_current_presets();
        reload_all_vendors();
        sync_async([callback_result](int nbupdates) { callback_result(true); });
    } else {
        callback_result(true);
    }

}

} // namespace Slic3r
