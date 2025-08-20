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

std::string UtilInt::getDefaultVideoSavePath(void)
{
	return _impl->getDefaultVideoSavePath();
}

void UtilInt::runApplication(void)
{
	_impl->runApplication();
}

void UtilInt::stopApplication(void)
{
	_impl->stopApplication();
}

unsigned long long UtilInt::getTotalPhysicalMemory(void)
{
	return _impl->getTotalPhysicalMemory();
}

unsigned long long UtilInt::getAvailableMemory(void)
{
	return _impl->getAvailableMemory();
}

std::vector<std::pair<uint32_t, uint32_t>> UtilInt::getAvailableScreenResolutions(void)
{
	return _impl->getAvailableScreenResolutions();
}

std::string UtilInt::getUserDataPath(void)
{
	return _impl->getUserDataPath();
}

std::string UtilInt::getWorkingDirectory(void)
{
	return _impl->getWorkingDirectory();
}