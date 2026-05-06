#include "nodeobs_settings-osx.h"

#import <AVFoundation/AVFoundation.h>

std::vector<std::pair<std::string, std::string>> getVideoDevicesMacOS()
{
	std::vector<std::pair<std::string, std::string>> result;

	if (@available(macOS 12.0, *)) {
		NSMutableArray<AVCaptureDeviceType> *deviceTypes = [NSMutableArray arrayWithObject:AVCaptureDeviceTypeBuiltInWideAngleCamera];

		if (@available(macOS 13.0, *)) {
			[deviceTypes addObject:AVCaptureDeviceTypeExternal];
		} else {
			// AVCaptureDeviceTypeExternalUnknown is deprecated in macOS 13 in favour of
			// AVCaptureDeviceTypeExternal, but is the correct constant for macOS 12.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
			[deviceTypes addObject:AVCaptureDeviceTypeExternalUnknown];
#pragma clang diagnostic pop
		}

		AVCaptureDeviceDiscoverySession *session = [AVCaptureDeviceDiscoverySession discoverySessionWithDeviceTypes:deviceTypes
														  mediaType:AVMediaTypeVideo
														   position:AVCaptureDevicePositionUnspecified];

		for (AVCaptureDevice *device in session.devices) {
			if (!device.localizedName || !device.uniqueID)
				continue;
			result.push_back({[device.localizedName UTF8String], [device.uniqueID UTF8String]});
		}
	}

	return result;
}
