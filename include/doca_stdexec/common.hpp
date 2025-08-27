#pragma once
#include <cstdio>
#include <mutex>
#ifndef DOCA_STDEXEC_COMMON_HPP
#define DOCA_STDEXEC_COMMON_HPP
#include <doca_error.h>

#if defined(__has_feature)
#   if __has_feature(address_sanitizer) // for clang
#       define __SANITIZE_ADDRESS__ // GCC already sets this
#   endif
#endif

#if defined(__SANITIZE_ADDRESS__)
#include <sanitizer/common_interface_defs.h>
#endif

#include <stdexec/execution.hpp>

namespace doca_stdexec {
template <typename... Args>
inline void check_error(doca_error_t err, const char *msg, Args... args) {
  static std::mutex mutex;
  std::unique_lock lock(mutex);

  if (err != DOCA_SUCCESS) {
#if defined(__SANITIZE_ADDRESS__)
    __sanitizer_print_stack_trace();
#endif
    printf(msg, args...);
    printf(". Error: %s [%d] (%s)\n", doca_error_get_name(err), err,
           doca_error_get_descr(err));

    exit(1);
  }
}

} // namespace doca_stdexec
#endif