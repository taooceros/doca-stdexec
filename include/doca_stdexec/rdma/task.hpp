#pragma once
#include "doca_rdma.h"
#include "doca_stdexec/common.hpp"
#include "doca_types.h"
#include "stdexec/__detail/__receivers.hpp"
#include "stdexec/__detail/__sync_wait.hpp"
#include "stdexec/__detail/__then.hpp"
#ifndef DOCA_STDEXEC_RDMA_TASK_HPP
#define DOCA_STDEXEC_RDMA_TASK_HPP

#include "doca_stdexec/operation.hpp"
#include <doca_error.h>
#include <doca_pe.h>
#include <doca_stdexec/operation.hpp>
#include <stdexec/execution.hpp>

namespace doca_stdexec::rdma::task {

template <typename T>
concept DocaTask = requires(T t) {
  { t.as_task() } -> std::same_as<doca_task *>;
};

template <typename Receiver, DocaTask DocaTask>
struct rdma_operation : immovable {

  using set_value_cb = void (*)(rdma_operation *);
  using set_error_cb = void (*)(rdma_operation *, doca_error_t);
  using set_stopped_cb = void (*)(rdma_operation *);

  set_value_cb set_value_callback = set_value;
  set_error_cb set_error_callback = set_error;
  set_stopped_cb set_stopped_callback = set_stopped;

  rdma_operation(DocaTask task, Receiver receiver)
      : task(std::move(task)), receiver(std::move(receiver)) {}

  static void set_value(rdma_operation *op) { op->receiver.set_value(); }

  static void set_error(rdma_operation *op, doca_error_t error) {
    op->receiver.set_error(error);
    check_error(error, "Operation Error");
  }

  static void set_stopped(rdma_operation *op) {
    // TODO: Implement this
  }

  void start() noexcept {
    doca_task_set_user_data(task.as_task(), doca_data{.ptr = this});
    auto status = doca_task_submit(task.as_task());
    check_error(status, "Failed to submit task");
  }
  DocaTask task;
  Receiver receiver;
};

template <DocaTask TaskType>
inline void rdma_operation_set_value(typename TaskType::raw_type *raw_task,
                                     doca_data user_data, doca_data ctx_data) {
  auto task = TaskType{raw_task};
  task.~TaskType(); // destroy the task
  auto *op = static_cast<rdma_operation<uint8_t, TaskType> *>(user_data.ptr);
  op->set_value_callback(op);
}

template <DocaTask TaskType>
inline void rdma_operation_set_error(typename TaskType::raw_type *raw_task,
                                     doca_data user_data, doca_data ctx_data) {
  auto *op = static_cast<rdma_operation<uint8_t, TaskType> *>(user_data.ptr);
  auto task = TaskType{raw_task};
  auto error = doca_task_get_status(task.as_task());

  printf("\n task: %p\n", &task);

  op->set_error_callback(op, error);
}

template <DocaTask TaskType>
inline void rdma_operation_set_stopped(typename TaskType::raw_type *raw_task,
                                       doca_data user_data,
                                       doca_data ctx_data) {
  auto task = TaskType{raw_task};
  auto *op = static_cast<rdma_operation<uint8_t, TaskType> *>(user_data.ptr);
  op->set_stopped_callback(op);
}

template <typename Task, typename... Buffers> struct rdma_sender {

  using sender_concept = stdexec::sender_t;

  using completion_signatures =
      stdexec::completion_signatures<stdexec::set_value_t(),
                                     stdexec::set_error_t(doca_error_t)>;

  stdexec::env<> get_env() { return {}; }

  template <stdexec::receiver Receiver> auto connect(Receiver rcvr) {
    return std::apply(
        [&](auto &...buffers) {
          auto task = Task::allocate(rdma, connection, buffers...);
          return rdma_operation<Receiver, Task>{std::move(task),
                                                std::move(rcvr)};
        },
        buffers);
  }

  doca_rdma *rdma;
  doca_rdma_connection *connection;
  std::tuple<Buffers...> buffers;
};

} // namespace doca_stdexec::rdma::task

#endif // DOCA_STDEXEC_RDMA_TASK_HPP