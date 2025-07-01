#pragma once
#include <cstdio>
#ifndef DOCA_STDEXEC_COMMON_HPP
#define DOCA_STDEXEC_COMMON_HPP

#include <doca_error.h>

namespace doca_stdexec {
inline void check_error(doca_error_t err, const char *msg) {
  if (err != DOCA_SUCCESS) {
    printf(msg, err);
    exit(1);
  }
}

} // namespace doca_stdexec
#endif