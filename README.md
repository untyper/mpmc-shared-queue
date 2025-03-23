# mpmc-shared-queue
Queue that works in shared memory, for C++

## Details
- Thrad-safe
- Lock-free
- Multi-Producer and Multi-Consumer
- Works in shared memory

## Usage examples (Windows)

### Stack (normal usage, non-shared memory)
```c++
#include <iostream>
#include <cstddef>
#include <new>            // For std::max_align_t
#include "shared_queue.h" // Include your Shared_Queue header

int main()
{
  // We'll create a Shared_Queue<int, 4>.
  using MyQueue = sq::Shared_Queue<int, 4>;

  // Calculate how many bytes we need for the queue.
  constexpr std::size_t requiredSize = MyQueue::required_size();

  // Create an aligned buffer on the stack.
  // This ensures it's at least aligned to std::max_align_t.
  alignas(std::max_align_t) char sharedMemory[requiredSize];

  // Construct the queue in that buffer.
  MyQueue queue{ sharedMemory };

  // Enqueue some items
  queue.enqueue(10, false);
  queue.enqueue(20, true);
  queue.enqueue(30, false);
  queue.enqueue(40, false);

  std::cout << "Current queue size: " << queue.size() << "\n\n";

  // Dequeue items in a loop
  std::cout << "Dequeuing items from stack-allocated Shared_Queue...\n";
  int value = 0;
  bool important = false;

  while (queue.dequeue(&value, &important))
  {
    std::cout << "Dequeued " << value
              << " (important=" << std::boolalpha << important << ")\n";
  }

  // The queue is now empty
  std::cout << "Queue is_empty() = " << std::boolalpha << queue.is_empty() << "\n";
  return 0;
}
```

### Producer
```c++
#include <windows.h>
#include <iostream>
#include <string>
#include <thread>   // For sleep_for (optional)
#include "shared_queue.h"

static constexpr LPCSTR MAPPING_NAME = "Local\\MySharedMemoryExample";
// For demonstration, we hold up to 4 integers.
using MyQueue = sq::Shared_Queue<int, 4>;

int main()
{
  // 1) Determine how many bytes of shared memory we need.
  const std::size_t requiredSize = MyQueue::required_size();

  // 2) Create or open a named file mapping backed by the system paging file.
  HANDLE hMapFile = CreateFileMappingA(
    INVALID_HANDLE_VALUE,    // use paging file
    NULL,                    // default security
    PAGE_READWRITE,          // read/write access
    0,                       // max size (high-order DWORD)
    static_cast<DWORD>(requiredSize), // max size (low-order DWORD)
    MAPPING_NAME             // name of mapping object
  );

  if (!hMapFile)
  {
    std::cerr << "Could not create/open file mapping (error "
              << GetLastError() << ").\n";
    return 1;
  }

  // 3) Map a view of the file into our process address space
  LPVOID pBuf = MapViewOfFile(
    hMapFile,            // handle to map object
    FILE_MAP_ALL_ACCESS, // read/write permission
    0,                   // offset high-order
    0,                   // offset low-order
    0                    // number of bytes to map (0 = entire mapping)
  );

  if (!pBuf)
  {
    std::cerr << "Could not map view of file (error "
              << GetLastError() << ").\n";
    CloseHandle(hMapFile);
    return 1;
  }

  // 4) Construct the queue in this shared memory block.
  MyQueue queue{ pBuf };

  // 5) Enqueue some items
  std::cout << "[Producer] Enqueueing 3 items...\n";
  queue.enqueue(100, false);
  queue.enqueue(200, true);
  queue.enqueue(300, false);

  std::cout << "[Producer] Current queue size: " << queue.size() << "\n";

  // 6) (Optional) Keep running for a while to allow the consumer to dequeue
  std::cout << "[Producer] Waiting 5 seconds before exiting...\n";
  std::this_thread::sleep_for(std::chrono::seconds(5));

  // 7) Cleanup
  UnmapViewOfFile(pBuf);
  CloseHandle(hMapFile);

  std::cout << "[Producer] Exiting.\n";
  return 0;
}
```

### Consumer
```c++
#include <windows.h>
#include <iostream>
#include <string>
#include <thread>   // For sleep_for
#include "shared_queue.h"

static constexpr LPCSTR MAPPING_NAME = "Local\\MySharedMemoryExample";
using MyQueue = sq::Shared_Queue<int, 4>;

int main()
{
  // Determine how many bytes of shared memory we need.
  const std::size_t requiredSize = MyQueue::required_size();

  // 1) Open the same named file mapping (must exist).
  HANDLE hMapFile = OpenFileMappingA(
    FILE_MAP_ALL_ACCESS, // read/write access
    FALSE,               // do not inherit
    MAPPING_NAME         // must match Producer
  );

  if (!hMapFile)
  {
    std::cerr << "[Consumer] Could not open file mapping (error "
              << GetLastError() << ").\n"
              << "Make sure producer is running first.\n";
    return 1;
  }

  // 2) Map the view
  LPVOID pBuf = MapViewOfFile(
    hMapFile,
    FILE_MAP_ALL_ACCESS,
    0,
    0,
    0
  );

  if (!pBuf)
  {
    std::cerr << "[Consumer] Could not map view of file (error "
              << GetLastError() << ").\n";
    CloseHandle(hMapFile);
    return 1;
  }

  // 3) Construct our queue. Attaches to the same control block.
  MyQueue queue{ pBuf };

  // 4) Dequeue items in a loop
  std::cout << "[Consumer] Checking queue for items...\n";
  while (true)
  {
    int value = 0;
    bool important = false;

    if (queue.dequeue(&value, &important))
    {
      std::cout << "[Consumer] Dequeued " << value
                << " (important=" << std::boolalpha << important << ")\n";
    }
    else
    {
      // No items available; wait briefly
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }
  }

  // Typically you'd unmap and close the handle if you had a condition to exit.
  UnmapViewOfFile(pBuf);
  CloseHandle(hMapFile);
  return 0;
}
```

## Notes
- Has not been tested on Linux
- Performance has not been tested
- Not tested extensively in general
