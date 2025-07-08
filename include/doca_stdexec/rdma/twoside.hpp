#pragma once
#ifndef DOCA_STDEXEC_RDMA_TWOSIDE_HPP
#define DOCA_STDEXEC_RDMA_TWOSIDE_HPP

#include "doca_stdexec/rdma.hpp"
#include <doca_pe.h>
#include <doca_rdma.h>
#include <stdexcept>
#include <stdexec/execution.hpp>

namespace doca_stdexec::rdma {

struct rdma_send_deleter {
  void operator()(doca_rdma_task_send *task) {
    doca_task_free(doca_rdma_task_send_as_task(task));
  }
};

struct RdmaSendTask {
  using raw_type = doca_rdma_task_send;

public:
  RdmaSendTask(doca_rdma_task_send *task) : task(task) {}

  RdmaSendTask(RdmaSendTask &&other) = default;

  static RdmaSendTask allocate(doca_rdma *rdma, doca_rdma_connection *conn,
                               doca_buf *buf) {

    union doca_data user_data;
    user_data.u64 = 0;

    doca_rdma_task_send *task = nullptr;

    auto err =
        doca_rdma_task_send_allocate_init(rdma, conn, buf, user_data, &task);
    printf("allocated send task %p\n", task);
    if (err != DOCA_SUCCESS) {
      throw std::runtime_error("Failed to allocate send task");
    }

    return RdmaSendTask(task);
  }

  doca_task *as_task() { return doca_rdma_task_send_as_task(task.get()); }

  void submit() {
    auto err = doca_task_submit(doca_rdma_task_send_as_task(task.get()));
    if (err != DOCA_SUCCESS) {
      throw std::runtime_error("Failed to submit send task");
    }
  }

  ~RdmaSendTask() = default;

private:
  std::unique_ptr<doca_rdma_task_send, rdma_send_deleter> task;
};

inline auto RdmaConnection::send(Buf buf) {
  auto sender = rdma::task::rdma_sender<RdmaSendTask, doca_buf *>{
      rdma->get(), connection.get(), std::make_tuple(buf.get())};
  return sender;
}

struct RdmaRecvTask {
  using raw_type = doca_rdma_task_receive;

public:
  RdmaRecvTask(doca_rdma_task_receive *task);

  static RdmaRecvTask allocate(doca_rdma *rdma, doca_buf *buf) {

    union doca_data user_data;
    user_data.u64 = 0;

    doca_rdma_task_receive *task = nullptr;

    auto err =
        doca_rdma_task_receive_allocate_init(rdma, buf, user_data, &task);
    if (err != DOCA_SUCCESS) {
      throw std::runtime_error("Failed to allocate recv task");
    }

    return RdmaRecvTask(task);
  }

  doca_task *as_task() { return doca_rdma_task_receive_as_task(task); }

  void submit() {
    auto err = doca_task_submit(doca_rdma_task_receive_as_task(task));
    if (err != DOCA_SUCCESS) {
      throw std::runtime_error("Failed to submit recv task");
    }
  }

  ~RdmaRecvTask();

private:
  doca_rdma_task_receive *task;
};

template <typename Receiver> struct RdmaRecvOperation {
  static void on_completion(doca_rdma_task_receive *task, doca_data user_data,
                            doca_data ctx_data) {
    auto *op = static_cast<RdmaRecvOperation *>(user_data.ptr);
    op->receiver.set_value();
  }

  static void on_error(doca_rdma_task_receive *task, doca_data user_data,
                       doca_data ctx_data) {
    auto *op = static_cast<RdmaRecvOperation *>(user_data.ptr);
    op->receiver.set_error();
  }

  void start() noexcept { task.submit(); }

  Receiver receiver;
  RdmaRecvTask task;
};

// TODO: return the connection
struct RdmaRecvSender {
  using sender_concept = stdexec::sender_t;

  using completion_signatures =
      stdexec::completion_signatures<stdexec::set_value_t,
                                     stdexec::set_error_t(doca_error_t)>;

  auto connect(stdexec::receiver auto rcvr) {
    return RdmaRecvOperation<decltype(rcvr)>{std::move(rcvr), std::move(task)};
  }

  RdmaRecvTask task;
};

inline auto Rdma::recv(Buf &buf) {
  auto task = RdmaRecvTask::allocate(rdma.get(), buf.get());
  // auto sender = rdma::task::rdma_sender<RdmaRecvTask>{std::move(task)};
  return stdexec::just(1);
}

} // namespace doca_stdexec::rdma

#endif // DOCA_STDEXEC_RDMA_TWOSIDE_HPP