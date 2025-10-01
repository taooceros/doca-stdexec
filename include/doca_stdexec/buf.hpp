#pragma once
#include "doca_stdexec/common.hpp"
#ifndef DOCA_STDEXEC_BUF_HPP
#define DOCA_STDEXEC_BUF_HPP

#include <cstddef>
#include <cstdint>
#include <doca_buf.h>
#include <doca_error.h>
#include <span>
#include <stdexcept>
#include <vector>

namespace doca_stdexec {

/**
 * @brief Exception thrown when DOCA buffer operations fail
 */
class BufException : public std::runtime_error {
public:
    BufException(doca_error_t error, const std::string& message)
        : std::runtime_error(message + " (error: " + std::to_string(static_cast<int>(error)) + ")"),
          error_code(error) {}

    doca_error_t get_error_code() const noexcept {
        return error_code;
    }

private:
    doca_error_t error_code;
};

/**
 * @brief RAII wrapper for DOCA Buffer functionality
 *
 * This class provides a modern C++ interface for DOCA buffer operations,
 * including automatic reference counting, memory management, and
 * comprehensive error handling.
 */
class Buf {
private:
    struct doca_buf* buf_;

public:
    /**
     * @brief Default constructor - creates an invalid buffer
     */
    Buf() : buf_(nullptr) {}

    /**
     * @brief Construct from existing doca_buf
     * @param buf The doca_buf to wrap
     */
    explicit Buf(struct doca_buf* buf) : buf_(buf) {}

    /**
     * @brief Copy constructor
     */
    Buf(const Buf& other) : buf_(other.buf_) {
        if (buf_) {
            uint16_t refcount;
            doca_error_t result = doca_buf_inc_refcount(buf_, &refcount);
            check_error(result, "increment reference count");
        }
    }

    /**
     * @brief Move constructor
     */
    Buf(Buf&& other) noexcept : buf_(other.buf_) {
        other.buf_ = nullptr;
    }

    /**
     * @brief Copy assignment operator
     */
    Buf& operator=(const Buf& other) {
        if (this != &other) {
            cleanup();
            buf_ = other.buf_;
            if (buf_) {
                uint16_t refcount;
                doca_error_t result = doca_buf_inc_refcount(buf_, &refcount);
                check_error(result, "increment reference count");
            }
        }
        return *this;
    }

    /**
     * @brief Move assignment operator
     */
    Buf& operator=(Buf&& other) noexcept {
        if (this != &other) {
            cleanup();
            buf_ = other.buf_;
            other.buf_ = nullptr;
        }
        return *this;
    }

    /**
     * @brief Destructor
     */
    ~Buf() {
        cleanup();
    }

    /**
     * @brief Check if the buffer is valid
     */
    bool is_valid() const noexcept {
        return buf_ != nullptr;
    }

    /**
     * @brief Get the underlying doca_buf pointer
     */
    struct doca_buf* get() const noexcept {
        return buf_;
    }

    /**
     * @brief Release ownership of the buffer (caller becomes responsible for
     * cleanup)
     */
    struct doca_buf* release() noexcept {
        struct doca_buf* released = buf_;
        buf_ = nullptr;
        return released;
    }

    // Reference counting operations

    /**
     * @brief Get current reference count
     */
    uint16_t get_refcount() const {
        if (!buf_) {
            check_error(DOCA_ERROR_INVALID_VALUE, "Invalid buffer");
        }
        uint16_t refcount;
        doca_error_t result = doca_buf_get_refcount(buf_, &refcount);
        check_error(result, "get reference count");
        return refcount;
    }

    /**
     * @brief Increment reference count
     */
    uint16_t inc_refcount() {
        if (!buf_) {
            check_error(DOCA_ERROR_INVALID_VALUE, "Invalid buffer");
        }
        uint16_t refcount;
        doca_error_t result = doca_buf_inc_refcount(buf_, &refcount);
        check_error(result, "increment reference count");
        return refcount;
    }

    /**
     * @brief Decrement reference count
     */
    uint16_t dec_refcount() {
        if (!buf_) {
            check_error(DOCA_ERROR_INVALID_VALUE, "Invalid buffer");
        }
        uint16_t refcount;
        doca_error_t result = doca_buf_dec_refcount(buf_, &refcount);
        check_error(result, "decrement reference count");
        return refcount;
    }

    // Buffer property accessors

    /**
     * @brief Get buffer length
     */
    size_t get_len() const {
        if (!buf_) {
            check_error(DOCA_ERROR_INVALID_VALUE, "Invalid buffer");
        }
        size_t len;
        doca_error_t result = doca_buf_get_len(buf_, &len);
        check_error(result, "get buffer length");
        return len;
    }

    /**
     * @brief Get buffer head pointer
     */
    void* get_head() const {
        if (!buf_) {
            check_error(DOCA_ERROR_INVALID_VALUE, "Invalid buffer");
        }
        void* head;
        doca_error_t result = doca_buf_get_head(buf_, &head);
        check_error(result, "get buffer head");
        return head;
    }

    /**
     * @brief Get buffer head as typed pointer
     */
    template <typename T>
    T* get_head_as() const {
        return static_cast<T*>(get_head());
    }

    /**
     * @brief Get data length
     */
    [[nodiscard]] size_t get_data_len() const {
        if (!buf_) {
            check_error(DOCA_ERROR_INVALID_VALUE, "Invalid buffer");
        }
        size_t data_len;
        doca_error_t result = doca_buf_get_data_len(buf_, &data_len);
        check_error(result, "get data length");
        return data_len;
    }

    /**
     * @brief Get data pointer
     */
    [[nodiscard]] void* get_data() const {
        if (!buf_) {
            check_error(DOCA_ERROR_INVALID_VALUE, "Invalid buffer");
        }
        void* data;
        doca_error_t result = doca_buf_get_data(buf_, &data);
        check_error(result, "get data pointer");
        return data;
    }


    [[nodiscard]] void* data() const {
        return get_data();
    }

    [[nodiscard]] size_t size_bytes() const {
        return get_data_len();
    }

    /**
     * @brief Get data as typed pointer
     */
    template <typename T>
    T* get_data_as() const {
        return static_cast<T*>(get_data());
    }

    /**
     * @brief Get data as span of bytes
     */
    [[nodiscard]] std::span<std::byte> get_data_span() const {
        void* ptr = get_data();
        size_t len = get_data_len();
        return {static_cast<std::byte*>(ptr), len};
    }

    /**
     * @brief Get data as span of specific type
     */
    template <typename T>
    std::span<T> get_data_span_as() const {
        T* data = get_data_as<T>();
        size_t len = get_data_len();
        if (len % sizeof(T) != 0) {
            check_error(DOCA_ERROR_INVALID_VALUE, "Data length not aligned to element type size");
        }
        return std::span<T>(data, len / sizeof(T));
    }

    // Data manipulation operations

    /**
     * @brief Set data pointer and length
     */
    void set_data(void* data, size_t data_len) {
        if (!buf_) {
            check_error(DOCA_ERROR_INVALID_VALUE, "Invalid buffer");
        }
        doca_error_t result = doca_buf_set_data(buf_, data, data_len);
        check_error(result, "set data");
    }

    /**
     * @brief Set data from span
     */
    template <typename T>
    void set_data(std::span<T> data) {
        void* ptr = static_cast<void*>(data.data());
        size_t len = data.size() * sizeof(T);
        set_data(ptr, len);
    }

    /**
     * @brief Set data length
     */
    void set_data_len(size_t data_len) {
        if (!buf_) {
            check_error(DOCA_ERROR_INVALID_VALUE, "Invalid buffer");
        }
        doca_error_t result = doca_buf_set_data_len(buf_, data_len);
        check_error(result, "set data length");
    }

    /**
     * @brief Reset data length to full buffer size
     */
    void reset_data_len() {
        if (!buf_) {
            check_error(DOCA_ERROR_INVALID_VALUE, "Invalid buffer");
        }
        doca_error_t result = doca_buf_reset_data_len(buf_);
        check_error(result, "reset data length");
    }

    // List operations

    /**
     * @brief Get next buffer in list
     * @param next_buf Output parameter for the next buffer
     * @return true if next buffer exists, false otherwise
     */
    bool get_next_in_list(Buf& next_buf) const {
        if (!buf_) {
            check_error(DOCA_ERROR_INVALID_VALUE, "Invalid buffer");
        }
        struct doca_buf* next_doca_buf;
        doca_error_t result = doca_buf_get_next_in_list(buf_, &next_doca_buf);
        if (result == DOCA_ERROR_NOT_FOUND) {
            return false;
        }
        check_error(result, "get next in list");
        next_buf = Buf(next_doca_buf);
        return true;
    }

    /**
     * @brief Check if there is a next buffer in list
     */
    bool has_next_in_list() const {
        if (!buf_) {
            return false;
        }
        struct doca_buf* next_buf;
        doca_error_t result = doca_buf_get_next_in_list(buf_, &next_buf);
        return result == DOCA_SUCCESS;
    }

    /**
     * @brief Get last buffer in list
     */
    Buf get_last_in_list() const {
        if (!buf_) {
            check_error(DOCA_ERROR_INVALID_VALUE, "Invalid buffer");
        }
        struct doca_buf* last_buf;
        doca_error_t result = doca_buf_get_last_in_list(buf_, &last_buf);
        check_error(result, "get last in list");
        return Buf(last_buf);
    }

    /**
     * @brief Check if this is the last buffer in list
     */
    bool is_last_in_list() const {
        if (!buf_) {
            check_error(DOCA_ERROR_INVALID_VALUE, "Invalid buffer");
        }
        uint8_t is_last;
        doca_error_t result = doca_buf_is_last_in_list(buf_, &is_last);
        check_error(result, "check if last in list");
        return is_last != 0;
    }

    /**
     * @brief Check if this is the first buffer in list
     */
    bool is_first_in_list() const {
        if (!buf_) {
            check_error(DOCA_ERROR_INVALID_VALUE, "Invalid buffer");
        }
        uint8_t is_first;
        doca_error_t result = doca_buf_is_first_in_list(buf_, &is_first);
        check_error(result, "check if first in list");
        return is_first != 0;
    }

    /**
     * @brief Check if this buffer is part of a list
     */
    bool is_in_list() const {
        if (!buf_) {
            check_error(DOCA_ERROR_INVALID_VALUE, "Invalid buffer");
        }
        uint8_t is_in_list;
        doca_error_t result = doca_buf_is_in_list(buf_, &is_in_list);
        check_error(result, "check if in list");
        return is_in_list != 0;
    }

    /**
     * @brief Get list length
     */
    uint32_t get_list_len() const {
        if (!buf_) {
            check_error(DOCA_ERROR_INVALID_VALUE, "Invalid buffer");
        }
        uint32_t num_elements;
        doca_error_t result = doca_buf_get_list_len(buf_, &num_elements);
        check_error(result, "get list length");
        return num_elements;
    }

    // List chaining operations

    /**
     * @brief Chain two buffer lists together
     */
    void chain_list(Buf& other) {
        if (!buf_ || !other.buf_) {
            check_error(DOCA_ERROR_INVALID_VALUE, "Invalid buffer");
        }
        doca_error_t result = doca_buf_chain_list(buf_, other.buf_);
        check_error(result, "chain lists");
    }

    /**
     * @brief Chain lists with explicit tail
     */
    void chain_list_tail(Buf& tail, Buf& other) {
        if (!buf_ || !tail.buf_ || !other.buf_) {
            check_error(DOCA_ERROR_INVALID_VALUE, "Invalid buffer");
        }
        doca_error_t result = doca_buf_chain_list_tail(buf_, tail.buf_, other.buf_);
        check_error(result, "chain lists with tail");
    }

    /**
     * @brief Unchain buffer from its list
     */
    void unchain_list(Buf& split_point) {
        if (!buf_ || !split_point.buf_) {
            check_error(DOCA_ERROR_INVALID_VALUE, "Invalid buffer");
        }
        doca_error_t result = doca_buf_unchain_list(buf_, split_point.buf_);
        check_error(result, "unchain list");
    }

    // Utility functions

    /**
     * @brief Collect all buffers in the list into a vector
     */
    std::vector<Buf> collect_list() const {
        std::vector<Buf> result;
        if (!buf_) {
            return result;
        }

        Buf current(buf_);
        result.push_back(current);

        while (true) {
            Buf next;
            if (!current.get_next_in_list(next)) {
                break;
            }
            result.push_back(next);
            current = next;
        }

        return result;
    }

private:
    void cleanup() {
        if (buf_) {
            uint16_t refcount;
            auto err = doca_buf_dec_refcount(buf_, &refcount);
            printf("Decrementing buf refcount %d\n", refcount);
            check_error(err, "Failed to decrement buf refcount");
            if (refcount == 1) {
                printf("Destroying buf\n");
            }
        }
        buf_ = nullptr;
    }
};

} // namespace doca_stdexec

#endif // DOCA_STDEXEC_BUF_HPP