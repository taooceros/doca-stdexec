#pragma once
#include "doca_stdexec/common/tcp.hpp"
#include <cstdint>
#include <utility>
#ifndef DOCA_STDEXEC_RDMA_HPP
#define DOCA_STDEXEC_RDMA_HPP

#include "doca_stdexec/common.hpp"
#include <memory>
#include <span>

#include "buf.hpp"
#include "doca_stdexec/operation.hpp"
#include "rdma/task.hpp"
#include <doca_rdma.h>
#include <stdexec/execution.hpp>

namespace doca_stdexec::rdma {

struct doca_rdma_deleter {
  void operator()(doca_rdma *rdma) { doca_rdma_destroy(rdma); }
};

struct RdmaConnection;

struct Rdma : public std::enable_shared_from_this<Rdma> {
  doca_ctx *as_ctx() noexcept { return doca_rdma_as_ctx(rdma.get()); }

  std::unique_ptr<doca_rdma, doca_rdma_deleter> rdma;

  doca_rdma *get() noexcept { return rdma.get(); }

  Rdma(doca_rdma *rdma) : rdma(rdma) {}

  static Rdma open_from_dev(doca_dev *dev) {
    doca_rdma *rdma;
    auto status = doca_rdma_create(dev, &rdma);
    check_error(status, "Failed to create rdma: %d\n");
    return Rdma(rdma);
  }

  auto export_ctx();

  RdmaConnection connect(tcp::tcp_socket &socket);

  ~Rdma() = default;

  inline auto recv(Buf &buf);
};

struct RdmaConnection {
  doca_rdma_connection *connection;
  std::shared_ptr<Rdma> rdma;

  RdmaConnection(std::shared_ptr<Rdma> rdma, doca_rdma_connection *connection);

  ~RdmaConnection();

  RdmaConnection(const RdmaConnection &) = delete;
  RdmaConnection &operator=(const RdmaConnection &) = delete;
  RdmaConnection(RdmaConnection &&) = default;
  void connect(std::span<std::byte> ctx);

  inline auto write(Buf &src, Buf &dst);
  inline auto read(Buf &src, Buf &dst);
  inline auto send(Buf &buf);
};

inline auto Rdma::export_ctx() {

  const void *local_descriptor;
  size_t local_descriptor_size;
  doca_rdma_connection *local_connection;

  auto status = doca_rdma_export(rdma.get(), &local_descriptor,
                                 &local_descriptor_size, &local_connection);
  check_error(status, "Failed to export rdma ctx: %d\n");

  auto connection = RdmaConnection(shared_from_this(), local_connection);

  return std::make_pair(
      std::span<std::byte>{reinterpret_cast<std::byte *>(local_connection),
                           local_descriptor_size},
      std::move(connection));
}

inline void RdmaConnection::connect(std::span<std::byte> ctx) {
  auto status =
      doca_rdma_connect(rdma->get(), ctx.data(), ctx.size(), connection);
  check_error(status, "Failed to connect rdma: %d\n");
}

inline RdmaConnection Rdma::connect(tcp::tcp_socket &socket) {
  auto [exported_ctx, connection] = export_ctx();
  send_message(socket, exported_ctx);
  auto received_ctx = receive_message(socket);
  connection.connect(received_ctx);
  return std::move(connection);
}

} // namespace doca_stdexec::rdma

#include "doca_stdexec/rdma/oneside.hpp"
#include "doca_stdexec/rdma/twoside.hpp"
#endif // DOCA_STDEXEC_RDMA_HPP