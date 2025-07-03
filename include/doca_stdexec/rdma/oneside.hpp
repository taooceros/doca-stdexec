#pragma once
#ifndef DOCA_STDEXEC_RDMA_ONESIDE_HPP
#define DOCA_STDEXEC_RDMA_ONESIDE_HPP

#include "doca_stdexec/rdma.hpp"
#include <doca_pe.h>
#include <doca_rdma.h>
#include <stdexcept>
#include <stdexec/execution.hpp>

namespace doca_stdexec::rdma {

struct RdmaWriteTask {
  using raw_type = doca_rdma_task_write;

public:
  RdmaWriteTask(doca_rdma_task_write *task) : task(task) {}

  static RdmaWriteTask allocate(doca_rdma *rdma, doca_rdma_connection *conn,
                                Buf &src, Buf &dst) {

    union doca_data user_data;
    user_data.u64 = 0;

    doca_rdma_task_write *task = nullptr;

    auto err = doca_rdma_task_write_allocate_init(rdma, conn, src.get(),
                                                  dst.get(), user_data, &task);
    if (err != DOCA_SUCCESS) {
      throw std::runtime_error("Failed to allocate write task");
    }

    return RdmaWriteTask(task);
  }

  void submit() {
    auto err = doca_task_submit(doca_rdma_task_write_as_task(task));
    if (err != DOCA_SUCCESS) {
      throw std::runtime_error("Failed to submit write task");
    }
  }

  ~RdmaWriteTask() { doca_task_free(doca_rdma_task_write_as_task(task)); }

private:
  doca_rdma_task_write *task;
};

inline auto RdmaConnection::write(Buf &src, Buf &dst) {
  auto task = RdmaWriteTask::allocate(rdma->get(), connection, src, dst);
  auto sender = rdma::task::rdma_sender<RdmaWriteTask>{std::move(task)};
  return sender;
}

struct RdmaReadTask {
  using raw_type = doca_rdma_task_read;

  RdmaReadTask(doca_rdma_task_read *task);

  static RdmaReadTask allocate(doca_rdma *rdma, doca_rdma_connection *conn,
                               Buf &src, Buf &dst) {

    union doca_data user_data;
    user_data.u64 = 0;

    doca_rdma_task_read *task = nullptr;

    auto err = doca_rdma_task_read_allocate_init(rdma, conn, src.get(),
                                                 dst.get(), user_data, &task);
    if (err != DOCA_SUCCESS) {
      throw std::runtime_error("Failed to allocate read task");
    }

    return RdmaReadTask(task);
  }

  void submit() {
    auto err = doca_task_submit(doca_rdma_task_read_as_task(task));
    if (err != DOCA_SUCCESS) {
      throw std::runtime_error("Failed to submit read task");
    }
  }

  ~RdmaReadTask() { doca_task_free(doca_rdma_task_read_as_task(task)); }

private:
  doca_rdma_task_read *task;
};

inline auto RdmaConnection::read(Buf &src, Buf &dst) {
  auto task = RdmaReadTask::allocate(rdma->get(), connection, src, dst);
  auto sender = rdma::task::rdma_sender<RdmaReadTask>{std::move(task)};
  return sender;
}

} // namespace doca_stdexec::rdma

#endif // DOCA_STDEXEC_RDMA_ONESIDE_HPP