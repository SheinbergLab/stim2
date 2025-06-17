// SleepWakeHandler.h
#ifndef SLEEPWAKEHANDLER_H
#define SLEEPWAKEHANDLER_H

#include <functional>

class SleepWakeHandler {
public:
    typedef std::function<void()> callback_type;
    
    SleepWakeHandler();
    ~SleepWakeHandler();
    
    void setSleepCallback(callback_type callback);
    void setWakeCallback(callback_type callback);
    
    void startMonitoring();
    void stopMonitoring();
    
private:
    struct Impl;
    Impl* pImpl;
};

#endif // SLEEPWAKEHANDLER_H
