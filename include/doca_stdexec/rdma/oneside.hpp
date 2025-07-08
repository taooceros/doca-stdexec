#pragma once
#include "doca_buf.h"
#ifndef DOCA_STDEXEC_RDMA_ONESIDE_HPP
#define DOCA_STDEXEC_RDMA_ONESIDE_HPP

#include "doca_stdexec/rdma.hpp"
#include <doca_pe.h>
#include <doca_rdma.h>
#include <stdexcept>
#include <stdexec/execution.hpp>

namespace doca_stdexec::rdma {

struct rdma_write_deleter {
  void operator()(doca_rdma_task_write *task) {
    printf("Destroying write task\n");
    doca_task_free(doca_rdma_task_write_as_task(task));
  }
};

struct RdmaWriteTask {
  using raw_type = doca_rdma_task_write;

public:
  RdmaWriteTask(doca_rdma_task_write *task) : task(task) {}

  RdmaWriteTask(RdmaWriteTask &&other) = default;

  static RdmaWriteTask allocate(doca_rdma *rdma, doca_rdma_connection *conn,
                                doca_buf *src, doca_buf *dst) {

    doca_error status;

    void *src_head;
    void *src_data;
    size_t src_data_len;
    status = doca_buf_get_head(src, &src_head);
    check_error(status, "Failed to get src head");
    status = doca_buf_get_data(src, &src_data);
    check_error(status, "Failed to get src data");
    status = doca_buf_get_data_len(src, &src_data_len);
    check_error(status, "Failed to get src data len");

    printf("src buf head %p, data %p,%zu\n", src_head, src_data, src_data_len);

    void *dst_head;
    void *dst_data;
    size_t dst_data_len;
    status = doca_buf_get_head(dst, &dst_head);
    check_error(status, "Failed to get dst head");
    status = doca_buf_get_data(dst, &dst_data);
    check_error(status, "Failed to get dst data");
    status = doca_buf_get_data_len(dst, &dst_data_len);
    check_error(status, "Failed to get dst data len");
    printf("dst buf head %p, data %p,%zu\n", dst_head, dst_data, dst_data_len);

    union doca_data user_data;
    user_data.u64 = 0;

    doca_rdma_task_write *task = nullptr;

    printf("Allocating write task: rdma %p, conn %p, src %p, dst %p\n", rdma,
           conn, src, dst);

    auto err = doca_rdma_task_write_allocate_init(rdma, conn, src, dst,
                                                  user_data, &task);
    if (err != DOCA_SUCCESS) {
      throw std::runtime_error("Failed to allocate write task");
    }

    return RdmaWriteTask(task);
  }

  doca_task *as_task() { return doca_rdma_task_write_as_task(task.get()); }

  void submit() {
    auto err = doca_task_submit(doca_rdma_task_write_as_task(task.get()));
    check_error(err, "Failed to submit write task");
  }

private:
  std::unique_ptr<doca_rdma_task_write, rdma_write_deleter> task;
};

inline auto RdmaConnection::write(Buf src, Buf dst) {
  auto sender = rdma::task::rdma_sender<RdmaWriteTask, doca_buf *, doca_buf *>{
      rdma->get(), connection.get(), std::make_tuple(src.get(), dst.get())};
  return sender;
}

struct RdmaReadTask {
  using raw_type = doca_rdma_task_read;

  RdmaReadTask(doca_rdma_task_read *task) : task(task) {}

  static RdmaReadTask allocate(doca_rdma *rdma, doca_rdma_connection *conn,
                               doca_buf *src, doca_buf *dst) {

    union doca_data user_data;
    user_data.u64 = 0;

    doca_rdma_task_read *task = nullptr;

    auto err = doca_rdma_task_read_allocate_init(rdma, conn, src, dst,
                                                 user_data, &task);
    if (err != DOCA_SUCCESS) {
      throw std::runtime_error("Failed to allocate read task");
    }

    return RdmaReadTask(task);
  }

  doca_task *as_task() { return doca_rdma_task_read_as_task(task); }

  void submit() {
    auto err = doca_task_submit(doca_rdma_task_read_as_task(task));
    check_error(err, "Failed to submit read task");
  }

  ~RdmaReadTask() { doca_task_free(doca_rdma_task_read_as_task(task)); }

private:
  doca_rdma_task_read *task;
};

inline auto RdmaConnection::read(Buf src, Buf dst) {
  auto sender = rdma::task::rdma_sender<RdmaReadTask, doca_buf *, doca_buf *>{
      rdma->get(), connection.get(), std::make_tuple(src.get(), dst.get())};
  return sender;
}

} // namespace doca_stdexec::rdma

#endif // DOCA_STDEXEC_RDMA_ONESIDE_HPP