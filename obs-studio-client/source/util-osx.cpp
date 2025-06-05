/******************************************************************************
    Copyright (C) 2016-2019 by Streamlabs (General Workings Inc)

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

#include "util-osx.hpp"
#include "util-osx-int.h"

UtilInt::UtilInt(void) : _impl(nullptr) {}

void UtilInt::init(void)
{
	_impl = new UtilObjCInt();
}

UtilInt::~UtilInt(void)
{
	if (_impl) {
		delete _impl;
		_impl = nullptr;
	}
}

void UtilInt::getPermissionsStatus(bool &webcam, bool &mic)
{
	_impl->getPermissionsStatus(webcam, mic);
}

void UtilInt::requestPermissions(void *async_cb, perms_cb cb)
{
	_impl->requestPermissions(async_cb, cb);
}

void UtilInt::installPlugin(void)
{
	_impl->installPlugin();
}

void UtilInt::uninstallPlugin(void)
{
	_impl->uninstallPlugin();
}

void UtilInt::setServerWorkingDirectoryPath(std::string path)
{
	_impl->setServerWorkingDirectoryPath(path);
}

void UtilInt::requestCamExtCheck(void *async_cb, virtualcam_cb cb)
{
	_impl->requestCamExtCheck(async_cb, cb);
}
