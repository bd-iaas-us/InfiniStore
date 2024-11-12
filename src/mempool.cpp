#include "mempool.h"

#include <assert.h>
#include <sys/mman.h>

#include <cstring>
#include <iostream>
#include <stdexcept>

#include "log.h"
#include "utils.h"

MemoryPool::MemoryPool(size_t pool_size, size_t block_size, struct ibv_pd* pd)
    : pool_(nullptr), pool_size_(pool_size), block_size_(block_size), pd_(pd), mr_(nullptr) {
    // 计算总的内存块数量
    total_blocks_ = pool_size_ / block_size_;
    assert(pool_size % block_size == 0);

    INFO(
        "Memory pool size: {} bytes, block size: {} bytes, total blocks: {}, "
        "it may take a while",
        pool_size_, block_size_, total_blocks_);
    // CHECK_CUDA(cudaMallocHost(&pool_, pool_size_));

    if (posix_memalign(&pool_, 4096, pool_size_) != 0) {
        ERROR("Failed to allocate memory pool");
        exit(EXIT_FAILURE);
    }
    CHECK_CUDA(cudaHostRegister(pool_, pool_size_, cudaHostRegisterDefault));

    // 注册内存区域
    mr_ = ibv_reg_mr(pd_, pool_, pool_size_,
                     IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ);
    if (!mr_) {
        ERROR("Failed to register MR");
        exit(EXIT_FAILURE);
    }
    INFO("Memory pool allocated at {}", pool_);

    bitmap_.resize(total_blocks_, 0);
}

MemoryPool::~MemoryPool() {
    if (mr_) {
        ibv_dereg_mr(mr_);
    }
    if (pool_) {
        cudaFreeHost(pool_);
    }
}

void* MemoryPool::allocate(size_t size) {
    size_t required_blocks = size / block_size_;
    if (size % block_size_ != 0) {
        required_blocks += 1;  // round up
    }

    if (required_blocks > total_blocks_) {
        return nullptr;
    }

    size_t bit_per_word = 64;

    for (size_t word_index = 0; word_index < bitmap_.size(); ++word_index) {
        uint64_t word = bitmap_[word_index];
        if (word == 0xFFFFFFFFFFFFFFFFULL) {
            continue;
        }

        for (size_t bit_index = 0; bit_index < bit_per_word; ++bit_index) {
            size_t start_block = word_index * bit_per_word + bit_index;

            if (start_block + required_blocks > total_blocks_) {
                return nullptr;
            }

            bool found = true;
            for (size_t i = 0; i < required_blocks; ++i) {
                size_t idx = (start_block + i) / bit_per_word;
                size_t bit = (start_block + i) % bit_per_word;
                if (bitmap_[idx] & (1ULL << bit)) {
                    found = false;
                    bit_index += i;  // skip all the blocks we already checked
                    break;
                }
            }

            if (found) {
                for (size_t i = 0; i < required_blocks; ++i) {
                    size_t idx = (start_block + i) / bit_per_word;
                    size_t bit = (start_block + i) % bit_per_word;
                    bitmap_[idx] |= (1ULL << bit);
                }
                void* addr = static_cast<char*>(pool_) + start_block * block_size_;
                return addr;
            }
        }
    }
    return nullptr;
}

void MemoryPool::deallocate(void* ptr, size_t size) {
    size_t blocks_to_free = size / block_size_;
    if (size % block_size_ != 0) {
        blocks_to_free += 1;  // round up
    }

    uintptr_t offset = static_cast<char*>(ptr) - static_cast<char*>(pool_);
    if (offset % block_size_ != 0) {
        std::cerr << "Invalid pointer deallocation attempt: not aligned" << std::endl;
        return;
    }

    size_t start_block = offset / block_size_;
    if (start_block >= total_blocks_) {
        ERROR("Pointer out of range");
        return;
    }

    if (start_block + blocks_to_free > total_blocks_) {
        ERROR("Deallocation size out of range");
        return;
    }

    for (size_t i = start_block; i < start_block + blocks_to_free; ++i) {
        size_t idx = i / 64;
        size_t bit = i % 64;
        if (bitmap_[idx] & (1ULL << bit)) {
            bitmap_[idx] &= ~(1ULL << bit);
        }
        else {
            ERROR("Double free detected at block index {}", i);
        }
    }
}

void* MM::allocate(size_t size, int* pool_idx) {
    // first fit. TODO: binaray search
    for (int i = 0; i < mempools_.size(); ++i) {
        void* ptr = mempools_[i]->allocate(size);
        if (ptr) {
            *pool_idx = i;
            return ptr;
        }
    }
    return nullptr;
}
void MM::deallocate(void* ptr, size_t size, int pool_idx) {
    mempools_[pool_idx]->deallocate(ptr, size);
}
