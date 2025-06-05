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

#import "Foundation/Foundation.h"
#import <AVFoundation/AVFoundation.h>
#import <Cocoa/Cocoa.h>
#import <Security/Security.h>
#import <Foundation/Foundation.h>
#import <SystemExtensions/SystemExtensions.h>

#include "util-osx-int.h"
#include "util-osx.hpp"

typedef std::function<void(void *data, bool webcam, bool mic)> perms_cb;
typedef std::function<void(void *data, bool isInstalled)> virtualcam_cb;
extern std::string g_server_working_dir;

@interface UtilImplObj : NSObject
@end
