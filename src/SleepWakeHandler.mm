
// SleepWakeHandler.mm (Objective-C++ implementation)
#include "SleepWakeHandler.h"

#ifdef __APPLE__
#import <Foundation/Foundation.h>
#import <Cocoa/Cocoa.h>

@interface SleepWakeObserver : NSObject
@property (nonatomic, copy) void(^sleepCallback)(void);
@property (nonatomic, copy) void(^wakeCallback)(void);
- (void)startObserving;
- (void)stopObserving;
@end

@implementation SleepWakeObserver

- (void)startObserving {
    NSNotificationCenter* center = [[NSWorkspace sharedWorkspace] notificationCenter];
    
    [center addObserver:self 
               selector:@selector(systemWillSleep:)
                   name:NSWorkspaceWillSleepNotification 
                 object:nil];
    
    [center addObserver:self 
               selector:@selector(systemDidWake:)
                   name:NSWorkspaceDidWakeNotification 
                 object:nil];
}

- (void)stopObserving {
    NSNotificationCenter* center = [[NSWorkspace sharedWorkspace] notificationCenter];
    [center removeObserver:self];
}

- (void)systemWillSleep:(NSNotification*)notification {
    if (self.sleepCallback) {
        self.sleepCallback();
    }
}

- (void)systemDidWake:(NSNotification*)notification {
    if (self.wakeCallback) {
        // Dispatch to a background queue to avoid blocking main thread
        dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
            self.wakeCallback();
        });
    }
}

- (void)dealloc {
    [self stopObserving];
#if !__has_feature(objc_arc)
    [super dealloc];
#endif
}

@end

struct SleepWakeHandler::Impl {
    SleepWakeObserver* observer;
    callback_type sleepCallback;
    callback_type wakeCallback;
    bool monitoring;
    
    Impl() : observer(nullptr), monitoring(false) {
        observer = [[SleepWakeObserver alloc] init];
    }
    
    ~Impl() {
        if (monitoring) {
            stopMonitoring();
        }
        observer = nil; // ARC will handle cleanup
    }
    
    void startMonitoring() {
        if (!monitoring) {
            // Set up the callbacks
            observer.sleepCallback = ^{
                if (sleepCallback) {
                    sleepCallback();
                }
            };
            
            observer.wakeCallback = ^{
                if (wakeCallback) {
                    wakeCallback();
                }
            };
            
            [observer startObserving];
            monitoring = true;
        }
    }
    
    void stopMonitoring() {
        if (monitoring) {
            [observer stopObserving];
            observer.sleepCallback = nil;
            observer.wakeCallback = nil;
            monitoring = false;
        }
    }
};

#else
// Non-macOS implementation (empty stubs)
struct SleepWakeHandler::Impl {
    callback_type sleepCallback;
    callback_type wakeCallback;
    
    void startMonitoring() {}
    void stopMonitoring() {}
};
#endif

// Common C++ implementation
SleepWakeHandler::SleepWakeHandler() : pImpl(new Impl()) {
}

SleepWakeHandler::~SleepWakeHandler() {
    delete pImpl;
}

void SleepWakeHandler::setSleepCallback(callback_type callback) {
    pImpl->sleepCallback = callback;
}

void SleepWakeHandler::setWakeCallback(callback_type callback) {
    pImpl->wakeCallback = callback;
}

void SleepWakeHandler::startMonitoring() {
    pImpl->startMonitoring();
}

void SleepWakeHandler::stopMonitoring() {
    pImpl->stopMonitoring();
}
