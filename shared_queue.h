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
  template <typename T, std::size_t Capacity>
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
      std::atomic<std::size_t> head;       // Consumer position
      std::atomic<std::size_t> tail;       // Producer position
      std::atomic<std::size_t> count{ 0 }; // Item counter
      std::size_t capacity{ 0 };           // Capacity of the buffer
    };

    Shared_Control_Block* control_block{ nullptr }; // Shared control block
    Buffer_Slot* buffer{ nullptr };                 // Circular buffer slots

    std::size_t wrap(std::size_t index) const
    {
      return index % Capacity; // Use Capacity as the capacity
    }

  public:
    constexpr static std::size_t required_size()
    {
      return sizeof(Shared_Control_Block) + (sizeof(Buffer_Slot) * Capacity);
    }

    // Check if the buffer is empty
    bool is_empty() const
    {
      return (this->control_block->count.load(std::memory_order_acquire) == 0);
    }

    // Count of items in the buffer
    std::size_t size() const
    {
      return this->control_block->count.load(std::memory_order_acquire);
    }

    // Enqueue a new item
    bool enqueue(const T& item, bool important = false)
    {
      std::size_t current_count = this->control_block->count.load(std::memory_order_acquire);

      if (current_count == Capacity)
      {
        // Queue is full; search for a non-important slot to overwrite
        std::size_t search_pos = this->control_block->head.load(std::memory_order_relaxed);
        bool found_non_important = false;

        for (std::size_t i = 0; i < Capacity; ++i)
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
          this->control_block->head.store(
            wrap(this->control_block->head.load(std::memory_order_relaxed) + 1),
            std::memory_order_release
          );

          // If we forced out an item, the count *doesn't change*
          // because we are about to place a new one. So effectively
          // we do a 'dequeue + enqueue' on the same operation.
          // But let's forcibly decrement then increment to be consistent.
          this->control_block->count.fetch_sub(1, std::memory_order_acq_rel);
        }
        else
        {
          // No non-important slots found; overwrite the oldest important slot
          this->control_block->head.store(
            wrap(this->control_block->head.load(std::memory_order_relaxed) + 1),
            std::memory_order_release
          );

          this->control_block->count.fetch_sub(1, std::memory_order_acq_rel);
        }
      }

      std::size_t pos = this->control_block->tail.load(std::memory_order_relaxed);
      std::size_t next_pos = wrap(pos + 1);

      // Write data to the current tail
      this->buffer[wrap(pos)].data = item;
      this->buffer[wrap(pos)].is_important.store(important, std::memory_order_release);
      this->control_block->tail.store(next_pos, std::memory_order_release);

      this->control_block->count.fetch_add(1, std::memory_order_acq_rel);

      return true;
    }

    // Dequeue an item
    bool dequeue(T* item, bool* important = nullptr)
    {
      std::size_t current_count = this->control_block->count.load(std::memory_order_acquire);

      if (current_count == 0)
      {
        // Queue is empty
        return false;
      }

      // We definitely have an item to consume
      std::size_t pos = this->control_block->head.load(std::memory_order_relaxed);

      *item = this->buffer[wrap(pos)].data;

      if (important != nullptr)
      {
        *important = this->buffer[wrap(pos)].is_important.load(std::memory_order_relaxed);
      }

      this->control_block->head.store(wrap(pos + 1), std::memory_order_release);
      this->control_block->count.fetch_sub(1, std::memory_order_acq_rel);

      return true;
    }

    // Create queue. Assume that memory pointed to by shared_memory is large enough.
    // To allocate enough memory use; Shared_Queue<T, Capacity>::required_size().
    bool create(void* shared_memory)
    {
      // We can still perform alignment if desired
      std::size_t alignment = alignof(std::max_align_t);
      std::size_t aligned_control_size = (sizeof(Shared_Control_Block) + alignment - 1) & ~(alignment - 1);

      // The size of the final Shared_Queue<T, Capacity> must be:
      //   sizeof(Shared_Control_Block) + (sizeof(Buffer_Slot) * Capacity)

      this->control_block = static_cast<Shared_Control_Block*>(shared_memory);
      this->buffer = reinterpret_cast<Buffer_Slot*>(
        static_cast<char*>(shared_memory) + aligned_control_size
        );

      if (this->control_block->capacity != Capacity)
      {
        // Initialize control block and buffer
        new (this->control_block) Shared_Control_Block();
        this->control_block->head.store(0, std::memory_order_relaxed);
        this->control_block->tail.store(0, std::memory_order_relaxed);
        this->control_block->capacity = Capacity;

        for (std::size_t i = 0; i < Capacity; ++i)
        {
          new (&this->buffer[i]) Buffer_Slot();
          this->buffer[i].is_important.store(false, std::memory_order_relaxed);
        }
      }

      return true;
    }

    explicit Shared_Queue(void* shared_memory)
    {
      // Now we just call create without the size
      this->create(shared_memory);
    }

    // Default constructor
    Shared_Queue() = default;
  };
} // namespace sq

#endif // MPMC_SHARED_QUEUE_H
