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

#pragma once
#include <nan.h>
#include <node.h>
#include "utility-v8.hpp"

namespace osn
{
	struct SourceHotkeyInfo
	{
		std::string sourceName;
		std::string hotkeyName;
		std::string hotkeyDesc;
		size_t		hotkeyId;
	};

	typedef utilv8::managed_callback<std::shared_ptr<std::vector<SourceHotkeyInfo>>>	SourceCallback;

	class ISource : public Nan::ObjectWrap, public utilv8::InterfaceObject<osn::ISource>
	{
		friend class utilv8::InterfaceObject<osn::ISource>;

		protected:
		static Nan::Persistent<v8::FunctionTemplate> prototype;

		public:
		uint64_t sourceId;
		virtual ~ISource();

		private:
		uint64_t m_uid;
		uint32_t m_sleep_interval = 33;

		std::thread m_worker;
		bool        m_worker_stop = true;
		std::mutex  m_worker_lock;

		osn::SourceCallback*	m_async_callback = nullptr;
		Nan::Callback			m_callback_function;

		private:
		void start_async_runner();
		void stop_async_runner();
		void callback_handler(void* data, std::shared_ptr<std::vector<SourceHotkeyInfo>> item);

		void start_worker();
		void stop_worker();
		void worker();
		void set_keepalive(v8::Local<v8::Object>);

		public:
		static void Register(Nan::ADDON_REGISTER_FUNCTION_ARGS_TYPE target);

		static Nan::NAN_METHOD_RETURN_TYPE Release(Nan::NAN_METHOD_ARGS_TYPE info);
		static Nan::NAN_METHOD_RETURN_TYPE Remove(Nan::NAN_METHOD_ARGS_TYPE info);

		static Nan::NAN_METHOD_RETURN_TYPE IsConfigurable(Nan::NAN_METHOD_ARGS_TYPE info);
		static Nan::NAN_METHOD_RETURN_TYPE GetProperties(Nan::NAN_METHOD_ARGS_TYPE info);
		static Nan::NAN_METHOD_RETURN_TYPE GetSettings(Nan::NAN_METHOD_ARGS_TYPE info);
		static Nan::NAN_METHOD_RETURN_TYPE Update(Nan::NAN_METHOD_ARGS_TYPE info);
		static Nan::NAN_METHOD_RETURN_TYPE Load(Nan::NAN_METHOD_ARGS_TYPE info);
		static Nan::NAN_METHOD_RETURN_TYPE Save(Nan::NAN_METHOD_ARGS_TYPE info);

		static Nan::NAN_METHOD_RETURN_TYPE GetType(Nan::NAN_METHOD_ARGS_TYPE info);
		static Nan::NAN_METHOD_RETURN_TYPE GetName(Nan::NAN_METHOD_ARGS_TYPE info);
		static Nan::NAN_METHOD_RETURN_TYPE SetName(Nan::NAN_METHOD_ARGS_TYPE info);
		static Nan::NAN_METHOD_RETURN_TYPE GetOutputFlags(Nan::NAN_METHOD_ARGS_TYPE info);
		static Nan::NAN_METHOD_RETURN_TYPE GetFlags(Nan::NAN_METHOD_ARGS_TYPE info);
		static Nan::NAN_METHOD_RETURN_TYPE SetFlags(Nan::NAN_METHOD_ARGS_TYPE info);
		static Nan::NAN_METHOD_RETURN_TYPE GetStatus(Nan::NAN_METHOD_ARGS_TYPE info);
		static Nan::NAN_METHOD_RETURN_TYPE GetId(Nan::NAN_METHOD_ARGS_TYPE info);
		static Nan::NAN_METHOD_RETURN_TYPE GetMuted(Nan::NAN_METHOD_ARGS_TYPE info);
		static Nan::NAN_METHOD_RETURN_TYPE SetMuted(Nan::NAN_METHOD_ARGS_TYPE info);
		static Nan::NAN_METHOD_RETURN_TYPE GetEnabled(Nan::NAN_METHOD_ARGS_TYPE info);
		static Nan::NAN_METHOD_RETURN_TYPE SetEnabled(Nan::NAN_METHOD_ARGS_TYPE info);

		static void ConnectHotkeyCallback(const v8::FunctionCallbackInfo<v8::Value>& args);
		static void DisconnectHotkeyCallback(const v8::FunctionCallbackInfo<v8::Value>& args);
		static void ProcessHotkeyStatus(const v8::FunctionCallbackInfo<v8::Value>& args);
	};
} // namespace osn
