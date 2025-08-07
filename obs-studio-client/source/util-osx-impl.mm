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

void uninstallLegacyDALPlugin()
{
	@try {
		NSDictionary *error = [NSDictionary dictionary];
		std::string cmd = "do shell script \"rm -rf /Library/CoreMediaIO/Plug-Ins/DAL/vcam-plugin.plugin \" with administrator privileges";

		NSString *script = [NSString stringWithCString:cmd.c_str() encoding:[NSString defaultCStringEncoding]];
		NSAppleScript *run = [[NSAppleScript alloc] initWithSource:script];
		[run executeAndReturnError:&error];
		NSLog(@"errors: %@", error);
	} @catch (NSException *exception) {
		std::cout << "Caught an NSException!" << std::endl;
		std::cout << "Name: " << [[exception name] UTF8String] << std::endl;
		std::cout << "Reason: " << [[exception reason] UTF8String] << std::endl;
	}
}

int executeTaskAndCaptureOutput(const std::string &path, NSArray<NSString *> *exeArguments = nullptr)
{
	int exitCode = 0;
	@autoreleasepool {
		@try {
			// Create an NSTask instance
			NSTask *task = [[NSTask alloc] init];

			// Set the launch path (program to execute)
			NSString *exePath = [NSString stringWithCString:path.c_str() encoding:[NSString defaultCStringEncoding]];
			task.launchPath = exePath; // Command (e.g., "ls")
			if (exeArguments && exeArguments.count > 0) {
				task.arguments = exeArguments;
			}

			// Set up a pipe to capture the output
			NSPipe *outputPipe = [NSPipe pipe];
			task.standardOutput = outputPipe; // Redirect standard output to the pipe
			task.standardError = outputPipe;  // Optional: Redirect errors as well

			[task launch];
			[task waitUntilExit];

			exitCode = [task terminationStatus];

			// Read the data from the pipe
			NSFileHandle *readHandle = [outputPipe fileHandleForReading];
			NSData *outputData = [readHandle readDataToEndOfFile];

			// Convert the data to a string
			NSString *outputString = [[NSString alloc] initWithData:outputData encoding:NSUTF8StringEncoding];

			// Print the captured output
			std::cout << "Child process output:\n" << outputString.UTF8String << std::endl;
			std::cout << "Child process exited with code: " << exitCode << std::endl;
		} @catch (NSException *exception) {
			std::cout << "Caught an NSException!" << std::endl;
			std::cout << "Name: " << [[exception name] UTF8String] << std::endl;
			std::cout << "Reason: " << [[exception reason] UTF8String] << std::endl;
			exitCode = 1;
		}
	}
	return exitCode;
}

// In our app bundle, we will search for the slobs-virtual-cam-installer.app which
// exclusively handles installing/uninstalling the mac camera system extension.
std::string getInstallerAppPath()
{
	NSString *bundlePath = [[NSBundle mainBundle] bundlePath];
	std::string app_framework_path = bundlePath.UTF8String;
	char delimiter = '/';

	size_t last_occurrence_pos = app_framework_path.rfind(delimiter);

	if (last_occurrence_pos != std::string::npos) {
		app_framework_path.erase(last_occurrence_pos + 1); // Erase from after the delimiter
	}
	app_framework_path += "slobs-virtual-cam-installer.app/Contents/MacOS/slobs-virtual-cam-installer";
	return app_framework_path;
}

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

int UtilObjCInt::installPlugin()
{
	const std::string path = getInstallerAppPath();
	return executeTaskAndCaptureOutput(path); // Run the installer
}

int UtilObjCInt::uninstallPlugin()
{
	uninstallLegacyDALPlugin();
	// Uninstall the camera system extension
	const std::string path = getInstallerAppPath();
	return executeTaskAndCaptureOutput(path, @[@"--deactivate"]); // Run the uninstaller
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
				// Edge case- if it shows "terminated waiting to uninstall on reboot" then technically the plugin was uninstalled.
				if ((text.find("com.streamlabs.slobs") != std::string::npos) &&
				    (text.find("terminated waiting to uninstall on reboot") == std::string::npos)) {
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
	return isInstalled;
}

@end
