#import <SystemExtensions/SystemExtensions.h>
#import "util-osx-systemextensionrequest.h"

@interface UtilOsxSystemExtensionDelegate : NSObject <OSSystemExtensionRequestDelegate> {
@private
    void *js_vcam_thread;
    virtualcam_cb vcam_callback;
}
@end

@implementation UtilOsxSystemExtensionDelegate

- (id)init:(void *)js_thread  withAsyncCallback:(virtualcam_cb)virtualcam_callback
{
    self = [super init];

    if (self) {
        js_vcam_thread = js_thread;
        vcam_callback = virtualcam_callback;
    }

    return self;
}

- (OSSystemExtensionReplacementAction)request:(OSSystemExtensionRequest *)request actionForReplacingExtension:(OSSystemExtensionProperties *)existing withExtension:(OSSystemExtensionProperties *)ext {

    return OSSystemExtensionReplacementActionReplace;
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

- (void)requestNeedsUserApproval:(nonnull OSSystemExtensionRequest *)request {
}


// Called when the activation request needs user authorization
- (void)requestNeedsUserAuthorization:(OSSystemExtensionRequest *)request {
    NSLog(@"User authorization is required to activate the system extension: %@", request.identifier);
    NSLog(@"Please instruct the user to provide the necessary authorization for the extension.");
}

- (void)request:(OSSystemExtensionRequest *)request foundProperties:(NSArray<OSSystemExtensionProperties *> *)properties {
    NSLog(@"[UtilOsx] properties count: %ld\n", (unsigned long)properties.count);
    bool isInstalled = false;
    //TODO: iterate thru the array passed in
    for (OSSystemExtensionProperties *property in properties) {
        NSLog(@"rno prop %@", property.bundleIdentifier);
        if ([property.bundleIdentifier isEqualToString:@"com.streamlabs.slobs.mac-camera-extension"]) {
            isInstalled = true;
            NSLog(@"rno found bundle com.streamlabs.slobs.mac-camera-extension");
        }
    }
    
    if (vcam_callback && js_vcam_thread)
        vcam_callback(js_vcam_thread, isInstalled);
}
@end

void UtilOsxSystemExtensionRequest::requestVirtualCamInstallation(void *js_thread, virtualcam_cb callback)
{
    if (@available(macOS 12.0, *)) {
        id delegate = [[UtilOsxSystemExtensionDelegate alloc] init:js_thread withAsyncCallback:callback];
        // We need to use streamlabs teamID/Bundle identifier here.
        OSSystemExtensionRequest *request = [OSSystemExtensionRequest
                                             propertiesRequestForExtension:@"com.streamlabs.slobs.mac-camera-extension"
                                             queue:dispatch_get_main_queue()];
        request.delegate = delegate;
        
        [[OSSystemExtensionManager sharedManager] submitRequest:request];
    } else if (callback && js_thread) {
        callback(js_thread, false); // cannot determine if plugin installed.
    }
}
