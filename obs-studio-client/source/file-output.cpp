/******************************************************************************
    Copyright (C) 2016-2022 by Streamlabs (General Workings Inc)

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

******************************************************************************/

#include "file-output.hpp"
#include "utility.hpp"
#include "video.hpp"

Napi::Value osn::FileOutput::GetPath(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("FileOutput", "GetPath", {ipc::value(this->uid)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return Napi::String::New(info.Env(), response[1].value_str);
}

void osn::FileOutput::SetPath(const Napi::CallbackInfo &info, const Napi::Value &value)
{
	auto conn = GetConnection(info);
	if (!conn)
		return;

	conn->call_synchronous_helper("FileOutput", "SetPath", {ipc::value(this->uid), ipc::value(value.ToString().Utf8Value())});
}

Napi::Value osn::FileOutput::GetCanvas(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("FileOutput", "GetVideoCanvas", {ipc::value(this->uid)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	auto instance = osn::Video::constructor.New({Napi::Number::New(info.Env(), static_cast<uint32_t>(response[1].value_union.ui64))});

	return instance;
}

void osn::FileOutput::SetCanvas(const Napi::CallbackInfo &info, const Napi::Value &value)
{
	osn::Video *canvas = Napi::ObjectWrap<osn::Video>::Unwrap(value.ToObject());

	if (!canvas) {
		Napi::TypeError::New(info.Env(), "Invalid canvas argument").ThrowAsJavaScriptException();
		return;
	}

	auto conn = GetConnection(info);
	if (!conn)
		return;

	conn->call_synchronous_helper("FileOutput", "SetVideoCanvas", {ipc::value(this->uid), ipc::value(canvas->canvasId)});
}

Napi::Value osn::FileOutput::GetFormat(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("FileOutput", "GetFormat", {ipc::value(this->uid)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return Napi::String::New(info.Env(), response[1].value_str);
}

void osn::FileOutput::SetFormat(const Napi::CallbackInfo &info, const Napi::Value &value)
{
	auto conn = GetConnection(info);
	if (!conn)
		return;

	conn->call_synchronous_helper("FileOutput", "SetFormat", {ipc::value(this->uid), ipc::value(value.ToString().Utf8Value())});
}

Napi::Value osn::FileOutput::GetFileFormat(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("FileOutput", "GetFileFormat", {ipc::value(this->uid)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return Napi::String::New(info.Env(), response[1].value_str);
}

void osn::FileOutput::SetFileFormat(const Napi::CallbackInfo &info, const Napi::Value &value)
{
	auto conn = GetConnection(info);
	if (!conn)
		return;

	conn->call_synchronous_helper("FileOutput", "SetFileFormat", {ipc::value(this->uid), ipc::value(value.ToString().Utf8Value())});
}

Napi::Value osn::FileOutput::GetOverwrite(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("FileOutput", "GetOverwrite", {ipc::value(this->uid)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return Napi::Boolean::New(info.Env(), response[1].value_union.ui32);
}

void osn::FileOutput::SetOverwrite(const Napi::CallbackInfo &info, const Napi::Value &value)
{
	auto conn = GetConnection(info);
	if (!conn)
		return;

	conn->call_synchronous_helper("FileOutput", "SetOverwrite", {ipc::value(this->uid), ipc::value((uint32_t)value.ToBoolean().Value())});
}

Napi::Value osn::FileOutput::GetNoSpace(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("FileOutput", "GetNoSpace", {ipc::value(this->uid)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return Napi::Boolean::New(info.Env(), response[1].value_union.ui32);
}

void osn::FileOutput::SetNoSpace(const Napi::CallbackInfo &info, const Napi::Value &value)
{
	auto conn = GetConnection(info);
	if (!conn)
		return;

	conn->call_synchronous_helper("FileOutput", "SetNoSpace", {ipc::value(this->uid), ipc::value((uint32_t)value.ToBoolean().Value())});
}

Napi::Value osn::FileOutput::GetMuxerSettings(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("FileOutput", "GetMuxerSettings", {ipc::value(this->uid)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return Napi::String::New(info.Env(), response[1].value_str);
}

void osn::FileOutput::SetMuxerSettings(const Napi::CallbackInfo &info, const Napi::Value &value)
{
	auto conn = GetConnection(info);
	if (!conn)
		return;

	conn->call_synchronous_helper("FileOutput", "SetMuxerSettings", {ipc::value(this->uid), ipc::value(value.ToString().Utf8Value())});
}

Napi::Value osn::FileOutput::GetLastFile(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("FileOutput", "GetLastFile", {ipc::value(this->uid)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return Napi::String::New(info.Env(), response[1].value_str);
}