#ifndef TORRENT_THREAD_SAFE_QUEUE_HPP
#define TORRENT_THREAD_SAFE_QUEUE_HPP

#include <mutex>
#include <queue>

namespace torrent {
/*
 * A simple thread safe wrapper around std::queue.
 * */
template<typename Type>
class ThreadSafeQueue {
  public:
    bool empty() {
        std::scoped_lock<std::mutex> lock {mutex};
        return queue.empty();
    }

    void push(Type&& value) {
        std::scoped_lock<std::mutex> lock {mutex};
        queue.push(std::forward<Type>(value));
    }

    Type pop() {
        std::scoped_lock<std::mutex> lock {mutex};
        auto value = std::move(queue.front());
        queue.pop();
        return value;
    }

  private:
    std::mutex mutex;
    std::queue<Type> queue;
};
} // namespace torrent

#endif
