
#include "doca_stdexec/mmap.hpp"
#include "stdexec/__detail/__execution_fwd.hpp"
#include "stdexec/__detail/__start_detached.hpp"
#include <doca_stdexec/common/tcp.hpp>
#include <doca_stdexec/device.hpp>
#include <doca_stdexec/progress_engine.hpp>
#include <doca_stdexec/rdma.hpp>
#include <iostream>
#include <stdexec/execution.hpp>
#include <vector>

using namespace doca_stdexec::tcp;

void client() {

  doca_stdexec::doca_pe_context context{};

  auto device = doca_stdexec::Device::open_from_ib_name("mlx5_0");

  auto rdma = doca_stdexec::rdma::Rdma::open_from_dev(device.get());

  tcp_socket socket;
  socket.connect("127.0.0.1", 12345);

  auto connection = rdma.connect(socket);

  auto received_ctx = receive_message(socket);
}

void server() {

  tcp_server server;
  server.listen(12345);

  auto socket = server.accept();

  doca_stdexec::doca_pe_context context{};

  auto device = std::make_shared<doca_stdexec::Device>(
      doca_stdexec::Device::open_from_ib_name("mlx5_0"));

  auto rdma = doca_stdexec::rdma::Rdma::open_from_dev(device->get());

  auto connection = rdma.connect(socket);

  auto buffer = std::vector<uint8_t>(1024);

  auto mmap = doca_stdexec::MMap<uint8_t>(std::span<uint8_t>(buffer));

  mmap.add_device(device);
  mmap.set_permissions(
      DOCA_ACCESS_FLAG_LOCAL_READ_WRITE | DOCA_ACCESS_FLAG_PCI_READ_WRITE |
      DOCA_ACCESS_FLAG_RDMA_READ | DOCA_ACCESS_FLAG_RDMA_WRITE);

  mmap.start();

  auto export_desc = mmap.export_rdma(device);

  doca_data user_data;
  user_data.u64 = 0;

  socket.send_dynamic(export_desc);
  auto received_ctx = socket.receive_dynamic();

  auto mmap2 = doca_stdexec::MMap<uint8_t>::create_from_export(
      &user_data, received_ctx.data(), received_ctx.size(), device);

  mmap2.start();

  

}

int main() { return 0; }