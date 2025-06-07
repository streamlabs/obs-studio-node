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

#include "util-osx-impl.h"
#include <iostream>

std::string g_server_working_dir;

@implementation UtilImplObj

UtilObjCInt::UtilObjCInt(void) : self(NULL) {}

UtilObjCInt::~UtilObjCInt(void)
{
	[(id)self dealloc];
}

void UtilObjCInt::init(void)
{
	self = [[UtilImplObj alloc] init];

	m_webcam_perm = false;
	m_mic_perm = false;
}

void UtilObjCInt::getPermissionsStatus(bool &webcam, bool &mic)
{
	NSOperatingSystemVersion OSversion = [NSProcessInfo processInfo].operatingSystemVersion;
	if (OSversion.majorVersion >= 10 && OSversion.minorVersion >= 14) {
		AVAuthorizationStatus camStatus = [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeVideo];
		webcam = camStatus == AVAuthorizationStatusAuthorized;

		AVAuthorizationStatus micStatus = [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio];
		mic = micStatus == AVAuthorizationStatusAuthorized;

		m_webcam_perm = webcam;
		m_mic_perm = mic;
	} else {
		webcam = true;
		mic = true;
	}
}

void UtilObjCInt::requestPermissions(void *async_cb, perms_cb cb)
{
	NSOperatingSystemVersion OSversion = [NSProcessInfo processInfo].operatingSystemVersion;
	if ((OSversion.majorVersion >= 10 && OSversion.minorVersion >= 14) || OSversion.majorVersion > 10) {
		m_async_cb = async_cb;
		[AVCaptureDevice requestAccessForMediaType:AVMediaTypeVideo
					 completionHandler:^(BOOL granted) {
						 m_webcam_perm = granted;
						 cb(m_async_cb, m_webcam_perm, m_mic_perm);
					 }];

		[AVCaptureDevice requestAccessForMediaType:AVMediaTypeAudio
					 completionHandler:^(BOOL granted) {
						 m_mic_perm = granted;
						 cb(m_async_cb, m_webcam_perm, m_mic_perm);
					 }];
	}
}

void UtilObjCInt::setServerWorkingDirectoryPath(std::string path)
{
	path.erase(path.length() - strlen("/bin"));
	g_server_working_dir = path;
}

bool replace(std::string &str, const std::string &from, const std::string &to)
{
	size_t start_pos = str.find(from);
	if (start_pos == std::string::npos)
		return false;
	str.replace(start_pos, from.length(), to);
	return true;
}

void UtilObjCInt::installPlugin()
{
	NSDictionary *error = [NSDictionary dictionary];
	std::string pathToScript = g_server_working_dir + "/data/obs-plugins/slobs-virtual-cam/install-plugin.sh";
	std::cout << "launching: " << pathToScript.c_str() << std::endl;

	replace(pathToScript, " ", "\\\\ ");
	std::string arg = g_server_working_dir + "/data/obs-plugins/slobs-virtual-cam";
	replace(arg, " ", "\\\\ ");
	std::string cmd = "do shell script \"/bin/sh " + pathToScript + " " + arg + "\" with administrator privileges";

	NSString *script = [NSString stringWithCString:cmd.c_str() encoding:[NSString defaultCStringEncoding]];
	NSAppleScript *run = [[NSAppleScript alloc] initWithSource:script];
	[run executeAndReturnError:&error];
	NSLog(@"errors: %@", error);
}

void UtilObjCInt::uninstallPlugin()
{
	NSDictionary *error = [NSDictionary dictionary];
	std::string cmd = "do shell script \"rm -rf /Library/CoreMediaIO/Plug-Ins/DAL/vcam-plugin.plugin \" with administrator privileges";

	NSString *script = [NSString stringWithCString:cmd.c_str() encoding:[NSString defaultCStringEncoding]];
	NSAppleScript *run = [[NSAppleScript alloc] initWithSource:script];
	[run executeAndReturnError:&error];
	NSLog(@"errors: %@", error);
}

bool UtilObjCInt::isPluginInstalled()
{
	const std::string command("systemextensionsctl list");
	std::array<char, 256> buffer;
	std::string result;
	bool isInstalled = false;

	// Open a pipe to execute the command
	FILE *pipe = popen(command.c_str(), "r");
	if (pipe) {
		try {
			// Read the output from the command execution
			while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
				std::string text(buffer.data());
				if (text.find("com.streamlabs.slobs.mac-camera-extension") != std::string::npos) {
					isInstalled = true;
				}
			}
		} catch (...) {
			std::cerr << "Exception occurred during reading the buffer" << std::endl;
		}
	} else {
		std::cerr << "Could not run the system command:" << command << std::endl;
	}

	pclose(pipe);
	std::cout << "isVirtualCamInstalled: " << isInstalled << std::endl;
	return isInstalled;
}

@end
