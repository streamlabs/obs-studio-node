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

#ifndef __UTIL_CLASS_H__
#define __UTIL_CLASS_H__

#include <string>
#include <vector>

class UtilObjCInt;

class UtilInt {
public:
	UtilInt(void);
	~UtilInt(void);

	void init(void);
	std::string getDefaultVideoSavePath(void);
	void runApplication(void);
	void stopApplication(void);
	unsigned long long getTotalPhysicalMemory(void);
	unsigned long long getAvailableMemory(void);
	std::vector<std::pair<uint32_t, uint32_t>> getAvailableScreenResolutions(void);
	std::string getUserDataPath(void);
	std::string getWorkingDirectory(void);

private:
	UtilObjCInt *_impl;
};

#endif
