#include "platform/UpdatePingInternal.hpp"

#import <Foundation/Foundation.h>

#include <memory>
#include <string>

namespace galaxy {
namespace {

struct RequestDone {
    explicit RequestDone() : semaphore(dispatch_semaphore_create(0)) {}
    ~RequestDone() { dispatch_release(semaphore); }
    dispatch_semaphore_t semaphore;
};

}  // namespace

void postUpdateCheck(std::string_view body) {
    @autoreleasepool {
        NSString* address = @"https://starnexa.net/galaxy_editor_services/check_for_update/";
        NSMutableURLRequest* request = [NSMutableURLRequest requestWithURL:[NSURL URLWithString:address]];
        request.HTTPMethod = @"POST";
        request.cachePolicy = NSURLRequestReloadIgnoringLocalCacheData;
        request.timeoutInterval = 15.0;
        [request setValue:@"application/json" forHTTPHeaderField:@"Content-Type"];
        request.HTTPBody = [NSData dataWithBytes:body.data() length:body.size()];

        const auto done = std::make_shared<RequestDone>();
        NSURLSessionDataTask* task = [[NSURLSession sharedSession] dataTaskWithRequest:request
            completionHandler:^(NSData*, NSURLResponse*, NSError*) {
                dispatch_semaphore_signal(done->semaphore);
            }];
        [task resume];
        dispatch_semaphore_wait(done->semaphore, dispatch_time(DISPATCH_TIME_NOW, 20 * NSEC_PER_SEC));
    }
}

std::string hostPlatformName() {
    return "macOS";
}

std::string hostOperatingSystem() {
    @autoreleasepool {
        const NSOperatingSystemVersion version = [[NSProcessInfo processInfo] operatingSystemVersion];
        return "macOS " + std::to_string(version.majorVersion) + "." + std::to_string(version.minorVersion) + "."
            + std::to_string(version.patchVersion);
    }
}

}  // namespace galaxy
