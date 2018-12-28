// Client module for the OBS Studio node module.
// Copyright(C) 2017 Streamlabs (General Workings Inc)
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110 - 1301, USA.

#include "osn-source.hpp"
#include <ipc-class.hpp>
#include <ipc-function.hpp>
#include <ipc-server.hpp>
#include <ipc-value.hpp>
#include <map>
#include <memory>
#include <obs-data.h>
#include <obs.h>
#include <obs.hpp>
#include "error.hpp"
#include "obs-property.hpp"
#include "osn-common.hpp"
#include "shared.hpp"

std::map<uint64_t, std::vector<std::tuple<std::string, std::string, obs_hotkey_id>>> pending_source_hotkeys;
std::mutex                                                           source_hotkey_mtx;

std::vector<std::tuple<std::string, std::string, obs_hotkey_id>> get_source_hotkeys(uint64_t sourceID)
{
	std::vector<std::tuple<std::string, std::string, obs_hotkey_id>> hotkeys;

	obs_source_t* src = osn::Source::Manager::GetInstance().find(sourceID);
	if (src == nullptr) {
		return {};
	}

	// For each registered hotkey
	auto indata  = std::make_pair(src, &hotkeys);
	using data_t = decltype(indata);
	obs_enum_hotkeys(
	    [](void* data, obs_hotkey_id id, obs_hotkey_t* key) {
		    data_t&       d               = *static_cast<data_t*>(data);
		    obs_source_t* source          = d.first;
		    auto          registerer_type = obs_hotkey_get_registerer_type(key);
		    void*         registerer      = obs_hotkey_get_registerer(key);

		    if (registerer_type == OBS_HOTKEY_REGISTERER_SOURCE) {
			    auto* weak_source = static_cast<obs_weak_source_t*>(registerer);
			    auto  key_source  = OBSGetStrongRef(weak_source);

			    if (source == key_source) {

					auto ToTitle = [](std::string s) {
						bool last = true;
						for (char& c : s) {
							c    = last ? ::toupper(c) : ::tolower(c);
							last = ::isspace(c);
						}
						return s;
				    };

				    auto key_name = std::string(obs_hotkey_get_name(key));
				    auto desc     = std::string(obs_hotkey_get_description(key));
				    const auto  hotkeyId = obs_hotkey_get_id(key);

					// Parse the key name and the description
					key_name = key_name.substr(key_name.find_first_of(".") + 1);
				    std::replace(key_name.begin(), key_name.end(), '-', '_');
				    std::transform(key_name.begin(), key_name.end(), key_name.begin(), ::toupper);
					// 
				    std::replace(desc.begin(), desc.end(), '-', ' ');
				    desc = ToTitle(desc);

				    d.second->push_back({key_name, desc, hotkeyId});
			    }
		    }

		    return true;
	    },
	    &indata);

	return hotkeys;
}

void osn::Source::initialize_global_signals()
{
	signal_handler_t* sh = obs_get_signal_handler();
	signal_handler_connect(sh, "source_create", osn::Source::global_source_create_cb, nullptr);
}

void osn::Source::finalize_global_signals()
{
	signal_handler_t* sh = obs_get_signal_handler();
	signal_handler_disconnect(sh, "source_create", osn::Source::global_source_create_cb, nullptr);
}

void osn::Source::attach_source_signals(obs_source_t* src)
{
	signal_handler_t* sh = obs_source_get_signal_handler(src);
	if (!sh)
		return;
	signal_handler_connect(sh, "destroy", osn::Source::global_source_destroy_cb, nullptr);
}

void osn::Source::detach_source_signals(obs_source_t* src)
{
	signal_handler_t* sh = obs_source_get_signal_handler(src);
	if (!sh)
		return;
	signal_handler_disconnect(sh, "destroy", osn::Source::global_source_destroy_cb, nullptr);
}

void osn::Source::global_source_create_cb(void* ptr, calldata_t* cd)
{
	obs_source_t* source = nullptr;
	if (!calldata_get_ptr(cd, "source", &source)) {
		throw std::exception((const std::exception&)"calldata did not contain source pointer");
	}

	osn::Source::Manager::GetInstance().allocate(source);
	osn::Source::attach_source_signals(source);

	auto sourceId      = osn::Source::Manager::GetInstance().find(source);
	auto sourceHotkeys = get_source_hotkeys(sourceId);

	std::unique_lock<std::mutex> ulock(source_hotkey_mtx);
	pending_source_hotkeys.insert({sourceId, sourceHotkeys});
}

void osn::Source::global_source_destroy_cb(void* ptr, calldata_t* cd)
{
	obs_source_t* source = nullptr;
	if (!calldata_get_ptr(cd, "source", &source)) {
		throw std::exception((const std::exception&)"calldata did not contain source pointer");
	}

	{
		std::unique_lock<std::mutex> ulock(source_hotkey_mtx);

		auto iter = pending_source_hotkeys.find(osn::Source::Manager::GetInstance().find(source));
		if (iter != pending_source_hotkeys.end()) {
			pending_source_hotkeys.erase(iter);
		}
	}

	detach_source_signals(source);
	osn::Source::Manager::GetInstance().free(source);
}

void osn::Source::Register(ipc::server& srv)
{
	std::shared_ptr<ipc::collection> cls = std::make_shared<ipc::collection>("Source");
	cls->register_function(
	    std::make_shared<ipc::function>("GetDefaults", std::vector<ipc::type>{ipc::type::String}, GetTypeDefaults));
	cls->register_function(
	    std::make_shared<ipc::function>("GetProperties", std::vector<ipc::type>{ipc::type::String}, GetTypeProperties));
	cls->register_function(std::make_shared<ipc::function>(
	    "GetOutputFlags", std::vector<ipc::type>{ipc::type::String}, GetTypeOutputFlags));

	cls->register_function(
	    std::make_shared<ipc::function>("Remove", std::vector<ipc::type>{ipc::type::UInt64}, Remove));
	cls->register_function(
	    std::make_shared<ipc::function>("Release", std::vector<ipc::type>{ipc::type::UInt64}, Release));
	cls->register_function(
	    std::make_shared<ipc::function>("IsConfigurable", std::vector<ipc::type>{ipc::type::UInt64}, IsConfigurable));
	cls->register_function(
	    std::make_shared<ipc::function>("GetProperties", std::vector<ipc::type>{ipc::type::UInt64}, GetProperties));
	cls->register_function(
	    std::make_shared<ipc::function>("GetSettings", std::vector<ipc::type>{ipc::type::UInt64}, GetSettings));
	cls->register_function(std::make_shared<ipc::function>("Load", std::vector<ipc::type>{ipc::type::UInt64}, Load));
	cls->register_function(std::make_shared<ipc::function>("Save", std::vector<ipc::type>{ipc::type::UInt64}, Save));
	cls->register_function(std::make_shared<ipc::function>(
	    "Update", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::String}, Update));
	cls->register_function(
	    std::make_shared<ipc::function>("GetType", std::vector<ipc::type>{ipc::type::UInt64}, GetType));
	cls->register_function(
	    std::make_shared<ipc::function>("GetName", std::vector<ipc::type>{ipc::type::UInt64}, GetName));
	cls->register_function(
	    std::make_shared<ipc::function>("SetName", std::vector<ipc::type>{ipc::type::UInt64}, SetName));
	cls->register_function(
	    std::make_shared<ipc::function>("GetOutputFlags", std::vector<ipc::type>{ipc::type::UInt64}, GetOutputFlags));
	cls->register_function(
	    std::make_shared<ipc::function>("GetFlags", std::vector<ipc::type>{ipc::type::UInt64}, GetFlags));
	cls->register_function(std::make_shared<ipc::function>(
	    "SetFlags", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::UInt32}, SetFlags));
	cls->register_function(
	    std::make_shared<ipc::function>("GetStatus", std::vector<ipc::type>{ipc::type::UInt64}, GetStatus));
	cls->register_function(std::make_shared<ipc::function>("GetId", std::vector<ipc::type>{ipc::type::UInt64}, GetId));
	cls->register_function(std::make_shared<ipc::function>("Query", std::vector<ipc::type>{}, Query));
	cls->register_function(std::make_shared<ipc::function>(
	    "ProcessHotkeyStatus",
	    std::vector<ipc::type>{ipc::type::UInt64, ipc::type::Int32, ipc::type::String},
	    ProcessHotkeyStatus));
	cls->register_function(
	    std::make_shared<ipc::function>("GetMuted", std::vector<ipc::type>{ipc::type::UInt64}, GetMuted));
	cls->register_function(std::make_shared<ipc::function>(
	    "SetMuted", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::Int32}, SetMuted));
	cls->register_function(
	    std::make_shared<ipc::function>("GetEnabled", std::vector<ipc::type>{ipc::type::UInt64}, GetEnabled));
	cls->register_function(std::make_shared<ipc::function>(
	    "SetEnabled", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::Int32}, SetEnabled));
	srv.register_collection(cls);
}

void osn::Source::GetTypeProperties(
    void*                          data,
    const int64_t                  id,
    const std::vector<ipc::value>& args,
    std::vector<ipc::value>&       rval)
{
	AUTO_DEBUG;
	// Per Type Properties (doesn't have an object).
	//obs_get_source_properties();
}

void osn::Source::GetTypeDefaults(
    void*                          data,
    const int64_t                  id,
    const std::vector<ipc::value>& args,
    std::vector<ipc::value>&       rval)
{
	AUTO_DEBUG;
	// Per Type Defaults (doesn't have an object)
	//obs_get_source_defaults();
}

void osn::Source::GetTypeOutputFlags(
    void*                          data,
    const int64_t                  id,
    const std::vector<ipc::value>& args,
    std::vector<ipc::value>&       rval)
{
	AUTO_DEBUG;
	// Per Type Defaults (doesn't have an object)
	//obs_get_source_output_flags();
}

void osn::Source::Remove(
    void*                          data,
    const int64_t                  id,
    const std::vector<ipc::value>& args,
    std::vector<ipc::value>&       rval)
{
	// Attempt to find the source asked to load.
	obs_source_t* src = osn::Source::Manager::GetInstance().find(args[0].value_union.ui64);
	if (src == nullptr) {
		rval.push_back(ipc::value((uint64_t)ErrorCode::InvalidReference));
		rval.push_back(ipc::value("Source reference is not valid."));
		AUTO_DEBUG;
		return;
	}

	obs_source_remove(src);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void osn::Source::Release(
    void*                          data,
    const int64_t                  id,
    const std::vector<ipc::value>& args,
    std::vector<ipc::value>&       rval)
{
	// Attempt to find the source asked to load.
	obs_source_t* src = osn::Source::Manager::GetInstance().find(args[0].value_union.ui64);
	if (src == nullptr) {
		rval.push_back(ipc::value((uint64_t)ErrorCode::InvalidReference));
		rval.push_back(ipc::value("Source reference is not valid."));
		AUTO_DEBUG;
		return;
	}

	obs_source_release(src);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void osn::Source::IsConfigurable(
    void*                          data,
    const int64_t                  id,
    const std::vector<ipc::value>& args,
    std::vector<ipc::value>&       rval)
{
	// Attempt to find the source asked to load.
	obs_source_t* src = osn::Source::Manager::GetInstance().find(args[0].value_union.ui64);
	if (src == nullptr) {
		rval.push_back(ipc::value((uint64_t)ErrorCode::InvalidReference));
		rval.push_back(ipc::value("Source reference is not valid."));
		AUTO_DEBUG;
		return;
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(obs_source_configurable(src)));
	AUTO_DEBUG;
}

void osn::Source::GetProperties(
    void*                          data,
    const int64_t                  id,
    const std::vector<ipc::value>& args,
    std::vector<ipc::value>&       rval)
{
	// Attempt to find the source asked to load.
	obs_source_t* src = osn::Source::Manager::GetInstance().find(args[0].value_union.ui64);
	if (src == nullptr) {
		rval.push_back(ipc::value((uint64_t)ErrorCode::InvalidReference));
		rval.push_back(ipc::value("Source reference is not valid."));
		AUTO_DEBUG;
		return;
	}

	obs_properties_t* prp = obs_source_properties(src);
	const char*       buf;

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	for (obs_property_t* p = obs_properties_first(prp); (p != nullptr); obs_property_next(&p)) {
		std::shared_ptr<obs::Property> prop;

		switch (obs_property_get_type(p)) {
		case OBS_PROPERTY_BOOL:
			prop = std::make_shared<obs::BooleanProperty>();
			break;
		case OBS_PROPERTY_INT: {
			auto prop2        = std::make_shared<obs::IntegerProperty>();
			prop2->field_type = obs::NumberProperty::NumberType(obs_property_int_type(p));
			prop2->minimum    = obs_property_int_min(p);
			prop2->maximum    = obs_property_int_max(p);
			prop2->step       = obs_property_int_step(p);
			prop              = prop2;
			break;
		}
		case OBS_PROPERTY_FLOAT: {
			auto prop2        = std::make_shared<obs::FloatProperty>();
			prop2->field_type = obs::NumberProperty::NumberType(obs_property_float_type(p));
			prop2->minimum    = obs_property_float_min(p);
			prop2->maximum    = obs_property_float_max(p);
			prop2->step       = obs_property_float_step(p);
			prop              = prop2;
			break;
		}
		case OBS_PROPERTY_TEXT: {
			auto prop2        = std::make_shared<obs::TextProperty>();
			prop2->field_type = obs::TextProperty::TextType(obs_proprety_text_type(p));
			prop              = prop2;
			break;
		}
		case OBS_PROPERTY_PATH: {
			auto prop2          = std::make_shared<obs::PathProperty>();
			prop2->field_type   = obs::PathProperty::PathType(obs_property_path_type(p));
			prop2->filter       = (buf = obs_property_path_filter(p)) != nullptr ? buf : "";
			prop2->default_path = (buf = obs_property_path_default_path(p)) != nullptr ? buf : "";
			prop                = prop2;
			break;
		}
		case OBS_PROPERTY_LIST: {
			auto prop2        = std::make_shared<obs::ListProperty>();
			prop2->field_type = obs::ListProperty::ListType(obs_property_list_type(p));
			prop2->format     = obs::ListProperty::Format(obs_property_list_format(p));
			size_t items      = obs_property_list_item_count(p);
			for (size_t idx = 0; idx < items; ++idx) {
				obs::ListProperty::Item entry;
				entry.name    = (buf = obs_property_list_item_name(p, idx)) != nullptr ? buf : "";
				entry.enabled = !obs_property_list_item_disabled(p, idx);
				switch (prop2->format) {
				case obs::ListProperty::Format::Integer:
					entry.value_int = obs_property_list_item_int(p, idx);
					break;
				case obs::ListProperty::Format::Float:
					entry.value_float = obs_property_list_item_float(p, idx);
					break;
				case obs::ListProperty::Format::String:
					entry.value_string = (buf = obs_property_list_item_string(p, idx)) != nullptr ? buf : "";
					break;
				}
				prop2->items.push_back(std::move(entry));
			}
			prop = prop2;
			break;
		}
		case OBS_PROPERTY_COLOR:
			prop = std::make_shared<obs::ColorProperty>();
			break;
		case OBS_PROPERTY_BUTTON:
			prop = std::make_shared<obs::ButtonProperty>();
			break;
		case OBS_PROPERTY_FONT:
			prop = std::make_shared<obs::FontProperty>();
			break;
		case OBS_PROPERTY_EDITABLE_LIST: {
			auto prop2          = std::make_shared<obs::EditableListProperty>();
			prop2->field_type   = obs::EditableListProperty::ListType(obs_property_editable_list_type(p));
			prop2->filter       = (buf = obs_property_editable_list_filter(p)) != nullptr ? buf : "";
			prop2->default_path = (buf = obs_property_editable_list_default_path(p)) != nullptr ? buf : "";
			prop                = prop2;
			break;
		}
		case OBS_PROPERTY_FRAME_RATE: {
			auto   prop2      = std::make_shared<obs::FrameRateProperty>();
			size_t num_ranges = obs_property_frame_rate_fps_ranges_count(p);
			for (size_t idx = 0; idx < num_ranges; idx++) {
				auto min = obs_property_frame_rate_fps_range_min(p, idx),
				     max = obs_property_frame_rate_fps_range_max(p, idx);

				obs::FrameRateProperty::Range range;
				range.minimum.first  = min.numerator;
				range.minimum.second = min.denominator;
				range.maximum.first  = max.numerator;
				range.maximum.second = max.denominator;

				prop2->ranges.push_back(std::move(range));
			}

			size_t num_options = obs_property_frame_rate_options_count(p);
			for (size_t idx = 0; idx < num_options; idx++) {
				auto min = obs_property_frame_rate_fps_range_min(p, idx),
				     max = obs_property_frame_rate_fps_range_max(p, idx);

				obs::FrameRateProperty::Option option;
				option.name        = (buf = obs_property_frame_rate_option_name(p, idx)) != nullptr ? buf : "";
				option.description = (buf = obs_property_frame_rate_option_description(p, idx)) != nullptr ? buf : "";

				prop2->options.push_back(std::move(option));
			}

			prop = prop2;
			break;
		}
		}

		if (!prop) {
			continue;
		}

		prop->name             = obs_property_name(p);
		prop->description      = obs_property_description(p) ? obs_property_description(p) : "";
		prop->long_description = obs_property_long_description(p) ? obs_property_long_description(p) : "";
		prop->enabled          = obs_property_enabled(p);
		prop->visible          = obs_property_visible(p);

		std::vector<char> buf(prop->size());
		if (prop->serialize(buf)) {
			rval.push_back(ipc::value(buf));
		}
	}
	obs_properties_destroy(prp);
	AUTO_DEBUG;
}

void osn::Source::GetSettings(
    void*                          data,
    const int64_t                  id,
    const std::vector<ipc::value>& args,
    std::vector<ipc::value>&       rval)
{
	// Attempt to find the source asked to load.
	obs_source_t* src = osn::Source::Manager::GetInstance().find(args[0].value_union.ui64);
	if (src == nullptr) {
		rval.push_back(ipc::value((uint64_t)ErrorCode::InvalidReference));
		rval.push_back(ipc::value("Source reference is not valid."));
		AUTO_DEBUG;
		return;
	}

	obs_data_t* sets = obs_source_get_settings(src);
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(obs_data_get_json(sets)));
	obs_data_release(sets);
	AUTO_DEBUG;
}

void osn::Source::Update(
    void*                          data,
    const int64_t                  id,
    const std::vector<ipc::value>& args,
    std::vector<ipc::value>&       rval)
{
	// Attempt to find the source asked to load.
	obs_source_t* src = osn::Source::Manager::GetInstance().find(args[0].value_union.ui64);
	if (src == nullptr) {
		rval.push_back(ipc::value((uint64_t)ErrorCode::InvalidReference));
		rval.push_back(ipc::value("Source reference is not valid."));
		AUTO_DEBUG;
		return;
	}

	obs_data_t* sets = obs_data_create_from_json(args[1].value_str.c_str());
	obs_source_update(src, sets);
	obs_data_release(sets);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void osn::Source::Load(void* data, const int64_t id, const std::vector<ipc::value>& args, std::vector<ipc::value>& rval)
{
	// Attempt to find the source asked to load.
	obs_source_t* src = osn::Source::Manager::GetInstance().find(args[0].value_union.ui64);
	if (src == nullptr) {
		rval.push_back(ipc::value((uint64_t)ErrorCode::InvalidReference));
		rval.push_back(ipc::value("Source reference is not valid."));
		AUTO_DEBUG;
		return;
	}

	obs_source_load(src);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void osn::Source::Save(void* data, const int64_t id, const std::vector<ipc::value>& args, std::vector<ipc::value>& rval)
{
	// Attempt to find the source asked to load.
	obs_source_t* src = osn::Source::Manager::GetInstance().find(args[0].value_union.ui64);
	if (src == nullptr) {
		rval.push_back(ipc::value((uint64_t)ErrorCode::InvalidReference));
		rval.push_back(ipc::value("Source reference is not valid."));
		AUTO_DEBUG;
		return;
	}

	obs_source_save(src);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void osn::Source::GetType(
    void*                          data,
    const int64_t                  id,
    const std::vector<ipc::value>& args,
    std::vector<ipc::value>&       rval)
{
	// Attempt to find the source asked to load.
	obs_source_t* src = osn::Source::Manager::GetInstance().find(args[0].value_union.ui64);
	if (src == nullptr) {
		rval.push_back(ipc::value((uint64_t)ErrorCode::InvalidReference));
		rval.push_back(ipc::value("Source reference is not valid."));
		AUTO_DEBUG;
		return;
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(obs_source_get_type(src)));
	AUTO_DEBUG;
}

void osn::Source::GetName(
    void*                          data,
    const int64_t                  id,
    const std::vector<ipc::value>& args,
    std::vector<ipc::value>&       rval)
{
	// Attempt to find the source asked to load.
	obs_source_t* src = osn::Source::Manager::GetInstance().find(args[0].value_union.ui64);
	if (src == nullptr) {
		rval.push_back(ipc::value((uint64_t)ErrorCode::InvalidReference));
		rval.push_back(ipc::value("Source reference is not valid."));
		AUTO_DEBUG;
		return;
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(obs_source_get_name(src)));
	AUTO_DEBUG;
}

void osn::Source::SetName(
    void*                          data,
    const int64_t                  id,
    const std::vector<ipc::value>& args,
    std::vector<ipc::value>&       rval)
{
	// Attempt to find the source asked to load.
	obs_source_t* src = osn::Source::Manager::GetInstance().find(args[0].value_union.ui64);
	if (src == nullptr) {
		rval.push_back(ipc::value((uint64_t)ErrorCode::InvalidReference));
		rval.push_back(ipc::value("Source reference is not valid."));
		AUTO_DEBUG;
		return;
	}

	obs_source_set_name(src, args[1].value_str.c_str());

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(obs_source_get_name(src)));
	AUTO_DEBUG;
}

void osn::Source::GetOutputFlags(
    void*                          data,
    const int64_t                  id,
    const std::vector<ipc::value>& args,
    std::vector<ipc::value>&       rval)
{
	// Attempt to find the source asked to load.
	obs_source_t* src = osn::Source::Manager::GetInstance().find(args[0].value_union.ui64);
	if (src == nullptr) {
		rval.push_back(ipc::value((uint64_t)ErrorCode::InvalidReference));
		rval.push_back(ipc::value("Source reference is not valid."));
		AUTO_DEBUG;
		return;
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(obs_source_get_output_flags(src)));
	AUTO_DEBUG;
}

void osn::Source::GetFlags(
    void*                          data,
    const int64_t                  id,
    const std::vector<ipc::value>& args,
    std::vector<ipc::value>&       rval)
{
	// Attempt to find the source asked to load.
	obs_source_t* src = osn::Source::Manager::GetInstance().find(args[0].value_union.ui64);
	if (src == nullptr) {
		rval.push_back(ipc::value((uint64_t)ErrorCode::InvalidReference));
		rval.push_back(ipc::value("Source reference is not valid."));
		AUTO_DEBUG;
		return;
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(obs_source_get_flags(src)));
	AUTO_DEBUG;
}

void osn::Source::SetFlags(
    void*                          data,
    const int64_t                  id,
    const std::vector<ipc::value>& args,
    std::vector<ipc::value>&       rval)
{
	// Attempt to find the source asked to load.
	obs_source_t* src = osn::Source::Manager::GetInstance().find(args[0].value_union.ui64);
	if (src == nullptr) {
		rval.push_back(ipc::value((uint64_t)ErrorCode::InvalidReference));
		rval.push_back(ipc::value("Source reference is not valid."));
		AUTO_DEBUG;
		return;
	}

	obs_source_set_flags(src, args[1].value_union.ui32);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(obs_source_get_flags(src)));
	AUTO_DEBUG;
}

void osn::Source::GetStatus(
    void*                          data,
    const int64_t                  id,
    const std::vector<ipc::value>& args,
    std::vector<ipc::value>&       rval)
{
	// Attempt to find the source asked to load.
	obs_source_t* src = osn::Source::Manager::GetInstance().find(args[0].value_union.ui64);
	if (src == nullptr) {
		rval.push_back(ipc::value((uint64_t)ErrorCode::InvalidReference));
		rval.push_back(ipc::value("Source reference is not valid."));
		AUTO_DEBUG;
		return;
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value((uint32_t) true));
	AUTO_DEBUG;
}

void osn::Source::GetId(
    void*                          data,
    const int64_t                  id,
    const std::vector<ipc::value>& args,
    std::vector<ipc::value>&       rval)
{
	// Attempt to find the source asked to load.
	obs_source_t* src = osn::Source::Manager::GetInstance().find(args[0].value_union.ui64);
	if (src == nullptr) {
		rval.push_back(ipc::value((uint64_t)ErrorCode::InvalidReference));
		rval.push_back(ipc::value("Source reference is not valid."));
		AUTO_DEBUG;
		return;
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	const char* sid = obs_source_get_id(src);
	rval.push_back(ipc::value(sid ? sid : ""));
	AUTO_DEBUG;
}

void osn::Source::Query(
    void*                          data,
    const int64_t                  id,
    const std::vector<ipc::value>& args,
    std::vector<ipc::value>&       rval)
{
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));

	std::unique_lock<std::mutex> ulock(source_hotkey_mtx);

	// For each source entry
	for (auto iter : pending_source_hotkeys) {
		// Check for an invalid reference
		obs_source_t* src = osn::Source::Manager::GetInstance().find(iter.first);
		if (src == nullptr) {
			continue;
		}

		// For each hotkey entry
		for (auto& hotkeyTuple : iter.second) {
			// rval.push_back(ipc::value(obs_source_get_name(src)));
			// rval.push_back(ipc::value(std::get<0>(hotkeyTuple)));
			// rval.push_back(ipc::value(std::get<1>(hotkeyTuple)));
			// rval.push_back(ipc::value(std::get<2>(hotkeyTuple)));
		}
	}

	pending_source_hotkeys.clear();

	AUTO_DEBUG;
}

void osn::Source::ProcessHotkeyStatus(
    void*                          data,
    const int64_t                  id,
    const std::vector<ipc::value>& args,
    std::vector<ipc::value>&       rval)
{
	obs_hotkey_id hotkeyId	= args[0].value_union.ui64;
	uint64_t press			= args[1].value_union.i32;

	// Attempt to find the source asked to load.
	obs_source_t* src = obs_get_source_by_name(args[2].value_str.c_str());
	if (src == nullptr) {
		rval.push_back(ipc::value((uint64_t)ErrorCode::InvalidReference));
		rval.push_back(ipc::value("Source reference is not valid."));
		AUTO_DEBUG;
		return;
	}

	// TODO: Check if the hotkey ID is valid
	obs_hotkey_enable_callback_rerouting(true);
	obs_hotkey_trigger_routed_callback(hotkeyId, (bool)press);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));

	AUTO_DEBUG;
}

void osn::Source::GetMuted(
    void*                          data,
    const int64_t                  id,
    const std::vector<ipc::value>& args,
    std::vector<ipc::value>&       rval)
{
	// Attempt to find the source asked to load.
	obs_source_t* src = osn::Source::Manager::GetInstance().find(args[0].value_union.ui64);
	if (src == nullptr) {
		rval.push_back(ipc::value((uint64_t)ErrorCode::InvalidReference));
		rval.push_back(ipc::value("Source reference is not valid."));
		AUTO_DEBUG;
		return;
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(obs_source_muted(src)));
	AUTO_DEBUG;
}

void osn::Source::SetMuted(
    void*                          data,
    const int64_t                  id,
    const std::vector<ipc::value>& args,
    std::vector<ipc::value>&       rval)
{
	// Attempt to find the source asked to load.
	obs_source_t* src = osn::Source::Manager::GetInstance().find(args[0].value_union.ui64);
	if (src == nullptr) {
		rval.push_back(ipc::value((uint64_t)ErrorCode::InvalidReference));
		rval.push_back(ipc::value("Source reference is not valid."));
		AUTO_DEBUG;
		return;
	}

	obs_source_set_muted(src, !!args[1].value_union.i32);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(obs_source_muted(src)));
	AUTO_DEBUG;
}

void osn::Source::GetEnabled(
    void*                          data,
    const int64_t                  id,
    const std::vector<ipc::value>& args,
    std::vector<ipc::value>&       rval)
{
	// Attempt to find the source asked to load.
	obs_source_t* src = osn::Source::Manager::GetInstance().find(args[0].value_union.ui64);
	if (src == nullptr) {
		rval.push_back(ipc::value((uint64_t)ErrorCode::InvalidReference));
		rval.push_back(ipc::value("Source reference is not valid."));
		AUTO_DEBUG;
		return;
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(obs_source_enabled(src)));
	AUTO_DEBUG;
}

void osn::Source::SetEnabled(
    void*                          data,
    const int64_t                  id,
    const std::vector<ipc::value>& args,
    std::vector<ipc::value>&       rval)
{
	// Attempt to find the source asked to load.
	obs_source_t* src = osn::Source::Manager::GetInstance().find(args[0].value_union.ui64);
	if (src == nullptr) {
		rval.push_back(ipc::value((uint64_t)ErrorCode::InvalidReference));
		rval.push_back(ipc::value("Source reference is not valid."));
		AUTO_DEBUG;
		return;
	}

	obs_source_set_enabled(src, !!args[1].value_union.i32);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(obs_source_enabled(src)));
	AUTO_DEBUG;
}

osn::Source::Manager& osn::Source::Manager::GetInstance()
{
	static osn::Source::Manager _inst;
	return _inst;
}