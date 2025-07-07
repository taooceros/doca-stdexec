#pragma once

#include "buf.hpp"
#include "common.hpp"
#include "mmap.hpp"
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <doca_buf_inventory.h>
#include <doca_error.h>
#include <doca_types.h>
#include <memory>

namespace doca_stdexec {

/**
 * @brief C++ wrapper for DOCA Buffer Inventory
 *
 * This class provides RAII semantics for managing DOCA buffer inventory
 * resources and uses check_error() for error handling instead of exceptions.
 */
class BufInventory {
private:
  struct doca_buf_inventory *inventory_ = nullptr;
  bool started_ = false;

public:
  /**
   * @brief Create a new buffer inventory
   *
   * @param num_elements Initial number of elements in the inventory
   */
  explicit BufInventory(size_t num_elements) {
    auto err = doca_buf_inventory_create(num_elements, &inventory_);
    check_error(err, "Failed to create buffer inventory with %zu elements",
                num_elements);
  }

  /**
   * @brief Destructor - automatically destroys the inventory
   */
  ~BufInventory() {
    if (inventory_) {
      // Stop the inventory if it's running
      if (started_) {
        stop();
      }
      // Destroy will implicitly stop if not already stopped
      auto err = doca_buf_inventory_destroy(inventory_);
      // Don't use check_error in destructor to avoid termination
      if (err != DOCA_SUCCESS) {
        printf("Warning: Failed to destroy buffer inventory. Error: %s [%d] "
               "(%s)\n",
               doca_error_get_name(err), err, doca_error_get_descr(err));
      }
    }
  }

  // Disable copy constructor and assignment
  BufInventory(const BufInventory &) = delete;
  BufInventory &operator=(const BufInventory &) = delete;

  // Enable move constructor and assignment
  BufInventory(BufInventory &&other) noexcept
      : inventory_(other.inventory_), started_(other.started_) {
    other.inventory_ = nullptr;
    other.started_ = false;
  }

  BufInventory &operator=(BufInventory &&other) noexcept {
    if (this != &other) {
      // Clean up current resource
      if (inventory_) {
        if (started_) {
          stop();
        }
        doca_buf_inventory_destroy(inventory_);
      }

      // Move from other
      inventory_ = other.inventory_;
      started_ = other.started_;
      other.inventory_ = nullptr;
      other.started_ = false;
    }
    return *this;
  }

  /**
   * @brief Start the buffer inventory
   *
   * Must be called before any buffer retrieval operations.
   * After start, inventory properties cannot be modified.
   */
  void start() {
    auto err = doca_buf_inventory_start(inventory_);
    check_error(err, "Failed to start buffer inventory");
    started_ = true;
  }

  /**
   * @brief Stop the buffer inventory
   *
   * Prevents any further buffer retrieval operations.
   */
  void stop() {
    if (started_) {
      auto err = doca_buf_inventory_stop(inventory_);
      check_error(err, "Failed to stop buffer inventory");
      started_ = false;
    }
  }

  /**
   * @brief Get a buffer by address and length
   *
   * @param mmap DOCA memory map structure (must be started)
   * @param addr Start address of the payload
   * @param len Length in bytes of the payload
   * @return Buf object wrapping the allocated buffer
   */
  template <typename T>
  Buf get_buffer_by_addr(const MMap<T> &mmap, void *addr, size_t len) {
    struct doca_buf *buf;
    auto err = doca_buf_inventory_buf_get_by_addr(inventory_, mmap.get(), addr, len, &buf);
    check_error(err, "Failed to get buffer by address (addr=%p, len=%zu)", addr, len);
    return Buf(buf);
  }

  /**
   * @brief Get a buffer by data pointer and length
   *
   * @param mmap DOCA memory map structure (must be started)
   * @param data Start address of the data
   * @param data_len Length in bytes of the data
   * @return Buf object wrapping the allocated buffer
   */
  template <typename T>
  Buf get_buffer_by_data(const MMap<T> &mmap, void *data, size_t data_len) {
    struct doca_buf *buf;
    auto err = doca_buf_inventory_buf_get_by_data(inventory_, mmap.get(), data, data_len, &buf);
    check_error(err, "Failed to get buffer by data (data=%p, data_len=%zu)", data, data_len);
    return Buf(buf);
  }

  /**
   * @brief Get a buffer with full argument specification
   *
   * @param mmap DOCA memory map structure (must be started)
   * @param addr Start address of the buffer
   * @param len Length in bytes of the buffer
   * @param data Start address of the data inside the buffer
   * @param data_len Length in bytes of the data
   * @return Buf object wrapping the allocated buffer
   */
  template <typename T>
  Buf get_buffer_by_args(const MMap<T> &mmap, void *addr, size_t len,
                         void *data, size_t data_len) {
    struct doca_buf *buf;
    auto err = doca_buf_inventory_buf_get_by_args(inventory_, mmap.get(), addr, len,
                                                  data, data_len, &buf);
    check_error(err,
                "Failed to get buffer by args (addr=%p, len=%zu, data=%p, "
                "data_len=%zu)",
                addr, len, data, data_len);
    return Buf(buf);
  }

  /**
   * @brief Duplicate a buffer (deep copy)
   *
   * @param src_buf Source buffer to duplicate
   * @return Buf object wrapping the duplicated buffer
   */
  Buf duplicate_buffer(const Buf &src_buf) {
    struct doca_buf *dst_buf;
    auto err = doca_buf_inventory_buf_dup(inventory_, src_buf.get(), &dst_buf);
    check_error(err, "Failed to duplicate buffer");
    return Buf(dst_buf);
  }

  /**
   * @brief Get a buffer for the entire memory range of an MMap
   *
   * @param mmap DOCA memory map structure (must be started)
   * @return Buf object wrapping the allocated buffer
   */
  template <typename T>
  Buf get_buffer_for_mmap(const MMap<T> &mmap) {
    auto span = mmap.get_memrange();
    return get_buffer_by_addr(mmap, span.data(), span.size_bytes());
  }

  /**
   * @brief Get a buffer for a specific span within an MMap
   *
   * @param mmap DOCA memory map structure (must be started)
   * @param span Span of data within the mmap
   * @return Buf object wrapping the allocated buffer
   */
  template <typename T>
  Buf get_buffer_for_span(const MMap<T> &mmap, std::span<T> span) {
    return get_buffer_by_data(mmap, span.data(), span.size_bytes());
  }

  /**
   * @brief Set user data for the inventory
   *
   * @param user_data User data to associate with the inventory
   */
  void set_user_data(union doca_data user_data) {
    auto err = doca_buf_inventory_set_user_data(inventory_, user_data);
    check_error(err, "Failed to set user data for inventory");
  }

  /**
   * @brief Get the total number of elements in the inventory
   *
   * @return Total number of elements
   */
  uint32_t get_num_elements() const {
    uint32_t num_elements;
    auto err = doca_buf_inventory_get_num_elements(inventory_, &num_elements);
    check_error(err, "Failed to get number of elements from inventory");
    return num_elements;
  }

  /**
   * @brief Get the number of free elements in the inventory
   *
   * @return Number of free elements
   */
  uint32_t get_num_free_elements() const {
    uint32_t num_free_elements;
    auto err = doca_buf_inventory_get_num_free_elements(inventory_,
                                                        &num_free_elements);
    check_error(err, "Failed to get number of free elements from inventory");
    return num_free_elements;
  }

  /**
   * @brief Get user data from the inventory
   *
   * @return User data associated with the inventory
   */
  union doca_data get_user_data() const {
    union doca_data user_data;
    auto err = doca_buf_inventory_get_user_data(inventory_, &user_data);
    check_error(err, "Failed to get user data from inventory");
    return user_data;
  }

  /**
   * @brief Expand the inventory by adding more elements
   *
   * @param num_elements Number of elements to add
   */
  void expand(uint32_t num_elements) {
    auto err = doca_buf_inventory_expand(inventory_, num_elements);
    check_error(err, "Failed to expand inventory by %u elements", num_elements);
  }

  /**
   * @brief Check if the inventory is started
   *
   * @return true if started, false otherwise
   */
  bool is_started() const { return started_; }

  /**
   * @brief Get the raw DOCA buffer inventory pointer
   *
   * @return Raw pointer to the underlying doca_buf_inventory structure
   */
  struct doca_buf_inventory *get_raw_inventory() const { return inventory_; }
};

} // namespace doca_stdexec