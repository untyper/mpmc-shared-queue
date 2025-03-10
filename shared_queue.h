// Copyright (c) [2024] [Jovan J. E. Odassius]
//
// License: MIT (See the LICENSE file in the root directory)
// Github: https://github.com/untyper/mpmc-shared-queue

#ifndef MPMC_SHARED_QUEUE_H
#define MPMC_SHARED_QUEUE_H

#include <atomic>      // For std::atomic
#include <cstddef>     // For std::size_t
//#include <stdexcept> // For std::runtime_error
#include <new>         // For placement new
#include <memory>      // Optional, if smart pointers are used
//#include <iostream>  // For debug output (optional, can be removed)

namespace sq
{
  template <typename T>
  class Shared_Queue
  {
  private:
    struct alignas(64) Buffer_Slot
    {
      T data;
      std::atomic<bool> is_important;
      Buffer_Slot() : is_important(false) {}
    };

    struct Shared_Control_Block
    {
      std::atomic<std::size_t> head; // Consumer position
      std::atomic<std::size_t> tail; // Producer position
      std::size_t capacity{ 0 };     // Capacity of the buffer
    };

    Shared_Control_Block* control_block{ nullptr }; // Shared control block
    Buffer_Slot* buffer{ nullptr };                 // Circular buffer slots

    std::size_t wrap(std::size_t index) const
    {
      return index % this->control_block->capacity;
    }

  public:
    // Check if the buffer is empty
    bool is_empty() const
    {
      return this->control_block->head.load(std::memory_order_acquire) == this->control_block->tail.load(std::memory_order_acquire);
    }

    // Approximate size of the buffer
    std::size_t size_approx() const
    {
      std::size_t current_head = this->control_block->head.load(std::memory_order_acquire);
      std::size_t current_tail = this->control_block->tail.load(std::memory_order_acquire);
      return (current_tail >= current_head) ? (current_tail - current_head)
        : (this->control_block->capacity - (current_head - current_tail));
    }

    // Enqueue a new item
    bool enqueue(const T& item, bool important = false)
    {
      std::size_t pos = this->control_block->tail.load(std::memory_order_relaxed);
      std::size_t next_pos = wrap(pos + 1);

      if (next_pos == this->control_block->head.load(std::memory_order_acquire))
      {
        // Queue is full; search for a non-important slot to overwrite
        std::size_t search_pos = this->control_block->head.load(std::memory_order_relaxed);
        bool found_non_important = false;

        for (std::size_t i = 0; i < this->control_block->capacity; ++i)
        {
          Buffer_Slot& candidate_slot = this->buffer[wrap(search_pos)];

          if (!candidate_slot.is_important.load(std::memory_order_acquire))
          {
            found_non_important = true;
            break;
          }

          search_pos = wrap(search_pos + 1);
        }

        if (found_non_important)
        {
          // Move head to free up the non-important slot
          this->control_block->head.store(wrap(this->control_block->head.load(std::memory_order_relaxed) + 1), std::memory_order_release);
        }
        else
        {
          // No non-important slots found; overwrite the oldest important slot
          this->control_block->head.store(wrap(this->control_block->head.load(std::memory_order_relaxed) + 1), std::memory_order_release);
        }
      }

      // Write data to the current tail
      this->buffer[wrap(pos)].data = item;
      this->buffer[wrap(pos)].is_important.store(important, std::memory_order_release);
      this->control_block->tail.store(next_pos, std::memory_order_release);

      return true;
    }

    // Dequeue an item
    bool dequeue(T* item, bool* important)
    {
      std::size_t pos = this->control_block->head.load(std::memory_order_relaxed);

      if (pos == this->control_block->tail.load(std::memory_order_acquire))
      {
        // Queue is empty
        return false;
      }

      *item = this->buffer[wrap(pos)].data;
      *important = this->buffer[wrap(pos)].is_important.load(std::memory_order_relaxed);
      this->control_block->head.store(wrap(pos + 1), std::memory_order_release);

      return true;
    }

    bool create(void* shared_memory, std::size_t shared_memory_size, std::size_t requested_capacity = 0)
    {
      std::size_t alignment = alignof(std::max_align_t);
      std::size_t aligned_control_size = (sizeof(Shared_Control_Block) + alignment - 1) & ~(alignment - 1);

      if (shared_memory_size < aligned_control_size)
      {
        //throw std::runtime_error("Insufficient shared memory size for control block.");
        return false;
      }

      std::size_t buffer_space = shared_memory_size - aligned_control_size;
      std::size_t capacity = requested_capacity ? requested_capacity : (buffer_space / sizeof(Buffer_Slot));

      if (capacity == 0)
      {
        //throw std::runtime_error("Insufficient shared memory size for buffer slots.");
        return false;
      }

      this->control_block = static_cast<Shared_Control_Block*>(shared_memory);
      this->buffer = reinterpret_cast<Buffer_Slot*>(static_cast<char*>(shared_memory) + aligned_control_size);

      if (this->control_block->capacity != capacity)
      {
        // Initialize control block and buffer
        new (this->control_block) Shared_Control_Block();
        this->control_block->head.store(0, std::memory_order_relaxed);
        this->control_block->tail.store(0, std::memory_order_relaxed);
        this->control_block->capacity = capacity;

        for (std::size_t i = 0; i < capacity; ++i)
        {
          new (&this->buffer[i]) Buffer_Slot();
          this->buffer[i].is_important.store(false, std::memory_order_relaxed);
        }
      }

      return true;
    }

    explicit Shared_Queue(void* shared_memory, std::size_t shared_memory_size, std::size_t requested_capacity = 0)
    {
      this->create(shared_memory, shared_memory_size, requested_capacity);
    }

    // Default constructor
    Shared_Queue() = default;
  };
} // namespace sq

#endif MPMC_SHARED_QUEUE_H
