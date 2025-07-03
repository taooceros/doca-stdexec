#pragma once
#ifndef DOCA_STDEXEC_CONTEXT_HPP
#define DOCA_STDEXEC_CONTEXT_HPP
#include <concepts>
#include <doca_ctx.h>
#include <doca_pe.h>
#include <doca_stdexec/common.hpp>

namespace doca_stdexec {

struct Context {

  void start() {
    doca_error_t status = doca_ctx_start(as_ctx());
    check_error(status, "Failed to start context");
  }

  virtual ~Context() = default;
  virtual doca_ctx *as_ctx() noexcept = 0;
};

} // namespace doca_stdexec
#endif // DOCA_STDEXEC_CONTEXT_HPP