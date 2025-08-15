/******************************************************************************
    Copyright (C) 2016-2020 by Streamlabs (General Workings Inc)

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

#ifndef __UTIL_OBJC_INTERFACE_H__
#define __UTIL_OBJC_INTERFACE_H__

#include <string>
#include <functional>

typedef std::function<void(void *data, bool webcam, bool mic)> perms_cb;

class UtilObjCInt {
private:
	bool m_webcam_perm;
	bool m_mic_perm;
	void *m_async_cb;

public:
	UtilObjCInt(void);
	~UtilObjCInt(void);

	void init(void);
	void getPermissionsStatus(bool &webcam, bool &mic);
	void requestPermissions(void *async_cb, perms_cb cb);
	int installPlugin(void);
	int uninstallPlugin(void);
	void setServerWorkingDirectoryPath(std::string path);
	bool isPluginInstalled();

private:
	void *self;
};

#endif
