/*
 * Copyright (c) 2014, Hewlett-Packard Development Company, LP.
 * The license and distribution terms for this file are placed in LICENSE.txt.
 */
#include <foedus/engine.hpp>
#include <foedus/memory/engine_memory.hpp>
#include <foedus/thread/thread_pimpl.hpp>
#include <foedus/thread/thread_pool.hpp>
#include <foedus/thread/thread_pool_pimpl.hpp>
#include <glog/logging.h>
#include <numa.h>
#include <cassert>
#include <atomic>
#include <future>
#include <mutex>
#include <thread>
namespace foedus {
namespace thread {
ErrorStack ThreadPimpl::initialize_once() {
    core_memory_ = engine_->get_memory_manager().get_core_memory(id_);
    raw_thread_ = new std::thread(&ThreadPimpl::handle_tasks, this);
    if (raw_thread_ == nullptr) {
        return ERROR_STACK(ERROR_CODE_OUTOFMEMORY);
    }
    return RET_OK;
}
ErrorStack ThreadPimpl::uninitialize_once() {
    if (raw_thread_) {
        impersonated_task_.set_value(nullptr);  // this signals that the thread should exit.
        raw_thread_->join();
        delete raw_thread_;
        raw_thread_ = nullptr;
    }
    core_memory_ = nullptr;
    return RET_OK;
}

void ThreadPimpl::handle_tasks() {
    int numa_node = static_cast<int>(decompose_numa_node(id_));
    LOG(INFO) << "Thread-" << id_ << " started running on NUMA node: " << numa_node;
    ::numa_run_on_node(numa_node);
    while (true) {
        LOG(INFO) << "Thread-" << id_ << " waiting for a task...";
        std::future<ImpersonateTask*> task_future = impersonated_task_.get_future();
        ImpersonateTask* functor = task_future.get();
        impersonated_task_ = std::promise<ImpersonateTask*>();  // reset the promise/future pair
        if (functor) {
            assert(impersonated_);
            LOG(INFO) << "Thread-" << id_ << " retrieved a task";
            ErrorStack result = functor->run(holder_);
            impersonated_task_result_.set_value(result);
            impersonated_ = false;
            LOG(INFO) << "Thread-" << id_ << " finished a task. result =" << result;
        } else {
            // NULL functor is the signal to terminate
            break;
        }
    }
    exitted_ = true;
    std::atomic_thread_fence(std::memory_order_release);
    LOG(INFO) << "Thread-" << id_ << " exits";
}

bool ThreadPimpl::try_impersonate(ImpersonateSession *session) {
    bool cas_tmp = false;
    if (!impersonated_ && std::atomic_compare_exchange_strong(&impersonated_, &cas_tmp, true)) {
        // successfully acquired. set a new promise for this session.
        LOG(INFO) << "Impersonation succeeded for Thread-" << id_ << ". Setting a task..";
        impersonated_task_result_ = std::promise<ErrorStack>();  // this is a promise for ME
        session->thread_ = holder_;
        *reinterpret_cast< std::shared_future< ErrorStack >* >(session->result_future_)
            = impersonated_task_result_.get_future().share();
        impersonated_task_.set_value(session->task_);
        return true;
    } else {
        // no, someone else took it.
        DLOG(INFO) << "Someone already took Thread-" << id_ << ".";
        return false;
    }
}

void ThreadPimpl::activate_xct() {
    assert(!current_xct_.is_active());
    current_xct_.activate(holder_);
}

void ThreadPimpl::deactivate_xct() {
    assert(current_xct_.is_active());
    current_xct_.deactivate();
}


}  // namespace thread
}  // namespace foedus