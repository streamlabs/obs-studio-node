#include "osn-multitrack-video-system-info.hpp"
#include "shared.hpp"
#include "util/platform.h"

#import <Foundation/Foundation.h>
#import <Foundation/NSProcessInfo.h>

namespace osn {

void fillSystemInfo(System &sysinfo)
{
	NSProcessInfo *procInfo = [NSProcessInfo processInfo];
	NSOperatingSystemVersion versionObj = [procInfo operatingSystemVersion];

	sysinfo.name = "macOS";
	sysinfo.bits = 64; // 32-bit macOS is long deprecated.
	sysinfo.version = [[procInfo operatingSystemVersionString] UTF8String];

	switch (versionObj.majorVersion) {
	case 11:
		sysinfo.release = "Big Sur";
		break;
	case 12:
		sysinfo.release = "Monterey";
		break;
	case 13:
		sysinfo.release = "Ventura";
		break;
	case 14:
		sysinfo.release = "Sonoma";
		break;
	case 15:
		sysinfo.release = "Sequoia";
		break;
	case 26:
		sysinfo.release = "Tahoe";
		break;
	default:
		sysinfo.release = "unknown";
	}

	sysinfo.arm = true;
	sysinfo.armEmulation = false;
}

void system_info(Capabilities &capabilities)
{
	capabilities.cpu.name = g_util_osx->getCpuName();
	// Getting the frequency is not supported on Apple Silicon.
	capabilities.cpu.physical_cores = os_get_physical_cores();
	capabilities.cpu.logical_cores = os_get_logical_cores();

	capabilities.memory.total = os_get_sys_total_size();
	capabilities.memory.free = os_get_sys_free_size();

	// Apple Silicon- there's only going to be gpu
	Gpu gpu;
	gpu.model = capabilities.cpu.name.value_or("Unknown");
	gpu.vendor_id = 0x106b; // Always Apple
	gpu.device_id = 0;      // Always 0 for Apple Silicon

	std::vector<Gpu> gpus;
	gpus.push_back(std::move(gpu));
	capabilities.gpu = gpus;
	fillSystemInfo(capabilities.system);
}

} // namespace osn
