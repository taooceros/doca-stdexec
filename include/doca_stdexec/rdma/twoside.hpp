#pragma once
#ifndef DOCA_STDEXEC_RDMA_TWOSIDE_HPP
#define DOCA_STDEXEC_RDMA_TWOSIDE_HPP

#include "doca_stdexec/rdma.hpp"
#include <doca_pe.h>
#include <doca_rdma.h>
#include <stdexcept>
#include <stdexec/execution.hpp>

namespace doca_stdexec::rdma {

struct RdmaSendTask {
  using raw_type = doca_rdma_task_send;

public:
  RdmaSendTask(doca_rdma_task_send *task);

  static RdmaSendTask allocate(doca_rdma *rdma, doca_rdma_connection *conn,
                               doca_buf *buf) {

    union doca_data user_data;
    user_data.u64 = 0;

    doca_rdma_task_send *task = nullptr;

    auto err =
        doca_rdma_task_send_allocate_init(rdma, conn, buf, user_data, &task);
    if (err != DOCA_SUCCESS) {
      throw std::runtime_error("Failed to allocate send task");
    }

    return RdmaSendTask(task);
  }

  void submit() {
    auto err = doca_task_submit(doca_rdma_task_send_as_task(task));
    if (err != DOCA_SUCCESS) {
      throw std::runtime_error("Failed to submit send task");
    }
  }

  ~RdmaSendTask();

private:
  doca_rdma_task_send *task;
};

inline auto RdmaConnection::send(Buf &buf) {
  auto task = RdmaSendTask::allocate(rdma->get(), connection, buf.get());
  auto sender = rdma::task::rdma_sender<RdmaSendTask>{std::move(task)};
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
  auto sender = rdma::task::rdma_sender<RdmaRecvTask>{std::move(task)};
  return sender;
}

} // namespace doca_stdexec::rdma

#endif // DOCA_STDEXEC_RDMA_TWOSIDE_HPP