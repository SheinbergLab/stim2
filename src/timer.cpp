#include "timer.h"

#ifndef __EMSCRIPTEN__

void Timer::threadStart()
{
    ScopedLock lock(sync);

    while (!done)
    {
        if (queue.empty())
        {
            // Wait (forever) for work
            wakeUp.wait(lock);
        }
        else
        {
            auto firstInstance = queue.begin();
            Instance& instance = *firstInstance;
            auto now = Clock::now();
            if (now >= instance.next)
            {
                queue.erase(firstInstance);

                // Mark it as running to handle racing destroy
                instance.running = true;

                // Call the handler
                lock.unlock();
                instance.handler();
                lock.lock();

                if (done)
                {
                    break;
                }
                else if (!instance.running)
                {
                    // Running was set to false, destroy was called
                    // for this Instance while the callback was in progress
                    // (this thread was not holding the lock during the callback)
                    active.erase(instance.id);
                }
                else
                {
                    instance.running = false;

                    // If it is periodic, schedule a new one
                    if (instance.period.count() > 0)
                    {
                        instance.next = instance.next + instance.period;
                        queue.insert(instance);
                    } else {
                        active.erase(instance.id);
                    }
                }
            } else {
                // Wait until the timer is ready or a timer creation notifies
                wakeUp.wait_until(lock, instance.next);
            }
        }
    }
}

Timer::Timer()
    : nextId(1)
    , queue(comparator)
    , done(false)
{
    ScopedLock lock(sync);
    worker = std::thread(std::bind(&Timer::threadStart, this));
}

Timer::~Timer()
{
    ScopedLock lock(sync);
    done = true;
    wakeUp.notify_all();
    lock.unlock();
    worker.join();
}

#else

#include <emscripten/emscripten.h>
#include <unordered_set>

static std::unordered_set<Timer *> live_timers;

void Timer::emPump(void *vp)
{
    Timer *self = static_cast<Timer *>(vp);
    if (!live_timers.count(self))
        return;
    self->pump();
}

void Timer::scheduleLocked()
{
    if (done || pending || queue.empty())
        return;
    auto wait = std::chrono::duration_cast<Duration>(
        queue.begin()->get().next - Clock::now());
    int ms = (int) wait.count();
    if (ms < 0)
        ms = 0;
    pending = true;
    emscripten_async_call(Timer::emPump, this, ms);
}

void Timer::pump()
{
    ScopedLock lock(sync);
    if (done)
        return;
    pending = false;
    if (queue.empty())
        return;

    auto firstInstance = queue.begin();
    Instance& instance = *firstInstance;
    if (Clock::now() < instance.next)
    {
        scheduleLocked();
        return;
    }

    queue.erase(firstInstance);
    instance.running = true;
    lock.unlock();
    instance.handler();
    lock.lock();

    if (done)
        return;
    else if (!instance.running)
    {
        active.erase(instance.id);
    }
    else
    {
        instance.running = false;
        if (instance.period.count() > 0)
        {
            instance.next = instance.next + instance.period;
            queue.insert(instance);
        }
        else
        {
            active.erase(instance.id);
        }
    }
    scheduleLocked();
}

Timer::Timer()
    : nextId(1)
    , queue(comparator)
    , pending(false)
    , done(false)
{
    live_timers.insert(this);
}

Timer::~Timer()
{
    ScopedLock lock(sync);
    done = true;
    live_timers.erase(this);
}

#endif

Timer::timer_id Timer::create(uint64_t msFromNow, uint64_t msPeriod,
        const std::function<void()> &handler)
{
    return createImpl(Instance(0,
            Clock::now() + Duration(msFromNow), Duration(msPeriod),
            handler));
}

Timer::timer_id Timer::create(uint64_t msFromNow, uint64_t msPeriod,
        std::function<void()>&& handler)
{
    return createImpl(Instance(0,
            Clock::now() + Duration(msFromNow), Duration(msPeriod),
            std::move(handler)));
}

Timer::timer_id Timer::createImpl(Instance&& item)
{
    ScopedLock lock(sync);
    item.id = nextId++;
    auto iter = active.emplace(item.id, std::move(item));
    queue.insert(iter.first->second);
    wakeUp.notify_all();
#ifdef __EMSCRIPTEN__
    scheduleLocked();
#endif
    return item.id;
}

bool Timer::destroy(timer_id id)
{
    ScopedLock lock(sync);
    auto i = active.find(id);
    if (i == active.end())
        return false;
    else if (i->second.running)
    {
        // A callback is in progress for this Instance,
        // so flag it for deletion in the worker
        i->second.running = false;
    }
    else
    {
        queue.erase(std::ref(i->second));
        active.erase(i);
    }

    wakeUp.notify_all();
    return true;
}

bool Timer::exists(timer_id id)
{
    ScopedLock lock(sync);
    return active.find(id) != active.end();
}
