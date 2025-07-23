#include "doca_stdexec/mmap.hpp"
#include <cstdlib>
#include <iostream>
#include <memory>
#include <vector>

using namespace doca_stdexec;

// Example: Basic MMap usage
void basic_mmap_example() {
  std::cout << "=== Basic MMap Example ===" << std::endl;

  try {
    // Allocate some memory
    size_t memory_size = 4096;
    void *memory = std::aligned_alloc(4096, memory_size);
    if (!memory) {
      throw std::runtime_error("Failed to allocate memory");
    }

    // Create MMap with memory range
    MMap<uint8_t> mmap(
        std::span<uint8_t>(static_cast<uint8_t *>(memory), memory_size));

    // Add device (assuming you have a device)
    // struct doca_dev* dev = get_your_device();
    // mmap.add_device(dev);

    // Start the mmap
    mmap.start();

    std::cout << "MMap created and started successfully" << std::endl;
    std::cout << "Is started: " << mmap.is_started() << std::endl;

    auto mem_span = mmap.get_memrange();
    std::cout << "Memory range: addr=" << mem_span.data()
              << ", len=" << mem_span.size() << std::endl;

    // Stop and clean up (automatic in destructor)
    mmap.stop();

    std::free(memory);

  } catch (const MMapException &e) {
    std::cerr << "MMap error: " << e.what() << std::endl;
    std::cerr << "Error code: " << static_cast<int>(e.get_error_code())
              << std::endl;
  }
}
