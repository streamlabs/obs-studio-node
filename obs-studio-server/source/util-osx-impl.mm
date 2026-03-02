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
#include <unistd.h>
#include <sys/types.h>
#include <sys/sysctl.h>

#import <mach/mach.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>

void UtilObjCInt::wait_terminate(void)
{
	const char *name = "/tmp/exit-slobs-crash-handler";
	std::vector<char> buffer;
	size_t count = 10;
	buffer.resize(count);
	remove(name);

	if (mkfifo(name, S_IRUSR | S_IWUSR) < 0)
		return;

	int file_descriptor = open(name, O_RDONLY | O_NONBLOCK);
	if (file_descriptor < 0)
		return;

	while (true) {
		if (!appRunning) {
			break;
		}
		int ret = ::read(file_descriptor, buffer.data(), count);
		if (ret > 0) {
			bool appCrashed = *reinterpret_cast<bool *>(buffer.data());
			if (appCrashed && appRunning)
				this->stopApplication();
			break;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
	}
	close(file_descriptor);
	remove(name);
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
}

std::string UtilObjCInt::getDefaultVideoSavePath(void)
{
	NSFileManager *fm = [NSFileManager defaultManager];
	NSURL *url = [fm URLForDirectory:NSMoviesDirectory inDomain:NSUserDomainMask appropriateForURL:nil create:true error:nil];

	if (!url)
		return getenv("HOME");

	return url.path.fileSystemRepresentation;
}

void UtilObjCInt::runApplication(void)
{
	appRunning = true;
	worker = new std::thread(&UtilObjCInt::wait_terminate, this);

	@autoreleasepool {
		NSApplication *app = [NSApplication sharedApplication];
		[app run];
	}
}

void UtilObjCInt::stopApplication(void)
{
	dispatch_async(dispatch_get_main_queue(), ^{
		[[NSApplication sharedApplication] stop:nil];
		NSEvent *event = [NSEvent otherEventWithType:NSEventTypeApplicationDefined
						    location:NSZeroPoint
					       modifierFlags:0
						   timestamp:0
						windowNumber:0
						     context:nil
						     subtype:0
						       data1:0
						       data2:0];
		[NSApp postEvent:event atStart:TRUE];

		appRunning = false;
		if (worker->joinable())
			worker->join();
	});
}

unsigned long long UtilObjCInt::getTotalPhysicalMemory(void)
{
	return [NSProcessInfo processInfo].physicalMemory;
}

unsigned long long UtilObjCInt::getAvailableMemory(void)
{
	mach_port_t host_port;
	mach_msg_type_number_t host_size;
	vm_size_t pagesize;

	host_port = mach_host_self();
	host_size = sizeof(vm_statistics_data_t) / sizeof(integer_t);
	host_page_size(host_port, &pagesize);

	vm_statistics_data_t vm_stat;

	if (host_statistics(host_port, HOST_VM_INFO, (host_info_t)&vm_stat, &host_size) != KERN_SUCCESS)
		return 0;

	// uint64_t mem_used = (uint64_t)(vm_stat.active_count +
	//                       vm_stat.inactive_count +
	//                       vm_stat.wire_count) * (uint64_t)pagesize;

	uint64_t mem_free = (uint64_t)vm_stat.free_count * (uint64_t)pagesize * 10;
	return mem_free;
}

std::vector<std::pair<uint32_t, uint32_t>> UtilObjCInt::getAvailableScreenResolutions(void)
{
	std::vector<std::pair<uint32_t, uint32_t>> resolutions;

	for (NSScreen *screen : [NSScreen screens]) {
		resolutions.push_back(std::make_pair([screen frame].size.width, [screen frame].size.height));
	}

	return resolutions;
}

std::string UtilObjCInt::getUserDataPath(void)
{
	NSArray *paths = NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory, NSUserDomainMask, YES);
	if ([paths count] == 0)
		return "";

	NSString *userPath = [paths objectAtIndex:0];
	return std::string([userPath UTF8String]);
}

std::string UtilObjCInt::getWorkingDirectory(void)
{
	NSString *workindDirPath = [[NSProcessInfo processInfo] environment][@"PWD"];
	return std::string([workindDirPath UTF8String]);
}

std::string UtilObjCInt::getComputerName(void)
{
	@autoreleasepool {
		NSString *computerName = [[NSHost currentHost] localizedName];
		if (computerName) {
			return [computerName UTF8String];
		}
	}
	return {};
}

int UtilObjCInt::getPhysicalCores(void)
{
	int physical_cores = 0;
	size_t size = sizeof(physical_cores);
	int ret = sysctlbyname("machdep.cpu.core_count", &physical_cores, &size, NULL, 0);
	if (ret != 0)
		return 0;
	return physical_cores;
}
@end
