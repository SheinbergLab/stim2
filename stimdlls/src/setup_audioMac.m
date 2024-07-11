#include <AudioToolbox/AudioToolbox.h>

void init_coreaudio( void )
{    
  // This is a critical bit of code! Prior to OSX 10.6, the Audio Object's
  // runloop ran on its own thread. After that, it was attached to the app's main thread,
  // and if you ever starved it, you just lost notifications. This code causes the 
  // run loop to detach and run its own thread as it used to.

  CFRunLoopRef Loop = NULL;
  AudioObjectPropertyAddress PA =
  {
    kAudioHardwarePropertyRunLoop,
    kAudioObjectPropertyScopeGlobal,
    kAudioObjectPropertyElementMaster
  };
  AudioObjectSetPropertyData( kAudioObjectSystemObject, &PA, 0, NULL, sizeof(CFRunLoopRef), &Loop);
}
