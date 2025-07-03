#pragma once
#ifndef DOCA_STDEXEC_MMAP_HPP
#define DOCA_STDEXEC_MMAP_HPP

#include <cstring>
#include <doca_error.h>
#include <doca_mmap.h>
#include <doca_types.h>
#include <functional>
#include <memory>
#include <span>
#include <stdexcept>
#include <vector>

#include "doca_stdexec/device.hpp"

namespace doca_stdexec {

/**
 * @brief Exception thrown when DOCA mmap operations fail
 */
class MMapException : public std::runtime_error {
public:
  MMapException(doca_error_t error, const std::string &message)
      : std::runtime_error(message + " (error: " +
                           std::to_string(static_cast<int>(error)) + ")"),
        error_code(error) {}

  doca_error_t get_error_code() const noexcept { return error_code; }

private:
  doca_error_t error_code;
};

/**
 * @brief RAII wrapper for DOCA Memory Map functionality
 *
 * This class provides a modern C++ interface for DOCA mmap operations,
 * including automatic resource management, easy device handling, and
 * comprehensive error handling.
 *
 * @tparam T The type of elements in the memory mapped region
 */
template <typename T> class MMap {
public:
  using FreeCallback = std::function<void(std::span<T>)>;

  /**
   * @brief Default constructor - creates an empty mmap
   */
  inline MMap() : mmap_(nullptr), started_(false) {
    doca_error_t result = doca_mmap_create(&mmap_);
    check_error(result, "create mmap");
    set_max_devices(8);
  }

  /**
   * @brief Constructor with memory range
   * @param data Span of memory to map
   */
  inline explicit MMap(std::span<T> data) : MMap() { set_memrange(data); }

  /**
   * @brief Constructor for creating from export
   * @param user_data User identifier for the mmap
   * @param export_desc Export descriptor
   * @param export_desc_len Length of export descriptor
   * @param dev Device for importing
   */
  inline MMap(const union doca_data *user_data, const void *export_desc,
              size_t export_desc_len, std::shared_ptr<Device> dev)
      : mmap_(nullptr), started_(false) {
    doca_error_t result = doca_mmap_create_from_export(
        user_data, export_desc, export_desc_len, dev->get(), &mmap_);
    check_error(result, "create mmap from export");
    started_ = true; // mmap created from export is automatically started
  }

  /**
   * @brief Destructor - automatically cleans up resources
   */
  inline ~MMap() { cleanup(); }

  // Disable copy constructor and assignment
  MMap(const MMap &) = delete;
  MMap &operator=(const MMap &) = delete;

  // Enable move constructor and assignment
  inline MMap(MMap &&other) noexcept
      : mmap_(other.mmap_), started_(other.started_),
        free_callback_(std::move(other.free_callback_)),
        devices_(std::move(other.devices_)) {
    other.mmap_ = nullptr;
    other.started_ = false;
  }

  inline MMap &operator=(MMap &&other) noexcept {
    if (this != &other) {
      cleanup();
      mmap_ = other.mmap_;
      started_ = other.started_;
      free_callback_ = std::move(other.free_callback_);
      devices_ = std::move(other.devices_);
      other.mmap_ = nullptr;
      other.started_ = false;
    }
    return *this;
  }

  /**
   * @brief Check if the mmap is valid
   */
  inline bool is_valid() const noexcept { return mmap_ != nullptr; }

  /**
   * @brief Get the underlying doca_mmap pointer
   */
  inline struct doca_mmap *get() const noexcept { return mmap_; }

  /**
   * @brief Release ownership of the mmap (caller becomes responsible for
   * cleanup)
   */
  inline struct doca_mmap *release() noexcept {
    struct doca_mmap *released = mmap_;
    mmap_ = nullptr;
    started_ = false;
    free_callback_.reset();
    devices_.clear();
    return released;
  }

  // Memory range management
  /**
   * @brief Set memory range for the mmap
   * @param data Span of memory to map
   */
  inline void set_memrange(std::span<T> data) {
    if (!mmap_) {
      throw MMapException(DOCA_ERROR_INVALID_VALUE, "Invalid mmap");
    }
    void *addr = static_cast<void *>(data.data());
    size_t len = data.size() * sizeof(T);
    doca_error_t result = doca_mmap_set_memrange(mmap_, addr, len);
    check_error(result, "set memory range");
  }

  /**
   * @brief Set memory range using dmabuf
   * @param dmabuf_fd File descriptor of dmabuf
   * @param data Span of memory to map
   * @param dmabuf_offset Offset in dmabuf (in bytes)
   */
  inline void set_dmabuf_memrange(int dmabuf_fd, std::span<T> data,
                                  size_t dmabuf_offset) {
    if (!mmap_) {
      throw MMapException(DOCA_ERROR_INVALID_VALUE, "Invalid mmap");
    }
    void *addr = static_cast<void *>(data.data());
    size_t len = data.size() * sizeof(T);
    doca_error_t result = doca_mmap_set_dmabuf_memrange(mmap_, dmabuf_fd, addr,
                                                        dmabuf_offset, len);
    check_error(result, "set dmabuf memory range");
  }

  /**
   * @brief Set DPA memory range
   * @param dpa DPA context
   * @param dpa_addr DPA address
   * @param count Number of elements of type T
   */
  inline void set_dpa_memrange(struct doca_dpa *dpa, uint64_t dpa_addr,
                               size_t count) {
    if (!mmap_) {
      throw MMapException(DOCA_ERROR_INVALID_VALUE, "Invalid mmap");
    }
    size_t len = count * sizeof(T);
    doca_error_t result = doca_mmap_set_dpa_memrange(mmap_, dpa, dpa_addr, len);
    check_error(result, "set DPA memory range");
  }

  /**
   * @brief Get the current memory range
   * @return std::span<T> representing the mapped memory
   */
  inline std::span<T> get_memrange() const {
    if (!mmap_) {
      throw MMapException(DOCA_ERROR_INVALID_VALUE, "Invalid mmap");
    }
    void *addr;
    size_t len;
    doca_error_t result = doca_mmap_get_memrange(mmap_, &addr, &len);
    check_error(result, "get memory range");

    if (len % sizeof(T) != 0) {
      throw MMapException(
          DOCA_ERROR_INVALID_VALUE,
          "Memory range size is not aligned to element type size");
    }

    T *typed_addr = static_cast<T *>(addr);
    size_t count = len / sizeof(T);
    return std::span<T>(typed_addr, count);
  }

  // Device management
  /**
   * @brief Add a device to the mmap
   * @param dev Device to add
   */
  inline void add_device(std::shared_ptr<Device> dev) {
    if (!mmap_) {
      throw MMapException(DOCA_ERROR_INVALID_VALUE, "Invalid mmap");
    }
    doca_error_t result = doca_mmap_add_dev(mmap_, dev->get());
    check_error(result, "add device");
    devices_.push_back(dev);
  }

  /**
   * @brief Remove a device from the mmap
   * @param dev Device to remove
   */
  inline void remove_device(std::shared_ptr<Device> dev) {
    if (!mmap_) {
      throw MMapException(DOCA_ERROR_INVALID_VALUE, "Invalid mmap");
    }
    doca_error_t result = doca_mmap_rm_dev(mmap_, dev->get());
    check_error(result, "remove device");

    // Remove from our device list
    auto it = std::find(devices_.begin(), devices_.end(), dev);
    if (it != devices_.end()) {
      devices_.erase(it);
    }
  }

  // Lifecycle management
  /**
   * @brief Start the mmap (enables operations)
   */
  inline void start() {
    if (!mmap_) {
      throw MMapException(DOCA_ERROR_INVALID_VALUE, "Invalid mmap");
    }
    if (started_) {
      return; // Already started
    }
    doca_error_t result = doca_mmap_start(mmap_);
    check_error(result, "start mmap");
    started_ = true;
  }

  /**
   * @brief Stop the mmap (disables operations)
   */
  inline void stop() {
    if (!mmap_) {
      throw MMapException(DOCA_ERROR_INVALID_VALUE, "Invalid mmap");
    }
    if (!started_) {
      return; // Already stopped
    }
    doca_error_t result = doca_mmap_stop(mmap_);
    check_error(result, "stop mmap");
    started_ = false;
  }

  /**
   * @brief Check if the mmap is started
   */
  inline bool is_started() const noexcept { return started_; }

  // Export/Import functionality
  /**
   * @brief Export mmap for PCI access
   * @param dev Device to export for
   * @return std::pair<std::unique_ptr<uint8_t[]>, size_t> containing export
   * data and length
   */
  inline std::pair<std::unique_ptr<uint8_t[]>, size_t>
  export_pci(std::shared_ptr<Device> dev) {
    if (!mmap_) {
      throw MMapException(DOCA_ERROR_INVALID_VALUE, "Invalid mmap");
    }
    const void *export_desc;
    size_t export_desc_len;
    doca_error_t result =
        doca_mmap_export_pci(mmap_, dev->get(), &export_desc, &export_desc_len);
    check_error(result, "export PCI");

    // Copy the export descriptor to our own buffer
    auto buffer = std::make_unique<uint8_t[]>(export_desc_len);
    std::memcpy(buffer.get(), export_desc, export_desc_len);

    return std::make_pair(std::move(buffer), export_desc_len);
  }

  /**
   * @brief Export mmap for RDMA access
   * @param dev Device to export for
   * @return std::pair<std::unique_ptr<uint8_t[]>, size_t> containing export
   * data and length
   */
  inline std::span<const std::byte> export_rdma(Device &dev) {
    if (!mmap_) {
      throw MMapException(DOCA_ERROR_INVALID_VALUE, "Invalid mmap");
    }
    const void *export_desc;
    size_t export_desc_len;
    doca_error_t result =
        doca_mmap_export_rdma(mmap_, dev.get(), &export_desc, &export_desc_len);
    check_error(result, "export RDMA");

    // Copy the export descriptor to our own buffer
    auto buffer =
        std::span{static_cast<const std::byte *>(export_desc), export_desc_len};

    return buffer;
  }

  /**
   * @brief Get DPA handle for device
   * @param dev Device to get handle for
   * @return DPA handle
   */
  inline doca_dpa_dev_mmap_t get_dpa_handle(std::shared_ptr<Device> dev) {
    if (!mmap_) {
      throw MMapException(DOCA_ERROR_INVALID_VALUE, "Invalid mmap");
    }
    doca_dpa_dev_mmap_t handle;
    doca_error_t result =
        doca_mmap_dev_get_dpa_handle(mmap_, dev->get(), &handle);
    check_error(result, "get DPA handle");
    return handle;
  }

  // Property management
  /**
   * @brief Set access permissions
   * @param access_mask Bitwise combination of access flags
   */
  inline void set_permissions(uint32_t access_mask) {
    if (!mmap_) {
      throw MMapException(DOCA_ERROR_INVALID_VALUE, "Invalid mmap");
    }
    doca_error_t result = doca_mmap_set_permissions(mmap_, access_mask);
    check_error(result, "set permissions");
  }

  /**
   * @brief Set maximum number of devices
   * @param max_devices Maximum number of devices
   */
  inline void set_max_devices(uint32_t max_devices) {
    if (!mmap_) {
      throw MMapException(DOCA_ERROR_INVALID_VALUE, "Invalid mmap");
    }
    doca_error_t result = doca_mmap_set_max_num_devices(mmap_, max_devices);
    check_error(result, "set max devices");
  }

  /**
   * @brief Set user data
   * @param user_data User data to associate with mmap
   */
  inline void set_user_data(union doca_data user_data) {
    if (!mmap_) {
      throw MMapException(DOCA_ERROR_INVALID_VALUE, "Invalid mmap");
    }
    doca_error_t result = doca_mmap_set_user_data(mmap_, user_data);
    check_error(result, "set user data");
  }

  /**
   * @brief Set memory free callback
   * @param callback Callback function to call when freeing memory
   */
  inline void set_free_callback(FreeCallback callback) {
    if (!mmap_) {
      throw MMapException(DOCA_ERROR_INVALID_VALUE, "Invalid mmap");
    }
    free_callback_ = std::make_unique<FreeCallback>(std::move(callback));
    doca_error_t result = doca_mmap_set_free_cb(mmap_, free_callback_wrapper,
                                                free_callback_.get());
    check_error(result, "set free callback");
  }

  // Property getters
  /**
   * @brief Get user data
   */
  inline union doca_data get_user_data() const {
    if (!mmap_) {
      throw MMapException(DOCA_ERROR_INVALID_VALUE, "Invalid mmap");
    }
    union doca_data user_data;
    doca_error_t result = doca_mmap_get_user_data(mmap_, &user_data);
    check_error(result, "get user data");
    return user_data;
  }

  /**
   * @brief Get maximum number of devices
   */
  inline uint32_t get_max_devices() const {
    if (!mmap_) {
      throw MMapException(DOCA_ERROR_INVALID_VALUE, "Invalid mmap");
    }
    uint32_t max_devices;
    doca_error_t result = doca_mmap_get_max_num_devices(mmap_, &max_devices);
    check_error(result, "get max devices");
    return max_devices;
  }

  /**
   * @brief Get current number of buffers
   */
  inline uint32_t get_num_buffers() const {
    if (!mmap_) {
      throw MMapException(DOCA_ERROR_INVALID_VALUE, "Invalid mmap");
    }
    uint32_t num_bufs;
    doca_error_t result = doca_mmap_get_num_bufs(mmap_, &num_bufs);
    check_error(result, "get number of buffers");
    return num_bufs;
  }

  /**
   * @brief Check if mmap has been exported
   */
  inline bool is_exported() const {
    if (!mmap_) {
      return false;
    }
    uint8_t exported;
    doca_error_t result = doca_mmap_get_exported(mmap_, &exported);
    if (result != DOCA_SUCCESS) {
      return false;
    }
    return exported != 0;
  }

  /**
   * @brief Check if mmap was created from export
   */
  inline bool is_from_export() const {
    if (!mmap_) {
      return false;
    }
    uint8_t from_export;
    doca_error_t result = doca_mmap_get_from_export(mmap_, &from_export);
    if (result != DOCA_SUCCESS) {
      return false;
    }
    return from_export != 0;
  }

  /**
   * @brief Get the list of devices associated with this mmap
   */
  inline const std::vector<std::shared_ptr<Device>> &get_devices() const {
    return devices_;
  }

  // Static factory methods
  /**
   * @brief Create mmap from export descriptor
   * @param user_data User data for the new mmap
   * @param export_desc Export descriptor
   * @param export_desc_len Length of export descriptor
   * @param dev Device for import
   * @return New MMap instance
   */
  static inline MMap<T> create_from_export(const union doca_data *user_data,
                                           const void *export_desc,
                                           size_t export_desc_len,
                                           std::shared_ptr<Device> dev) {
    return MMap<T>(user_data, export_desc, export_desc_len, dev);
  }

  // Capability checking
  /**
   * @brief Check if device supports PCI export
   * @param devinfo Device info to check
   * @return true if supported
   */
  static inline bool
  is_export_pci_supported(const struct doca_devinfo *devinfo) {
    uint8_t supported;
    doca_error_t result =
        doca_mmap_cap_is_export_pci_supported(devinfo, &supported);
    return (result == DOCA_SUCCESS) && (supported != 0);
  }

  /**
   * @brief Check if device supports create from PCI export
   * @param devinfo Device info to check
   * @return true if supported
   */
  static inline bool
  is_create_from_export_pci_supported(const struct doca_devinfo *devinfo) {
    uint8_t supported;
    doca_error_t result =
        doca_mmap_cap_is_create_from_export_pci_supported(devinfo, &supported);
    return (result == DOCA_SUCCESS) && (supported != 0);
  }

private:
  struct doca_mmap *mmap_;
  bool started_;
  std::unique_ptr<FreeCallback> free_callback_;
  std::vector<std::shared_ptr<Device>> devices_;

  // Helper methods
  inline void check_error(doca_error_t error,
                          const std::string &operation) const {
    if (error != DOCA_SUCCESS) {
      throw MMapException(error, "Failed to " + operation);
    }
  }

  inline void cleanup() {
    if (mmap_) {
      if (started_) {
        doca_mmap_stop(mmap_);
      }
      doca_mmap_destroy(mmap_);
      mmap_ = nullptr;
      started_ = false;
    }
    free_callback_.reset();
    devices_.clear();
  }

  static inline void free_callback_wrapper(void *addr, size_t len,
                                           void *cookie) {
    if (cookie) {
      FreeCallback *callback = static_cast<FreeCallback *>(cookie);
      if (len % sizeof(T) != 0) {
        // Handle misaligned memory - this shouldn't happen in normal cases
        return;
      }
      T *typed_addr = static_cast<T *>(addr);
      size_t count = len / sizeof(T);
      (*callback)(std::span<T>(typed_addr, count));
    }
  }
};

} // namespace doca_stdexec

#endif