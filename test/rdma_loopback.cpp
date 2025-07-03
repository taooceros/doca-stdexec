
#include "doca_stdexec/buf.hpp"
#include "doca_stdexec/mmap.hpp"
#include "stdexec/__detail/__execution_fwd.hpp"
#include "stdexec/__detail/__start_detached.hpp"
#include "stdexec/__detail/__sync_wait.hpp"
#include "stdexec/__detail/__then.hpp"
#include <doca_stdexec/common/tcp.hpp>
#include <doca_stdexec/device.hpp>
#include <doca_stdexec/progress_engine.hpp>
#include <doca_stdexec/rdma.hpp>
#include <iostream>
#include <stdexec/execution.hpp>
#include <vector>

using namespace doca_stdexec::tcp;
using namespace doca_stdexec;

void client() {

  doca_stdexec::doca_pe_context context{};

  auto device = doca_stdexec::Device::open_from_ib_name("mlx5_0");

  auto rdma = doca_stdexec::rdma::Rdma::open_from_dev(device.get());

  context.get_pe().connect_ctx(*rdma);

  rdma->start();

  tcp_socket socket;
  socket.connect("127.0.0.1", 12345);

  printf("Client: Connected to server\n");  

  auto connection = rdma->connect(socket);

  printf("Client: Connected to server rdma\n");

  auto local_buf = std::vector<uint8_t>(1024);

  auto mmap = doca_stdexec::MMap<uint8_t>(std::span<uint8_t>(local_buf));

  mmap.add_device(device.shared_from_this());
  mmap.set_permissions(
      DOCA_ACCESS_FLAG_LOCAL_READ_WRITE | DOCA_ACCESS_FLAG_PCI_READ_WRITE |
      DOCA_ACCESS_FLAG_RDMA_READ | DOCA_ACCESS_FLAG_RDMA_WRITE);

  auto export_desc = mmap.export_rdma(device);

  printf("Client: Sending export desc\n");

  socket.send_dynamic(export_desc);

  printf("Client: Received message\n");

  auto received_message = socket.receive_dynamic_string();

  printf("Client: Received message\n");

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
}

void server() {

  tcp_server server;
  server.listen(12345);

  auto socket = server.accept();

  printf("Server: Connected to client\n");

  doca_stdexec::doca_pe_context context{};

  auto device = doca_stdexec::Device::open_from_ib_name("mlx5_0");

  auto rdma = doca_stdexec::rdma::Rdma::open_from_dev(device.get());

  context.get_pe().connect_ctx(*rdma);

  rdma->start();

  auto connection = rdma->connect(socket);

  printf("Server: Connected to client rdma\n");

  auto buffer = std::vector<uint8_t>(1024);

  auto mmap = doca_stdexec::MMap<uint8_t>(std::span<uint8_t>(buffer));

  mmap.add_device(device.shared_from_this());
  mmap.set_permissions(
      DOCA_ACCESS_FLAG_LOCAL_READ_WRITE | DOCA_ACCESS_FLAG_PCI_READ_WRITE |
      DOCA_ACCESS_FLAG_RDMA_READ | DOCA_ACCESS_FLAG_RDMA_WRITE);

  mmap.start();

  // auto export_desc = mmap.export_rdma(device);

  printf("Server: Sending export desc\n");

  doca_data user_data;
  user_data.u64 = 0;

  // socket.send_dynamic(export_desc);
  auto received_ctx = socket.receive_dynamic();

  printf("Server: Received export desc\n");

  auto mmap2 = doca_stdexec::MMap<uint8_t>::create_from_export(
      &user_data, received_ctx.data(), received_ctx.size(),
      device.shared_from_this());

  mmap2.start();

  printf("Server: Mapped export desc\n");

  auto buf1 = Buf();
  auto buf2 = Buf();

  auto scheduler = context.get_scheduler();

  auto sender = scheduler.schedule() |
                stdexec::then([&]() { connection.write(buf1, buf2); });

  auto result = stdexec::sync_wait(std::move(sender));

  socket.send("1");
}

int main() {

  std::thread server_thread(server);
  std::thread client_thread(client);

  server_thread.join();
  client_thread.join();

  return 0;
}