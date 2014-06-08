/*
 * Copyright (c) 2014, Hewlett-Packard Development Company, LP.
 * The license and distribution terms for this file are placed in LICENSE.txt.
 */
#include <foedus/engine.hpp>
#include <foedus/engine_options.hpp>
#include <foedus/error_stack_batch.hpp>
#include <foedus/debugging/debugging_supports.hpp>
#include <foedus/memory/engine_memory.hpp>
#include <foedus/memory/numa_node_memory.hpp>
#include <foedus/storage/storage_id.hpp>
#include <foedus/thread/thread_id.hpp>
#include <glog/logging.h>
#include <numa.h>
namespace foedus {
namespace memory {
ErrorStack EngineMemory::initialize_once() {
    LOG(INFO) << "Initializing EngineMemory..";
    if (!engine_->get_debug().is_initialized()) {
        return ERROR_STACK(ERROR_CODE_DEPEDENT_MODULE_UNAVAILABLE_INIT);
    } else if (::numa_available() < 0) {
        return ERROR_STACK(ERROR_CODE_MEMORY_NUMA_UNAVAILABLE);
    }
    ASSERT_ND(node_memories_.empty());
    const EngineOptions& options = engine_->get_options();

    // Can we at least start up?
    uint64_t total_threads = options.thread_.group_count_ * options.thread_.thread_count_per_group_;
    uint64_t minimal_page_pool = total_threads * options.memory_.private_page_pool_initial_grab_
        * storage::PAGE_SIZE;
    if ((static_cast<uint64_t>(options.memory_.page_pool_size_mb_per_node_)
            * options.thread_.group_count_ << 20) < minimal_page_pool) {
        return ERROR_STACK(ERROR_CODE_MEMORY_PAGE_POOL_TOO_SMALL);
    }

    thread::ThreadGroupId numa_nodes = options.thread_.group_count_;
    PagePoolOffset page_offset_begin = 0;
    PagePoolOffset page_offset_end = 0;
    GlobalPageResolver::Base bases[256];
    for (thread::ThreadGroupId node = 0; node < numa_nodes; ++node) {
        ScopedNumaPreferred numa_scope(node);
        NumaNodeMemory* node_memory = new NumaNodeMemory(engine_, node);
        node_memories_.push_back(node_memory);
        CHECK_ERROR(node_memory->initialize());
        bases[node] = node_memory->get_page_pool().get_resolver().base_;
        if (node == 0) {
            page_offset_begin = node_memory->get_page_pool().get_resolver().begin_;
            page_offset_end = node_memory->get_page_pool().get_resolver().end_;
        } else {
            ASSERT_ND(page_offset_begin == node_memory->get_page_pool().get_resolver().begin_);
            ASSERT_ND(page_offset_end == node_memory->get_page_pool().get_resolver().end_);
        }
    }
    global_page_resolver_ = GlobalPageResolver(
        bases, numa_nodes, page_offset_begin, page_offset_end);
    return RET_OK;
}

ErrorStack EngineMemory::uninitialize_once() {
    LOG(INFO) << "Uninitializing EngineMemory..";
    ErrorStackBatch batch;
    if (!engine_->get_debug().is_initialized()) {
        batch.emprace_back(ERROR_STACK(ERROR_CODE_DEPEDENT_MODULE_UNAVAILABLE_UNINIT));
    }

    batch.uninitialize_and_delete_all(&node_memories_);
    return SUMMARIZE_ERROR_BATCH(batch);
}

NumaCoreMemory* EngineMemory::get_core_memory(thread::ThreadId id) const {
    thread::ThreadGroupId node = thread::decompose_numa_node(id);
    NumaNodeMemory* node_memory = get_node_memory(node);
    ASSERT_ND(node_memory);
    return node_memory->get_core_memory(id);
}


}  // namespace memory
}  // namespace foedus
