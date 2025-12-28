#ifndef SHARED_QUEUE_H
#define SHARED_QUEUE_H

#include <deque>
#include <mutex>
#include <condition_variable>
#include <chrono>

template <typename T>
class SharedQueue
{
public:
  SharedQueue() {}
  ~SharedQueue() {}
  
  T& front() {
    std::unique_lock<std::mutex> mlock(mutex_);
    while (queue_.empty()) {
      cond_.wait(mlock);
    }
    return queue_.front();
  }
  
  void pop_front() {
    std::unique_lock<std::mutex> mlock(mutex_);
    while (queue_.empty()) {
      cond_.wait(mlock);
    }
    queue_.pop_front();
  }
  
  void push_back(const T& item) {
    std::unique_lock<std::mutex> mlock(mutex_);
    queue_.push_back(item);
    mlock.unlock();     // unlock before notification to minimize mutex contention
    cond_.notify_one(); // notify one waiting thread
  }
  
  void push_back(T&& item) {
    std::unique_lock<std::mutex> mlock(mutex_);
    queue_.push_back(std::move(item));
    mlock.unlock();     // unlock before notification to minimize mutex contention
    cond_.notify_one(); // notify one waiting thread
  }
  
  bool empty() {
    std::unique_lock<std::mutex> mlock(mutex_);
    return queue_.empty();
  }
  
  int size() {
    std::unique_lock<std::mutex> mlock(mutex_);
    int size = queue_.size();
    mlock.unlock();
    return size;
  }
  
  bool wait_with_timeout(int timeout_ms) {
    std::unique_lock<std::mutex> mlock(mutex_);
    if (queue_.empty()) {
      return cond_.wait_for(mlock, std::chrono::milliseconds(timeout_ms)) == std::cv_status::no_timeout;
    }
    return true;
  }
  
  void clear_all() {
    std::unique_lock<std::mutex> mlock(mutex_);
    queue_.clear();
  }
  
private:
  std::deque<T> queue_;
  std::mutex mutex_;
  std::condition_variable cond_;
};

#endif // SHARED_QUEUE_H
