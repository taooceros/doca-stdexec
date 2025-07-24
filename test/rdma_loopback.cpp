#include "doca_stdexec/buf.hpp"
#include "doca_stdexec/buf_inventory.hpp"
#include "doca_stdexec/mmap.hpp"
#include "stdexec/__detail/__just.hpp"
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

    rdma->set_gid_index(1);

    tcp_socket socket;
    socket.connect("127.0.0.1", 12345);

    printf("Client: Connected to server\n");

    std::optional<doca_stdexec::rdma::RdmaConnection> connection;
    doca_stdexec::BufInventory buf_inventory = doca_stdexec::BufInventory(16);

    std::optional<doca_stdexec::MMap<uint8_t>> mmap;
    std::vector<uint8_t> local_buf(32768);
    std::optional<doca_stdexec::Buf> src_buf;

    auto work = schedule(context.get_scheduler()) | stdexec::then([&]() {
                    context.connect_ctx(rdma);
                    rdma->start();
                }) |
                stdexec::let_value([&]() { return rdma->connect(socket); }) |
                stdexec::then([&](rdma::RdmaConnection connection_) {
                    connection = std::move(connection_);

                    printf("Client: Connected to server rdma\n");

                    mmap = doca_stdexec::MMap<uint8_t>(std::span<uint8_t>(local_buf));

                    mmap->add_device(device);
                    mmap->set_permissions(DOCA_ACCESS_FLAG_LOCAL_READ_WRITE | DOCA_ACCESS_FLAG_RDMA_READ |
                                          DOCA_ACCESS_FLAG_RDMA_WRITE);

                    printf("client mmap %p %zu\n", mmap->get_memrange().data(), mmap->get_memrange().size_bytes());

                    mmap->start();

                    auto export_desc = mmap->export_rdma(*device);

                    printf("Client: Sending export desc\n\t");

                    socket.send_dynamic(export_desc);
                }) |
                upon_error([](std::variant<doca_error_t, std::exception_ptr> i) {
                    if (std::holds_alternative<doca_error_t>(i)) {
                        printf("Client: Error %d\n", std::get<doca_error_t>(i));
                    } else {
                        try {
                            std::rethrow_exception(std::get<std::exception_ptr>(i));
                        } catch (const std::exception& e) {
                            printf("Client: Error %s\n", e.what());
                        }
                    }
                });

    stdexec::sync_wait(work).value();

    src_buf = buf_inventory.get_buffer_for_mmap(*mmap);
    src_buf->set_data_len(32768);

    if (socket.receive_dynamic_string() != "1") {
        printf("Client: Unexpected message\n");
        exit(1);
    }

    for (auto i : local_buf) {
        printf("%d ", i);
        if (i > 10) {
            break;
        }
    }

    auto cleanup = stdexec::schedule(context.get_scheduler()) | stdexec::then([&]() { rdma->stop(); });
    stdexec::sync_wait(cleanup);

    printf("\n");
}

void server() {
    tcp_server server;
    server.listen(12345);

    auto socket = server.accept();

    printf("Server: Connected to client\n");

    doca_stdexec::doca_pe_context context{};

    auto device = doca_stdexec::Device::open_from_ib_name("mlx5_0");

    auto rdma = doca_stdexec::rdma::Rdma::open_from_dev(device);

    rdma->set_gid_index(1);

    std::optional<doca_stdexec::rdma::RdmaConnection> connection;
    std::optional<doca_stdexec::BufInventory> buf_inventory;

    std::optional<doca_stdexec::MMap<uint8_t>> src_mmap;
    std::optional<doca_stdexec::MMap<uint8_t>> dst_mmap;

    std::optional<doca_stdexec::Buf> src_buf;
    std::optional<doca_stdexec::Buf> dst_buf;

    auto work = schedule(context.get_scheduler()) | stdexec::then([&]() {
                    context.connect_ctx(rdma);
                    rdma->start();
                }) |
                stdexec::let_value([&]() {
                    printf("Server: Connecting to client rdma\n");
                    return rdma->connect(socket);
                }) |
                stdexec::let_value([&](auto& connection_) {
                    connection = std::move(connection_);

                    printf("Server: Connected to client rdma\n");

                    auto buffer = std::vector<uint8_t>(32768);

                    for (size_t i = 0; i < buffer.size(); i++) {
                        buffer[i] = i;
                    }

                    src_mmap = doca_stdexec::MMap<uint8_t>(std::span<uint8_t>(buffer));

                    src_mmap->add_device(device);
                    src_mmap->set_permissions(DOCA_ACCESS_FLAG_LOCAL_READ_WRITE | DOCA_ACCESS_FLAG_RDMA_READ |
                                              DOCA_ACCESS_FLAG_RDMA_WRITE);

                    src_mmap->start();

                    doca_data user_data{};

                    auto received_ctx = socket.receive_dynamic();

                    printf("Server: Received export desc\n\t");
                    for (auto i : received_ctx) {
                        printf("%d ", (int)i);
                    }
                    printf("\n");

                    dst_mmap = doca_stdexec::MMap<uint8_t>::create_from_export(&user_data, received_ctx.data(),
                                                                               received_ctx.size(), device);

                    printf("Server: Mapped export desc\n");

                    buf_inventory = doca_stdexec::BufInventory(16);

                    buf_inventory->start();
                    printf("Server: Started buf inventory\n");

                    src_buf = buf_inventory->get_buffer_for_mmap(*src_mmap);
                    src_buf->set_data_len(32768);

                    dst_buf = buf_inventory->get_buffer_by_addr(*dst_mmap, dst_mmap->get_memrange().data(),
                                                                dst_mmap->get_memrange().size_bytes());

                    dst_buf->set_data_len(0);

                    return connection->write(*src_buf, *dst_buf);
                }) |
                stdexec::upon_error([](std::variant<doca_error_t, std::exception_ptr> i) {
                    if (std::holds_alternative<doca_error_t>(i)) {
                        check_error(std::get<doca_error_t>(i), "Server: Error");
                    } else {
                        try {
                            std::rethrow_exception(std::get<std::exception_ptr>(i));
                        } catch (const std::exception& e) {
                            printf("Server: Error %s\n", e.what());
                        }
                    }
                }) |
                stdexec::then([&]() {
                    printf("Server: Writing done\n");

                    socket.send_dynamic("1");
                });

    stdexec::sync_wait(work);

    auto cleanup = stdexec::schedule(context.get_scheduler()) | stdexec::then([&]() { rdma->stop(); });

    stdexec::sync_wait(cleanup);
}

int main() {
    doca_log_backend* backend;
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