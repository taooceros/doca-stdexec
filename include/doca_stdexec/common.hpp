#pragma once
#include <cstdio>
#ifndef DOCA_STDEXEC_COMMON_HPP
#define DOCA_STDEXEC_COMMON_HPP

#include <doca_error.h>

namespace doca_stdexec {
template <typename... Args>
inline void check_error(doca_error_t err, const char *msg, Args... args) {
  if (err != DOCA_SUCCESS) {
    printf(msg, args...);
    printf(". Error: %s [%d] (%s)\n", doca_error_get_name(err), err,
           doca_error_get_descr(err));

    exit(1);
  }
}

} // namespace doca_stdexec
#endif