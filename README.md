# mpmc-shared-queue
Lock-free (Multi-Producer, Multi-Consumer) queue that works in shared memory, for C++

## Usage examples (Windows)

### Producer
```c++
#include "shared_queue.h"

#include <iostream>
#include <Windows.h>

int main()
{
  constexpr size_t SHARED_MEMORY_SIZE = 1024 * 1024; // 1 MB

  // Create shared memory
  HANDLE map_file = CreateFileMappingA(
    INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, SHARED_MEMORY_SIZE, "SharedQueueWindowsExample");

  if (!map_file)
  {
    std::cerr << "Failed to create shared memory: " << GetLastError() << std::endl;
    return 1;
  }

  void* shared_memory = MapViewOfFile(map_file, FILE_MAP_ALL_ACCESS, 0, 0, SHARED_MEMORY_SIZE);

  if (!shared_memory)
  {
    std::cerr << "Failed to map shared memory: " << GetLastError() << std::endl;
    CloseHandle(map_file);
    return 1;
  }

  // Create or get the shared queue depending on if it already exists or not
  sq::Shared_Queue<int> shared_queue(shared_memory, SHARED_MEMORY_SIZE);

  if (!shared_queue.is_created())
  {
    std::cerr << "Failed to create or get shared queue" << std::endl;
    UnmapViewOfFile(shared_memory);
    CloseHandle(map_file);
    return 1;
  }

  // Producer code
  for (int i = 1; i <= 10; ++i)
  {
    while (!shared_queue.enqueue(i, i % 2 == 0)) // Mark even numbers as important
    {
      Sleep(1); // Wait if enqueue fails
    }

    std::cout << "Producer enqueued: " << i << (i % 2 == 0 ? " (Important)" : "") << std::endl;
  }

  // Keep the process alive to allow the second process to access the queue
  std::cout << "Press Enter to exit...";
  std::cin.get();

  // Clean up
  UnmapViewOfFile(shared_memory);
  CloseHandle(map_file);

  return 0;
}
```

### Consumer
```c++
#include "shared_queue.h"

#include <iostream>
#include <Windows.h>

int main()
{
  constexpr size_t SHARED_MEMORY_SIZE = 1024 * 1024; // 1 MB

  // Open existing shared memory
  HANDLE map_file = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, "SharedQueueWindowsExample");

  if (!map_file)
  {
    std::cerr << "Failed to open shared memory: " << GetLastError() << std::endl;
    return 1;
  }

  void* shared_memory = MapViewOfFile(map_file, FILE_MAP_ALL_ACCESS, 0, 0, SHARED_MEMORY_SIZE);
  if (!shared_memory)
  {
    std::cerr << "Failed to map shared memory: " << GetLastError() << std::endl;
    CloseHandle(map_file);
    return 1;
  }

  // Create or get the shared queue depending on if it already exists or not
  sq::Shared_Queue<int> shared_queue(shared_memory, SHARED_MEMORY_SIZE);

  if (!shared_queue.is_created())
  {
    std::cerr << "Failed to create or get shared queue" << std::endl;
    UnmapViewOfFile(shared_memory);
    CloseHandle(map_file);
    return 1;
  }

  // Consumer code
  int value;
  bool important;

  while (true)
  {
    if (shared_queue.dequeue(&value, &important))
    {
      std::cout << "Consumer dequeued: " << value;

      if (important)
      {
        std::cout << " (Important)";
      }

      std::cout << std::endl;
    }
    else if (shared_queue.is_empty())
    {
      // Exit when the queue is empty
      break;
    }
    else
    {
      Sleep(1); // Wait if dequeue fails
    }
  }

  // Clean up
  UnmapViewOfFile(shared_memory);
  CloseHandle(map_file);

  return 0;
}
```

## Notes
- Has not been tested on Linux
- Performance has not been tested
- Not tested extensively in general
