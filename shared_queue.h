// Copyright (c) [2024] [Jovan J. E. Odassius]
//
// License: MIT (See the LICENSE file in the root directory)
// Github: https://github.com/untyper/mpmc-shared-queue

#ifndef MPMC_SHARED_QUEUE_H
#define MPMC_SHARED_QUEUE_H

#include <atomic>
#include <thread>
#include <memory>
#include <cassert>
#include <cstring>
//#include <iostream>

namespace sq
{

struct Shared_Control_Block
{
  // Synchronization flag for initialization
  std::atomic<int> initialization_flag; // 0 = uninitialized, 1 = initializing, 2 = initialized

  // Queue state
  std::atomic<size_t> head;
  std::atomic<size_t> tail;
  size_t capacity;
  // Buffer slots will be allocated after this structure

  Shared_Control_Block(size_t cap)
    : initialization_flag(0), head(0), tail(0), capacity(cap)
  {
  }
};

template <typename T>
class Shared_Queue
{
private:
  struct alignas(64) Buffer_Slot
  {
    std::atomic<size_t> sequence;
    T data;
    std::atomic<bool> important;

    Buffer_Slot() : sequence(0), important(false) {}
  };

  Shared_Control_Block* control_block = nullptr;
  Buffer_Slot* buffer = nullptr;
  bool created = false;

  size_t wrap(size_t index) const
  {
    return index % this->control_block->capacity;
  }

public:
  bool is_created() const
  {
    return this->created;
  }

  // Check if the queue is empty
  bool is_empty() const
  {
    size_t current_head = this->control_block->head.load(std::memory_order_acquire);
    size_t current_tail = this->control_block->tail.load(std::memory_order_acquire);
    return current_head == current_tail;
  }

  // Approximate size of the queue
  size_t size_approx() const
  {
    size_t current_head = this->control_block->head.load(std::memory_order_acquire);
    size_t current_tail = this->control_block->tail.load(std::memory_order_acquire);
    return current_tail - current_head;
  }

  // Enqueue a new item
  bool enqueue(const T& value, bool important = false)
  {
    size_t pos = this->control_block->tail.load(std::memory_order_relaxed);

    while (true)
    {
      Buffer_Slot& slot = this->buffer[wrap(pos)];
      size_t seq = slot.sequence.load(std::memory_order_acquire);
      intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos);

      if (diff == 0)
      {
        // Slot is ready for writing
        if (this->control_block->tail.compare_exchange_weak(pos, pos + 1, std::memory_order_acq_rel))
        {
          slot.data = value;
          slot.important.store(important, std::memory_order_relaxed);
          slot.sequence.store(pos + 1, std::memory_order_release);
          return true;
        }
      }
      else if (diff < 0)
      {
        // Buffer is full
        if (slot.important.load(std::memory_order_acquire))
        {
          // Cannot overwrite important data
          //std::cerr << "Cannot overwrite important data!" << std::endl;
          return false;
        }
        else
        {
          // Overwrite regular data
          if (this->control_block->tail.compare_exchange_weak(pos, pos + 1, std::memory_order_acq_rel))
          {
            slot.data = value;
            slot.important.store(important, std::memory_order_relaxed);
            slot.sequence.store(pos + 1, std::memory_order_release);
            //std::cerr << "Overwriting regular data." << std::endl;
            return true;
          }
        }
      }
      else
      {
        // Another producer has moved the tail, update pos and retry
        pos = this->control_block->tail.load(std::memory_order_relaxed);
      }
    }
  }

  // Dequeue an item
  bool dequeue(T* value, bool* important)
  {
    size_t pos = this->control_block->head.load(std::memory_order_relaxed);

    while (true)
    {
      Buffer_Slot& slot = this->buffer[wrap(pos)];
      size_t seq = slot.sequence.load(std::memory_order_acquire);
      intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos + 1);

      if (diff == 0)
      {
        // Slot is ready for reading
        if (this->control_block->head.compare_exchange_weak(pos, pos + 1, std::memory_order_acq_rel))
        {
          *value = slot.data;
          *important = slot.important.load(std::memory_order_relaxed);
          slot.important.store(false, std::memory_order_relaxed);
          slot.sequence.store(pos + this->control_block->capacity, std::memory_order_release);
          return true;
        }
      }
      else if (diff < 0)
      {
        // Buffer is empty
        return false;
      }
      else
      {
        // Another consumer has moved the head, update pos and retry
        pos = this->control_block->head.load(std::memory_order_relaxed);
      }
    }
  }

  // NOTE:
  //  Size of created queue will be calculated dynamically based on
  //  shared_memory_size, control_block and alignment requirements
  bool create(void* shared_memory_address, size_t shared_memory_size)
  {
    // Cast the void* to char* for pointer arithmetic
    char* shared_memory_base = static_cast<char*>(shared_memory_address);

    // Ensure alignment
    size_t alignment = alignof(std::max_align_t);
    size_t aligned_control_size = (sizeof(Shared_Control_Block) + alignment - 1) & ~(alignment - 1);

    // Check if shared memory size is sufficient for control block
    if (shared_memory_size < aligned_control_size)
    {
      //throw std::runtime_error("Shared memory size is insufficient for control block.");
      return false; // Error
    }

    // Calculate available space for buffer slots
    size_t buffer_space = shared_memory_size - aligned_control_size;

    // Calculate maximum queue capacity
    size_t queue_capacity = buffer_space / sizeof(Buffer_Slot);

    // Check if capacity is at least 1
    if (queue_capacity == 0)
    {
      //throw std::runtime_error("Shared memory size is insufficient for any buffer slots.");
      return false; // Error
    }

    // Access the shared data
    this->control_block = reinterpret_cast<Shared_Control_Block*>(shared_memory_base);

    // Attempt to initialize the queue
    int expected = 0;

    if (this->control_block->initialization_flag.compare_exchange_strong(expected, 1, std::memory_order_acq_rel))
    {
      // We are the initializing process

      // Initialize shared data
      new (this->control_block) Shared_Control_Block(queue_capacity);

      // Allocate and initialize buffer slots after shared data
      this->buffer = reinterpret_cast<Buffer_Slot*>(shared_memory_base + aligned_control_size);

      for (size_t i = 0; i < queue_capacity; ++i)
      {
        new (&(this->buffer)[i]) Buffer_Slot();
        this->buffer[i].sequence.store(i, std::memory_order_relaxed);
        this->buffer[i].important.store(false, std::memory_order_relaxed);
      }

      // Mark initialization as complete
      this->control_block->initialization_flag.store(2, std::memory_order_release);
    }
    else
    {
      // Wait for initialization to complete
      while (this->control_block->initialization_flag.load(std::memory_order_acquire) != 2)
      {
        std::this_thread::sleep_for(std::chrono::milliseconds(1)); // Sleep to avoid busy waiting
      }

      // Initialization is complete, access the shared data
      this->control_block = reinterpret_cast<Shared_Control_Block*>(shared_memory_base);
      this->buffer = reinterpret_cast<Buffer_Slot*>(shared_memory_base + aligned_control_size);
      queue_capacity = this->control_block->capacity;
    }

    return (this->created = true); // Success
  }

  // Copy constructor
  Shared_Queue(const Shared_Queue& other) :
    control_block(other.control_block),
    buffer(other.buffer),
    created(other.created)
  {
  }

  // Copy assignment operator
  Shared_Queue& operator=(const Shared_Queue& other)
  {
    if (this != &other)
    {
      this->control_block = other.control_block;
      this->buffer = other.buffer;
      this->created = other.created;
    }

    return *this;
  }

  // Constructor
  Shared_Queue(void* shared_memory_address, size_t shared_memory_size)
  {
    this->create(shared_memory_address, shared_memory_size);
  }

  Shared_Queue() {}
};

} // namespace sq

#endif // MPMC_SHARED_QUEUE_H
