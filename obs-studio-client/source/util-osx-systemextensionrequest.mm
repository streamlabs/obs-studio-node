#import <SystemExtensions/SystemExtensions.h>
#import "util-osx-systemextensionrequest.h"

@interface MySystemExtensionDelegate : NSObject <OSSystemExtensionRequestDelegate> {
@private
    void *js_vcam_thread;
    virtualcam_cb vcam_callback;
}
@end

@implementation MySystemExtensionDelegate

- (id)init:(void *)js_vcam_thread  withAsyncCallback:(virtualcam_cb)virtualcam_callback
{
    self = [super init];

    if (self) {
        self->js_vcam_thread = js_vcam_thread;
        self->vcam_callback = virtualcam_callback;
    }

    return self;
}

- (OSSystemExtensionReplacementAction)request:(OSSystemExtensionRequest *)request actionForReplacingExtension:(OSSystemExtensionProperties *)existing withExtension:(OSSystemExtensionProperties *)ext {
    
}

// Called when the extension activation request completes successfully
- (void)request:(OSSystemExtensionRequest *)request didFinishWithResult:(OSSystemExtensionRequestResult)result {
    NSLog(@"System extension activation completed successfully: %@", request.identifier);

    switch (result) {
        case OSSystemExtensionRequestCompleted: // Activation successfully completed
            NSLog(@"Extension activated successfully.");
            break;

        default: // Handle other possible results
            NSLog(@"Unexpected activation result: %ld", (long)result);
            break;
    }
}

// Called when the extension activation request fails
- (void)request:(OSSystemExtensionRequest *)request didFailWithError:(NSError *)error {
    NSLog(@"System extension activation failed for extension: %@", request.identifier);
    NSLog(@"Error: %@", error);
}

// Called when the activation request needs user authorization
- (void)requestNeedsUserAuthorization:(OSSystemExtensionRequest *)request {
    NSLog(@"User authorization is required to activate the system extension: %@", request.identifier);
    NSLog(@"Please instruct the user to provide the necessary authorization for the extension.");
}

- (void)request:(OSSystemExtensionRequest *)request foundProperties:(NSArray<OSSystemExtensionProperties *> *)properties {
    NSLog(@"rno foundProperties");
    bool isInstalled = false;
    //TODO: iterate thru the array passed in
    for (OSSystemExtensionProperties *property in properties) {
        NSLog(@"rno prop %@", property.bundleIdentifier);
        if ([property.bundleIdentifier isEqualToString:@"com.streamlabs.slobs.mac-camera-extension"]) {
            isInstalled = true;
            NSLog(@"rno found bundle com.streamlabs.slobs.mac-camera-extension");
        }
    }
    
    vcam_callback(js_vcam_thread, isInstalled);
}
@end

void UtilOsxSystemExtensionRequest::requestVirtualCamInstallation(void *async_cb, virtualcam_cb cb)
{
    if (@available(macOS 12.0, *)) {
        id delegate = [[MySystemExtensionDelegate alloc] init:async_cb withAsyncCallback:cb];
        // We need to use streamlabs teamID/Bundle identifier here.
        OSSystemExtensionRequest *request = [OSSystemExtensionRequest
                                             propertiesRequestForExtension:@"com.streamlabs.slobs.mac-camera-extension"
                                             queue:dispatch_get_main_queue()];
        request.delegate = delegate;
        
        [[OSSystemExtensionManager sharedManager] submitRequest:request];
    }
}
