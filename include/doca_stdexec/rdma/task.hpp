#pragma once
#include "doca_types.h"
#ifndef DOCA_STDEXEC_RDMA_TASK_HPP
#define DOCA_STDEXEC_RDMA_TASK_HPP

#include "doca_stdexec/operation.hpp"
#include <doca_error.h>
#include <doca_stdexec/operation.hpp>
#include <stdexec/execution.hpp>

namespace doca_stdexec::rdma::task {

template <typename T>
concept DocaTask = requires(T t) {
  { t.submit() } -> std::same_as<void>;
};

template <typename Receiver, DocaTask DocaTask>
struct rdma_operation : immovable {

  static void on_completion(DocaTask::raw_type *task, doca_data user_data,
                            doca_data ctx_data) {
    auto *op = static_cast<rdma_operation *>(user_data.ptr);
    op->receiver.set_value();
  }

  static void on_error(DocaTask *task, doca_data user_data,
                       doca_data ctx_data) {
    auto *op = static_cast<rdma_operation *>(user_data.ptr);
    op->receiver.set_error();
  }

  void start() noexcept { task.submit(on_completion, on_error, this); }

  Receiver receiver;
  DocaTask task;
};

template <typename Task> struct rdma_sender {

  using sender_concept = stdexec::sender_t;

  using completion_signatures =
      stdexec::completion_signatures<stdexec::set_value_t,
                                     stdexec::set_error_t(doca_error_t)>;

  auto connect(stdexec::receiver auto rcvr) {
    return rdma_operation<decltype(rcvr), Task>{std::move(rcvr), std::move(task)};
  }

  Task task;
};

} // namespace doca_stdexec::rdma::task

#endif // DOCA_STDEXEC_RDMA_TASK_HPP