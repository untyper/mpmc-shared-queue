# mpmc-shared-queue
Shared queue that works in shared memory, for C++

## Details
- Thrad-safe
- Lock-free
- Multi-Producer and Multi-Consumer
- Works in shared memory

## Usage examples (Windows)

### Producer
```c++
#include "shared_queue.h"

#include <windows.h>
#include <iostream>

int main()
{
  const size_t shared_memory_size = 1024; // 1 KB shared memory
  const char* shared_memory_name = "SharedQueueMemory";

  // Create shared memory
  HANDLE hMapFile = CreateFileMappingA(
    INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, shared_memory_size, shared_memory_name);
  if (!hMapFile)
  {
    std::cerr << "Failed to create shared memory: " << GetLastError() << std::endl;
    return 1;
  }

  void* shared_memory = MapViewOfFile(hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, shared_memory_size);

  if (!shared_memory)
  {
    std::cerr << "Failed to map shared memory: " << GetLastError() << std::endl;
    CloseHandle(hMapFile);
    return 1;
  }

  // Initialize the shared queue
  sq::Shared_Queue<int> queue(shared_memory, shared_memory_size, 10);

  // Producer loop
  int count = 0;

  while (true)
  {
    bool important = (count % 5 == 0); // Mark every 5th item as important

    if (queue.enqueue(count, important))
    {
      std::cout << "[Producer] Enqueued: " << count << (important ? " (Important)" : "") << std::endl;
    }
    else
    {
      std::cerr << "[Producer] Queue is full.\n";
    }

    count++;
    Sleep(500); // Sleep for 500ms
  }

  // Cleanup
  UnmapViewOfFile(shared_memory);
  CloseHandle(hMapFile);

  return 0;
}
```

### Consumer
```c++
#include "shared_queue.h"

#include <windows.h>
#include <iostream>

int main()
{
  const size_t shared_memory_size = 1024; // 1 KB shared memory
  const char* shared_memory_name = "SharedQueueMemory";

  // Open shared memory
  HANDLE hMapFile = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, shared_memory_name);

  if (!hMapFile)
  {
    std::cerr << "Failed to open shared memory: " << GetLastError() << std::endl;
    return 1;
  }

  void* shared_memory = MapViewOfFile(hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, shared_memory_size);

  if (!shared_memory)
  {
    std::cerr << "Failed to map shared memory: " << GetLastError() << std::endl;
    CloseHandle(hMapFile);
    return 1;
  }

  // Attach to the shared queue
  sq::Shared_Queue<int> queue(shared_memory, shared_memory_size);

  // Consumer loop
  while (true)
  {
    int value;
    bool important;

    if (queue.dequeue(&value, &important))
    {
      std::cout << "[Consumer] Dequeued: " << value << (important ? " (Important)" : "") << std::endl;
    }
    else
    {
      std::cerr << "[Consumer] Queue is empty.\n";
    }

    Sleep(1000); // Sleep for 1 second
  }

  // Cleanup
  UnmapViewOfFile(shared_memory);
  CloseHandle(hMapFile);

  return 0;
}
```

## Notes
- Has not been tested on Linux
- Performance has not been tested
- Not tested extensively in general
