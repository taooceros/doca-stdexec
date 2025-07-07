#include "doca_stdexec/buf.hpp"
#include "doca_stdexec/buf_inventory.hpp"
#include "doca_stdexec/mmap.hpp"
#include "stdexec/__detail/__let.hpp"
#include <doca_stdexec/common/tcp.hpp>
#include <doca_stdexec/device.hpp>
#include <doca_stdexec/progress_engine.hpp>
#include <doca_stdexec/rdma.hpp>
#include <exec/repeat_n.hpp>
#include <iostream>
#include <optional>
#include <stdexec/execution.hpp>
#include <vector>

#include <doca_log.h>

using namespace doca_stdexec::tcp;
using namespace doca_stdexec;

using namespace stdexec;

void client() {

  doca_stdexec::doca_pe_context context{};

  auto device = doca_stdexec::Device::open_from_ib_name("mlx5_0");

  auto rdma = doca_stdexec::rdma::Rdma::open_from_dev(device);

  rdma->set_gid_index(0);

  tcp_socket socket;
  socket.connect("127.0.0.1", 12345);

  printf("Client: Connected to server\n");

  auto work =
      context.connect_ctx(rdma) | stdexec::then([&]() { rdma->start(); }) |
      stdexec::let_value([&]() { return rdma->connect(socket); }) |
      stdexec::then([&](rdma::RdmaConnection &&connection) {
        printf("Client: Connected to server rdma\n");

        auto local_buf = std::vector<uint8_t>(1024);

        auto mmap = doca_stdexec::MMap<uint8_t>(std::span<uint8_t>(local_buf));

        mmap.add_device(device);
        mmap.set_permissions(DOCA_ACCESS_FLAG_LOCAL_READ_WRITE |
                             DOCA_ACCESS_FLAG_PCI_READ_WRITE |
                             DOCA_ACCESS_FLAG_RDMA_READ |
                             DOCA_ACCESS_FLAG_RDMA_WRITE);

        printf("client mmap %p %zu\n", mmap.get_memrange().data(),
               mmap.get_memrange().size_bytes());

        mmap.start();

        auto export_desc = mmap.export_rdma(*device);

        printf("Client: Sending export desc\n");

        socket.send_dynamic(export_desc);

        auto received_message = socket.receive_dynamic_string();

        if (received_message == "1") {
          std::cout << "Client: received message" << std::endl;
        } else {
          std::cout << "Client: unexpected message: " << received_message
                    << std::endl;
          exit(1);
        }

        for (auto i : local_buf) {
          printf("%d ", i);
        }
      });

  stdexec::sync_wait(std::move(work));
}

void server() {

  tcp_server server;
  server.listen(12345);

  auto socket = server.accept();

  printf("Server: Connected to client\n");

  doca_stdexec::doca_pe_context context{};

  auto device = doca_stdexec::Device::open_from_ib_name("mlx5_0");

  auto rdma = doca_stdexec::rdma::Rdma::open_from_dev(device);

  rdma->set_gid_index(0);

  std::optional<doca_stdexec::rdma::RdmaConnection> connection;
  std::optional<doca_stdexec::BufInventory> buf_inventory;

  std::optional<doca_stdexec::MMap<uint8_t>> src_mmap;
  std::optional<doca_stdexec::MMap<uint8_t>> dst_mmap;

  std::optional<doca_stdexec::Buf> src_buf;
  std::optional<doca_stdexec::Buf> dst_buf;

  auto work =
      context.connect_ctx(rdma) | stdexec::then([&]() { rdma->start(); }) |
      stdexec::let_value([&]() {
        printf("Server: Connecting to client rdma\n");
        return rdma->connect(socket);
      }) |
      stdexec::let_value([&](auto &connection_) {
        connection = std::move(connection_);

        printf("Server: Connected to client rdma\n");

        auto buffer = std::vector<uint8_t>(64);

        for (size_t i = 0; i < buffer.size(); i++) {
          buffer[i] = i;
        }

        src_mmap = doca_stdexec::MMap<uint8_t>(std::span<uint8_t>(buffer));

        src_mmap->add_device(device);
        src_mmap->set_permissions(DOCA_ACCESS_FLAG_LOCAL_READ_WRITE |
                                  DOCA_ACCESS_FLAG_PCI_READ_WRITE |
                                  DOCA_ACCESS_FLAG_RDMA_READ |
                                  DOCA_ACCESS_FLAG_RDMA_WRITE);

        src_mmap->start();

        // auto export_desc = mmap.export_rdma(device);

        printf("Server: Sending export desc\n");

        doca_data user_data;

        // socket.send_dynamic(export_desc);
        auto received_ctx = socket.receive_dynamic();

        printf("Server: Received export desc\n");

        dst_mmap = doca_stdexec::MMap<uint8_t>::create_from_export(
            &user_data, received_ctx.data(), received_ctx.size(), device);

        dst_mmap->start();

        printf("Server: Mapped export desc\n");

        buf_inventory = doca_stdexec::BufInventory(16);

        src_buf = buf_inventory->get_buffer_by_data(*src_mmap, buffer.data(),
                                                    buffer.size());

        dst_buf = buf_inventory->get_buffer_for_mmap(*dst_mmap);

        auto work = exec::repeat_n(connection->write(*src_buf, *dst_buf), 10);

        return work;
      });

  stdexec::sync_wait(std::move(work));
}

int main() {
  doca_log_backend *backend;
  auto status = doca_log_backend_create_with_fd_sdk(fileno(stdout), &backend);
  check_error(status, "Failed to create log backend");
  status = doca_log_backend_set_sdk_level(backend, DOCA_LOG_LEVEL_TRACE);
  check_error(status, "Failed to set default log backend");

  std::thread server_thread(server);
  std::thread client_thread(client);

  server_thread.join();
  client_thread.join();

  return 0;
}