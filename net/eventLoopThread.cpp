#include <muduo/net/eventLoopThread.h>
#include <muduo/net/eventLoop.h>
#include <muduo/base/logging.h>

namespace muduo {
namespace net {

eventLoop_thread::eventLoop_thread(const initializeCallback_t& func, const string& thread_name)
    : thread_([this]() { this->threadFunc(); }, thread_name)
    , loop_(nullptr)
    , exiting_(false)
    , mutex_()
    , cv_(mutex_)
    , initFunc_(func) {}

eventLoop_thread::~eventLoop_thread() {
    exiting_ = true;
    // access variable loop_ not 100% race-free
    // but when eventLoop_thread destructs, usually programming is exiting anyway.
    {
        mutexLock_guard locker(mutex_);
        if (loop_ != nullptr) {
            loop_->quit();
            thread_.join();
        }
    }
}

event_loop* eventLoop_thread::start_loop() {
    assert(!thread_.started());
    thread_.start();

    event_loop* loop = nullptr;
    {
        mutexLock_guard locker(mutex_);
        while (loop_ == nullptr) {
            cv_.wait();
        }
        loop = loop_;
    }
    return loop;
}

void eventLoop_thread::threadFunc() {
    event_loop loop;
    if (initFunc_) { initFunc_(&loop); }

    {
        mutexLock_guard locker(mutex_);
        if (exiting_) {
            LOG_ERROR << "eventLoop-thread is destructed";
            cv_.notify_one();   // in this case: func "eventLoop_thread::start_loop" return nullptr 
            return;
        }

        loop_ = &loop;
        cv_.notify_one();
    }

    loop.loop();    // start the loop

    return;
}

} // namespace net 
} // namespace muduo 