#pragma once
#ifndef DOCA_STDEXEC_CONTEXT_HPP
#define DOCA_STDEXEC_CONTEXT_HPP
#include <concepts>
#include <doca_ctx.h>
#include <doca_pe.h>
#include <doca_stdexec/common.hpp>

namespace doca_stdexec {

struct Context {

  void set_state_changed_cb(doca_ctx_state_changed_callback_t cb) {
    auto status = doca_ctx_set_state_changed_cb(as_ctx(), cb);
    check_error(status, "Failed to set state change callbacks");
  }

  void start() {
    doca_error_t status = doca_ctx_start(as_ctx());
    check_error(status, "Failed to start context");
  }

  void stop() {
    doca_error_t status = doca_ctx_stop(as_ctx());
    check_error(status, "Failed to stop context");
  }

  virtual ~Context() = default;
  virtual doca_ctx *as_ctx() noexcept = 0;
};

} // namespace doca_stdexec
#endif // DOCA_STDEXEC_CONTEXT_HPP