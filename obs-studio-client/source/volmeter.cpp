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

#include "volmeter.hpp"
#include <iostream>
#include <iterator>
#include <vector>
#include "controller.hpp"
#include "error.hpp"
#include "isource.hpp"
#include "shared.hpp"
#include "utility-v8.hpp"
#include "utility.hpp"

using namespace std::placeholders;

osn::VolMeter::VolMeter(uint64_t p_uid)
{
	m_uid = p_uid;
}

osn::VolMeter::~VolMeter()
{
}

std::vector<std::unique_ptr<osn::VolMeter>> volmeters;

uint64_t osn::VolMeter::GetId() 
{
	return m_uid;
}

void osn::VolMeter::start_async_runner()
{
	if (m_async_callback)
		return;

	std::unique_lock<std::mutex> ul(m_worker_lock);

	// Start v8/uv asynchronous runner.
	m_async_callback = new osn::VolMeterCallback();
	m_async_callback->set_handler(std::bind(&VolMeter::callback_handler, this, _1, _2), nullptr);
}

void osn::VolMeter::stop_async_runner()
{
	if (!m_async_callback)
		return;

	std::unique_lock<std::mutex> ul(m_worker_lock);

	// Stop v8/uv asynchronous runner.
	m_async_callback->clear();
	m_async_callback->finalize();
	m_async_callback = nullptr;
}

void osn::VolMeter::callback_handler(void* data, std::shared_ptr<osn::VolMeterData> item)
{
	// utilv8::ToValue on a std::vector<> creates a v8::Local<v8::Array> automatically.
	v8::Local<v8::Value> args[] = {
	    utilv8::ToValue(item->magnitude), utilv8::ToValue(item->peak), utilv8::ToValue(item->input_peak)};

	Nan::Call(m_callback_function, 3, args);
}

void osn::VolMeter::start_worker()
{
	if (!m_worker_stop)
		return;

	// Launch worker thread.
	m_worker_stop = false;
	m_worker      = std::thread(std::bind(&osn::VolMeter::worker, this));
}

void osn::VolMeter::stop_worker()
{
	if (m_worker_stop != false)
		return;

	// Stop worker thread.
	m_worker_stop = true;
	if (m_worker.joinable()) {
		m_worker.join();
	}
}

void osn::VolMeter::worker()
{
	size_t totalSleepMS = 0;

	while (!m_worker_stop) {
		auto tp_start = std::chrono::high_resolution_clock::now();

		// Validate Connection
		auto conn = Controller::GetInstance().GetConnection();
		if (!conn) {
			goto do_sleep;
		}

		// Call
		try {
			std::unique_lock<std::mutex> ul(m_worker_lock);

			if (!m_async_callback)
				goto do_sleep;

			std::vector<ipc::value> response = conn->call_synchronous_helper(
			    "VolMeter",
			    "Query",
			    {
			        ipc::value(m_uid),
			    });
			if (!response.size()) {
				goto do_sleep;
			}
			if ((response.size() == 1) && (response[0].type == ipc::type::Null)) {
				goto do_sleep;
			}

			ErrorCode error = (ErrorCode)response[0].value_union.ui64;
			if (error == ErrorCode::Ok) {
				std::shared_ptr<osn::VolMeterData> data     = std::make_shared<osn::VolMeterData>();
				size_t                             channels = response[1].value_union.i32;
				data->magnitude.resize(channels);
				data->peak.resize(channels);
				data->input_peak.resize(channels);
				data->param = this;
				for (size_t ch = 0; ch < channels; ch++) {
					data->magnitude[ch]  = response[1 + ch * 3 + 0].value_union.fp32;
					data->peak[ch]       = response[1 + ch * 3 + 1].value_union.fp32;
					data->input_peak[ch] = response[1 + ch * 3 + 2].value_union.fp32;
				}
				m_async_callback->queue(std::move(data));
			} else {
				std::cerr << "Failed VolMeter" << std::endl;
				break;
			}
		} catch (std::exception e) {
			goto do_sleep;
		}

	do_sleep:
		auto tp_end  = std::chrono::high_resolution_clock::now();
		auto dur     = std::chrono::duration_cast<std::chrono::milliseconds>(tp_end - tp_start);
		totalSleepMS = m_sleep_interval - dur.count();
		std::this_thread::sleep_for(std::chrono::milliseconds(totalSleepMS));
	}
}

void osn::VolMeter::set_keepalive(v8::Local<v8::Object> obj)
{
	if (!m_async_callback)
		return;
	m_async_callback->set_keepalive(obj);
}

Nan::Persistent<v8::FunctionTemplate> osn::VolMeter::prototype;

void osn::VolMeter::Register(Nan::ADDON_REGISTER_FUNCTION_ARGS_TYPE target)
{
	auto fnctemplate = Nan::New<v8::FunctionTemplate>();
	fnctemplate->InstanceTemplate()->SetInternalFieldCount(1);
	fnctemplate->SetClassName(Nan::New<v8::String>("Volmeter").ToLocalChecked());

	// Class Template
	utilv8::SetTemplateField(fnctemplate, "create", Create);

	// Instance Template
	auto objtemplate = fnctemplate->PrototypeTemplate();
	utilv8::SetTemplateAccessorProperty(objtemplate, "updateInterval", GetUpdateInterval, SetUpdateInterval);
	utilv8::SetTemplateField(objtemplate, "attach", Attach);
	utilv8::SetTemplateField(objtemplate, "detach", Detach);
	utilv8::SetTemplateField(objtemplate, "addCallback", AddCallback);
	utilv8::SetTemplateField(objtemplate, "removeCallback", RemoveCallback);

	// Stuff
	utilv8::SetObjectField(target, "Volmeter", fnctemplate->GetFunction());
	prototype.Reset(fnctemplate);
}

Nan::NAN_METHOD_RETURN_TYPE osn::VolMeter::Create(Nan::NAN_METHOD_ARGS_TYPE info)
{
	int32_t fader_type;

	// Validate and retrieve parameters.
	ASSERT_INFO_LENGTH(info, 1);
	ASSERT_GET_VALUE(info[0], fader_type);

	// Validate Connection
	auto conn = Controller::GetInstance().GetConnection();
	if (!conn) {
		Nan::ThrowError("IPC is not connected.");
		return;
	}

	// Call
	std::vector<ipc::value> rval = conn->call_synchronous_helper(
	    "VolMeter",
	    "Create",
	    {
	        ipc::value(fader_type),
	    });
	if (!rval.size()) {
		Nan::ThrowError("Failed to make IPC call, verify IPC status.");
		return;
	}

	// Handle Unexpected Errors
	if ((rval.size() == 1) && (rval[0].type == ipc::type::Null)) {
		Nan::ThrowError(Nan::New(rval[0].value_str).ToLocalChecked());
		return;
	}

	// Handle Expected Errors
	ErrorCode ec = (ErrorCode)rval[0].value_union.ui64;
	if (ec == ErrorCode::InvalidReference) {
		Nan::ThrowReferenceError(Nan::New(rval[1].value_str).ToLocalChecked());
		return;
	} else if (ec != ErrorCode::Ok) {
		Nan::ThrowError(Nan::New(rval[1].value_str).ToLocalChecked());
		return;
	}

	// Return created Object
	auto newVolmeter              = std::make_unique<osn::VolMeter>(rval[1].value_union.ui64);
	newVolmeter->m_sleep_interval = rval[2].value_union.ui32;
	volmeters.push_back(std::move(newVolmeter));
	info.GetReturnValue().Set(Store(volmeters.back().get()));
}

Nan::NAN_METHOD_RETURN_TYPE
    osn::VolMeter::OBS_Volmeter_ReleaseVolmeters(const v8::FunctionCallbackInfo<v8::Value>& args)
{
	// Validate Connection
	auto conn = Controller::GetInstance().GetConnection();
	if (!conn) {
		return; // Well, we can't really do anything here then.
	}

	// For each volmeter
	for (auto& volmeter : volmeters) {
		volmeter->stop_async_runner();
		volmeter->stop_worker();

		// Call
		std::vector<ipc::value> rval = conn->call_synchronous_helper(
		    "VolMeter",
		    "Destroy",
		    {
		        ipc::value(volmeter->GetId()),
		    });

		if (!rval.size()) {
			return; // Nothing we can do.
		}
	}
}

Nan::NAN_METHOD_RETURN_TYPE osn::VolMeter::GetUpdateInterval(Nan::NAN_METHOD_ARGS_TYPE info)
{
	osn::VolMeter* self;

	// Validate and retrieve parameters.
	ASSERT_INFO_LENGTH(info, 0);

	if (!Retrieve(info.This(), self)) {
		return;
	}

	// Validate Connection
	auto conn = Controller::GetInstance().GetConnection();
	if (!conn) {
		Nan::ThrowError("IPC is not connected.");
		return;
	}

	// Call
	std::vector<ipc::value> rval = conn->call_synchronous_helper(
	    "VolMeter",
	    "GetUpdateInterval",
	    {
	        ipc::value(self->m_uid),
	    });
	if (!rval.size()) {
		Nan::ThrowError("Failed to make IPC call, verify IPC status.");
		return;
	}

	// Handle Unexpected Errors
	if ((rval.size() == 1) && (rval[0].type == ipc::type::Null)) {
		Nan::ThrowError(Nan::New(rval[0].value_str).ToLocalChecked());
		return;
	}

	// Handle Expected Errors
	ErrorCode ec = (ErrorCode)rval[0].value_union.ui64;
	if (ec == ErrorCode::InvalidReference) {
		Nan::ThrowReferenceError(Nan::New(rval[1].value_str).ToLocalChecked());
		return;
	} else if (ec != ErrorCode::Ok) {
		Nan::ThrowError(Nan::New(rval[1].value_str).ToLocalChecked());
		return;
	}

	self->m_sleep_interval = rval[1].value_union.ui32;

	// Return DeziBel Value
	info.GetReturnValue().Set(rval[1].value_union.ui32);
}

Nan::NAN_METHOD_RETURN_TYPE osn::VolMeter::SetUpdateInterval(Nan::NAN_METHOD_ARGS_TYPE info)
{
	uint32_t       interval;
	osn::VolMeter* self;

	// Validate and retrieve parameters.
	ASSERT_INFO_LENGTH(info, 1);
	ASSERT_GET_VALUE(info[0], interval);

	if (!Retrieve(info.This(), self)) {
		return;
	}

	// Validate Connection
	auto conn = Controller::GetInstance().GetConnection();
	if (!conn) {
		Nan::ThrowError("IPC is not connected.");
		return;
	}

	// Call
	std::vector<ipc::value> rval =
	    conn->call_synchronous_helper("VolMeter", "SetUpdateInterval", {ipc::value(self->m_uid), ipc::value(interval)});
	if (!rval.size()) {
		Nan::ThrowError("Failed to make IPC call, verify IPC status.");
		return;
	}

	// Handle Unexpected Errors
	if ((rval.size() == 1) && (rval[0].type == ipc::type::Null)) {
		Nan::ThrowError(Nan::New(rval[0].value_str).ToLocalChecked());
		return;
	}

	// Handle Expected Errors
	ErrorCode ec = (ErrorCode)rval[0].value_union.ui64;
	if (ec == ErrorCode::InvalidReference) {
		Nan::ThrowReferenceError(Nan::New(rval[1].value_str).ToLocalChecked());
		return;
	} else if (ec != ErrorCode::Ok) {
		Nan::ThrowError(Nan::New(rval[1].value_str).ToLocalChecked());
		return;
	}

	self->m_sleep_interval = interval;

	// Return DeziBel Value
	info.GetReturnValue().Set(rval[1].value_union.ui32);
}

Nan::NAN_METHOD_RETURN_TYPE osn::VolMeter::Attach(Nan::NAN_METHOD_ARGS_TYPE info)
{
	osn::VolMeter* fader;
	osn::ISource*  source;

	// Validate and retrieve parameters.
	ASSERT_INFO_LENGTH(info, 1);

	if (!Retrieve(info.This(), fader)) {
		return;
	}

	v8::Local<v8::Object> sourceObj;
	ASSERT_GET_VALUE(info[0], sourceObj);
	if (!osn::ISource::Retrieve(sourceObj, source)) {
		return;
	}

	// Validate Connection
	auto conn = Controller::GetInstance().GetConnection();
	if (!conn) {
		Nan::ThrowError("IPC is not connected.");
		return;
	}

	// Call
	std::vector<ipc::value> rval =
	    conn->call_synchronous_helper("VolMeter", "Attach", {ipc::value(fader->m_uid), ipc::value(source->sourceId)});
	if (!rval.size()) {
		Nan::ThrowError("Failed to make IPC call, verify IPC status.");
		return;
	}

	// Handle Unexpected Errors
	if ((rval.size() == 1) && (rval[0].type == ipc::type::Null)) {
		Nan::ThrowError(Nan::New(rval[0].value_str).ToLocalChecked());
		return;
	}

	// Handle Expected Errors
	ErrorCode ec = (ErrorCode)rval[0].value_union.ui64;
	if (ec == ErrorCode::InvalidReference) {
		Nan::ThrowReferenceError(Nan::New(rval[1].value_str).ToLocalChecked());
		return;
	} else if (ec != ErrorCode::Ok) {
		Nan::ThrowError(Nan::New(rval[1].value_str).ToLocalChecked());
		return;
	}
}

Nan::NAN_METHOD_RETURN_TYPE osn::VolMeter::Detach(Nan::NAN_METHOD_ARGS_TYPE info)
{
	osn::VolMeter* fader;

	// Validate and retrieve parameters.
	ASSERT_INFO_LENGTH(info, 0);

	if (!Retrieve(info.This(), fader)) {
		return;
	}

	// Validate Connection
	auto conn = Controller::GetInstance().GetConnection();
	if (!conn) {
		Nan::ThrowError("IPC is not connected.");
		return;
	}

	// Call
	std::vector<ipc::value> rval = conn->call_synchronous_helper("VolMeter", "Detach", {ipc::value(fader->m_uid)});
	if (!rval.size()) {
		Nan::ThrowError("Failed to make IPC call, verify IPC status.");
		return;
	}

	// Handle Unexpected Errors
	if ((rval.size() == 1) && (rval[0].type == ipc::type::Null)) {
		Nan::ThrowError(Nan::New(rval[0].value_str).ToLocalChecked());
		return;
	}

	// Handle Expected Errors
	ErrorCode ec = (ErrorCode)rval[0].value_union.ui64;
	if (ec == ErrorCode::InvalidReference) {
		Nan::ThrowReferenceError(Nan::New(rval[1].value_str).ToLocalChecked());
		return;
	} else if (ec != ErrorCode::Ok) {
		Nan::ThrowError(Nan::New(rval[1].value_str).ToLocalChecked());
		return;
	}
}

Nan::NAN_METHOD_RETURN_TYPE osn::VolMeter::AddCallback(Nan::NAN_METHOD_ARGS_TYPE info)
{
	osn::VolMeter*          self;
	v8::Local<v8::Function> callback;

	{
		// Arguments
		ASSERT_INFO_LENGTH(info, 1);
		if (!Retrieve(info.This(), self)) {
			return;
		}

		ASSERT_GET_VALUE(info[0], callback);
	}

	{
		// Grab IPC Connection
		std::shared_ptr<ipc::client> conn = nullptr;
		if (!(conn = GetConnection())) {
			return;
		}

		// Send request
		std::vector<ipc::value> rval =
		    conn->call_synchronous_helper("VolMeter", "AddCallback", {ipc::value(self->m_uid)});
		if (!ValidateResponse(rval)) {
			return;
		}

		if (rval[0].value_union.ui64 != (uint64_t)ErrorCode::Ok) {
			info.GetReturnValue().Set(Nan::Null());
			return;
		}
	}

	self->m_callback_function.Reset(callback);
	self->start_async_runner();
	self->set_keepalive(info.This());
	self->start_worker();

	info.GetReturnValue().Set(true);
}

Nan::NAN_METHOD_RETURN_TYPE osn::VolMeter::RemoveCallback(Nan::NAN_METHOD_ARGS_TYPE info)
{
	osn::VolMeter* self;

	{
		ASSERT_INFO_LENGTH(info, 1);
		if (!Retrieve(info.This(), self)) {
			return;
		}
	}

	self->stop_worker();
	self->stop_async_runner();
	self->m_callback_function.Reset();

	// Grab IPC Connection
	{
		std::shared_ptr<ipc::client> conn = nullptr;
		if (!(conn = GetConnection())) {
			return;
		}

		// Send request
		std::vector<ipc::value> rval =
		    conn->call_synchronous_helper("VolMeter", "RemoveCallback", {ipc::value(self->m_uid)});
		if (!ValidateResponse(rval)) {
			return;
		}
	}

	info.GetReturnValue().Set(true);
}

INITIALIZER(nodeobs_fader)
{
	initializerFunctions.push([](v8::Local<v8::Object> exports) {
		NODE_SET_METHOD(exports, "OBS_Volmeter_ReleaseVolmeters", osn::VolMeter::OBS_Volmeter_ReleaseVolmeters);
	});
}