#pragma once
#include "doca_stdexec/common/tcp.hpp"
#include <cstdint>
#include <cstdio>
#include <utility>
#ifndef DOCA_STDEXEC_RDMA_HPP
#define DOCA_STDEXEC_RDMA_HPP

#include "doca_stdexec/common.hpp"
#include "doca_stdexec/context.hpp"
#include <memory>
#include <span>

#include "buf.hpp"
#include "doca_stdexec/device.hpp"
#include "doca_stdexec/operation.hpp"
#include "rdma/task.hpp"
#include <doca_error.h>
#include <doca_rdma.h>
#include <stdexec/execution.hpp>

namespace doca_stdexec::rdma {

struct doca_rdma_deleter {
    void operator()(doca_rdma* rdma) {
        printf("Destroying rdma\n");
        doca_rdma_destroy(rdma);
    }
};

struct RdmaConnection;

struct rdma_connection_sender;

struct Rdma : public std::enable_shared_from_this<Rdma>, public Context {
    doca_ctx* as_ctx() noexcept override {
        return doca_rdma_as_ctx(rdma.get());
    }

    std::unique_ptr<doca_rdma, doca_rdma_deleter> rdma;
    std::shared_ptr<Device> dev;

    doca_rdma* get() const noexcept {
        return rdma.get();
    }

    explicit Rdma(doca_rdma* rdma, std::shared_ptr<Device> dev);

    static std::shared_ptr<Rdma> open_from_dev(std::shared_ptr<Device> dev) {
        doca_rdma* rdma;
        auto status = doca_rdma_create(dev->get(), &rdma);
        check_error(status, "Failed to create rdma");

        doca_rdma_set_permissions(
            rdma, DOCA_ACCESS_FLAG_LOCAL_READ_WRITE | DOCA_ACCESS_FLAG_RDMA_READ | DOCA_ACCESS_FLAG_RDMA_WRITE);

        return std::make_shared<Rdma>(rdma, std::move(dev));
    }

    void set_conf() {
        set_write_conf(16);
        set_read_conf(16);
        set_send_conf(16);
    }

    void set_write_conf(uint32_t num_tasks);
    void set_read_conf(uint32_t num_tasks);
    void set_send_conf(uint32_t num_tasks);

    void set_gid_index(uint32_t gid_index) {
        auto status = doca_rdma_set_gid_index(rdma.get(), gid_index);
        check_error(status, "Failed to set gid index");
    }

    auto export_ctx();

    rdma_connection_sender connect(tcp::tcp_socket& socket);

    ~Rdma() = default;

    inline auto recv(Buf& buf);
};

struct rdma_connection_deleter {
    void operator()(doca_rdma_connection* connection) {
        auto status = doca_rdma_connection_disconnect(connection);
        check_error(status, "Failed to disconnect rdma connection");
    }
};

struct RdmaConnection {
    std::unique_ptr<doca_rdma_connection, rdma_connection_deleter> connection;
    std::shared_ptr<Rdma> rdma;

    RdmaConnection(std::shared_ptr<Rdma> rdma, doca_rdma_connection* connection)
        : connection(connection), rdma(std::move(rdma)) {}

    ~RdmaConnection() = default;

    RdmaConnection(RdmaConnection&&) = default;
    RdmaConnection& operator=(RdmaConnection&&) = default;

    void set_user_data(doca_data data) {
        auto status = doca_rdma_connection_set_user_data(connection.get(), data);
        check_error(status, "Failed to set user data");
    }

    void connect(std::span<std::byte> ctx);

    inline auto write(Buf src, Buf dst);
    inline auto read(Buf src, Buf dst);
    inline auto send(Buf buf);
};

struct rdma_connection_sender {
    using sender_concept = stdexec::sender_t;

    RdmaConnection connection;
    std::vector<std::byte> ctx;

    using completion_signatures =
        stdexec::completion_signatures<stdexec::set_value_t(RdmaConnection), stdexec::set_error_t(doca_error_t),
                                       stdexec::set_stopped_t()>;

    static stdexec::env<> get_env() noexcept {
        return {};
    }

    template <typename Receiver>
    struct _operation : immovable {
        // type erased callbacks
        // put at the top to ensure correct offset
        doca_rdma_connection_established_cb_t request_cb = on_established;
        doca_rdma_connection_failure_cb_t failure_cb = on_failure;

        RdmaConnection connection;
        std::vector<std::byte> ctx;
        Receiver receiver;

        void start() noexcept {
            connection.set_user_data(doca_data{.ptr = this});
            connection.connect(ctx);
            receiver.set_value(std::move(connection));
        }

        static void on_established(doca_rdma_connection* connection, doca_data connection_data, doca_data ctx_data) {
            auto* op = static_cast<_operation*>(connection_data.ptr);
            op->receiver.set_value(std::move(op->connection));
        }

        static void on_failure(doca_rdma_connection* connection, doca_data connection_data, doca_data ctx_data) {
            auto* op = static_cast<_operation*>(connection_data.ptr);
            op->receiver.set_error(doca_error_t::DOCA_ERROR_UNKNOWN);
        }
    };

    template <typename Receiver>
    auto connect(Receiver receiver) {
        return _operation<Receiver>{
            .connection = std::move(connection), .ctx = std::move(ctx), .receiver = std::move(receiver)};
    }

    static inline void connection_on_established(doca_rdma_connection* connection, doca_data connection_data,
                                                 doca_data ctx_data) {
        printf("connection_on_established\n");
        auto* op = static_cast<_operation<uint8_t>*>(connection_data.ptr);
        op->request_cb(connection, connection_data, ctx_data);
    }

    static inline void connection_on_failure(doca_rdma_connection* connection, doca_data connection_data,
                                             doca_data ctx_data) {
        printf("connection_on_failure\n");
        auto* op = static_cast<_operation<uint8_t>*>(connection_data.ptr);
        op->failure_cb(connection, connection_data, ctx_data);
    }
};

inline void connection_request_cb(doca_rdma_connection* connection, doca_data connection_data) {
    printf("connection_request_cb\n");
}

inline void connection_disconnection_cb(doca_rdma_connection* connection, doca_data connection_data,
                                        doca_data ctx_data) {
    printf("connection_disconnection_cb\n");
}

inline static void rdma_state_changed_cb(doca_data data, doca_ctx* ctx, doca_ctx_states old_state,
                                         doca_ctx_states new_state) {
    switch (new_state) {
        case DOCA_CTX_STATE_IDLE:
            printf("rdma_state_changed_cb, idle\n");
            break;
        case DOCA_CTX_STATE_STARTING:
            printf("rdma_state_changed_cb, starting\n");
            break;
        case DOCA_CTX_STATE_RUNNING:
            printf("rdma_state_changed_cb, running\n");
            break;
        case DOCA_CTX_STATE_STOPPING:
            printf("rdma_state_changed_cb, stopping\n");
            break;
        default:
            printf("rdma_state_changed_cb, unknown state\n");
            break;
    }
}

inline Rdma::Rdma(doca_rdma* rdma, std::shared_ptr<Device> dev) : rdma(rdma), dev(std::move(dev)) {
    printf("rdma set connection state callbacks\n");

    auto status = doca_rdma_set_connection_state_callbacks(
        rdma, connection_request_cb, rdma_connection_sender::connection_on_established,
        rdma_connection_sender::connection_on_failure, connection_disconnection_cb);
    check_error(status, "Failed to set connection state callbacks");

    printf("set state changed cb\n");

    set_state_changed_cb(rdma_state_changed_cb);

    set_conf();
}

inline void rdma_state_changed_cb(doca_rdma* rdma, doca_data rdma_data) {
    printf("rdma_state_changed_cb\n");
}

inline auto Rdma::export_ctx() {
    const void* local_descriptor;
    size_t local_descriptor_size;
    doca_rdma_connection* local_connection;

    auto status = doca_rdma_export(rdma.get(), &local_descriptor, &local_descriptor_size, &local_connection);
    check_error(status, "Failed to export rdma ctx");

    auto ctx = std::span{reinterpret_cast<const std::byte*>(local_descriptor), local_descriptor_size};

    auto connection = RdmaConnection(shared_from_this(), local_connection);

    return std::make_tuple(ctx, std::move(connection));
}

inline void RdmaConnection::connect(std::span<std::byte> ctx) {
    printf("RdmaConnection::connect\n");
    auto status = doca_rdma_connect(rdma->get(), ctx.data(), ctx.size(), connection.get());
    check_error(status, "Failed to connect rdma");
}

inline rdma_connection_sender Rdma::connect(tcp::tcp_socket& socket) {
    auto [exported_ctx, connection] = export_ctx();

    socket.send_dynamic(exported_ctx);
    auto received_ctx = socket.receive_dynamic();

    return rdma_connection_sender{.connection = std::move(connection), .ctx = std::move(received_ctx)};
}

} // namespace doca_stdexec::rdma

#include "doca_stdexec/rdma/oneside.hpp"
#include "doca_stdexec/rdma/twoside.hpp"

namespace doca_stdexec::rdma {

inline void Rdma::set_write_conf(uint32_t num_tasks) {
    auto status = doca_rdma_task_write_set_conf(rdma.get(), task::rdma_operation_set_value<RdmaWriteTask>,
                                                task::rdma_operation_set_error<RdmaWriteTask>, num_tasks);
    check_error(status, "Failed to set write conf");
}

inline void Rdma::set_read_conf(uint32_t num_tasks) {
    auto status = doca_rdma_task_read_set_conf(rdma.get(), task::rdma_operation_set_value<RdmaReadTask>,
                                               task::rdma_operation_set_error<RdmaReadTask>, num_tasks);
    check_error(status, "Failed to set read conf");
}

inline void Rdma::set_send_conf(uint32_t num_tasks) {
    auto status = doca_rdma_task_send_set_conf(rdma.get(), task::rdma_operation_set_value<RdmaSendTask>,
                                               task::rdma_operation_set_error<RdmaSendTask>, num_tasks);
    check_error(status, "Failed to set send conf");
}

} // namespace doca_stdexec::rdma

#endif // DOCA_STDEXEC_RDMA_HPP