#pragma once

#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <optional>
#include <memory>
#include <vector>
#include <cstdint>

#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <netdb.h>

namespace doca_stdexec::tcp {

class socket_error : public std::system_error {
public:
    socket_error(int code, const std::string& what)
        : std::system_error(code, std::system_category(), what) {}
    
    socket_error(const std::string& what)
        : std::system_error(errno, std::system_category(), what) {}
};

class tcp_socket {
private:
    int fd_;
    bool connected_;
    
    // Helper to set socket to non-blocking mode
    void set_non_blocking(bool non_blocking = true) {
        int flags = fcntl(fd_, F_GETFL, 0);
        if (flags == -1) {
            throw socket_error("Failed to get socket flags");
        }
        
        if (non_blocking) {
            flags |= O_NONBLOCK;
        } else {
            flags &= ~O_NONBLOCK;
        }
        
        if (fcntl(fd_, F_SETFL, flags) == -1) {
            throw socket_error("Failed to set socket flags");
        }
    }

public:
    tcp_socket() : fd_(-1), connected_(false) {}
    
    explicit tcp_socket(int fd) : fd_(fd), connected_(true) {}
    
    ~tcp_socket() {
        close();
    }
    
    // Move constructor
    tcp_socket(tcp_socket&& other) noexcept 
        : fd_(other.fd_), connected_(other.connected_) {
        other.fd_ = -1;
        other.connected_ = false;
    }
    
    // Move assignment
    tcp_socket& operator=(tcp_socket&& other) noexcept {
        if (this != &other) {
            close();
            fd_ = other.fd_;
            connected_ = other.connected_;
            other.fd_ = -1;
            other.connected_ = false;
        }
        return *this;
    }
    
    // Disable copy
    tcp_socket(const tcp_socket&) = delete;
    tcp_socket& operator=(const tcp_socket&) = delete;
    
    // Create and connect to server
    void connect(const std::string& host, std::uint16_t port) {
        if (connected_) {
            throw socket_error("Socket already connected");
        }
        
        fd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (fd_ == -1) {
            throw socket_error("Failed to create socket");
        }
        
        struct sockaddr_in server_addr{};
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(port);
        
        // Try to convert IP address directly first
        if (inet_pton(AF_INET, host.c_str(), &server_addr.sin_addr) != 1) {
            // If that fails, try hostname resolution
            struct hostent* he = gethostbyname(host.c_str());
            if (he == nullptr) {
                ::close(fd_);
                fd_ = -1;
                throw socket_error("Failed to resolve hostname: " + host);
            }
            server_addr.sin_addr = *reinterpret_cast<struct in_addr*>(he->h_addr);
        }
        
        if (::connect(fd_, reinterpret_cast<struct sockaddr*>(&server_addr), sizeof(server_addr)) == -1) {
            ::close(fd_);
            fd_ = -1;
            throw socket_error("Failed to connect to " + host + ":" + std::to_string(port));
        }
        
        connected_ = true;
    }
    
    // Close socket
    void close() {
        if (fd_ != -1) {
            ::close(fd_);
            fd_ = -1;
            connected_ = false;
        }
    }
    
    // Check if socket is connected
    bool is_connected() const {
        return connected_ && fd_ != -1;
    }
    
    // Get raw file descriptor
    int native_handle() const {
        return fd_;
    }
    
    // Send data with span - returns number of bytes sent
    std::size_t send(std::span<const std::byte> data) {
        if (!is_connected()) {
            throw socket_error("Socket not connected");
        }
        
        ssize_t result = ::send(fd_, data.data(), data.size(), MSG_NOSIGNAL);
        if (result == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return 0; // Would block, return 0 bytes sent
            }
            connected_ = false;
            throw socket_error("Failed to send data");
        }
        
        return static_cast<std::size_t>(result);
    }
    
    // Send all data in span - blocks until all data is sent
    void send_all(std::span<const std::byte> data) {
        std::size_t total_sent = 0;
        while (total_sent < data.size()) {
            std::size_t sent = send(data.subspan(total_sent));
            if (sent == 0) {
                // Would block or connection closed
                throw socket_error("Connection closed during send_all");
            }
            total_sent += sent;
        }
    }
    
    // Convenience method for string data
    std::size_t send(std::string_view str) {
        auto bytes = std::as_bytes(std::span{str});
        return send(bytes);
    }
    
    void send_all(std::string_view str) {
        auto bytes = std::as_bytes(std::span{str});
        send_all(bytes);
    }
    
    // Receive data into span - returns number of bytes received
    std::size_t receive(std::span<std::byte> buffer) {
        if (!is_connected()) {
            throw socket_error("Socket not connected");
        }
        
        ssize_t result = ::recv(fd_, buffer.data(), buffer.size(), 0);
        if (result == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return 0; // Would block, return 0 bytes received
            }
            connected_ = false;
            throw socket_error("Failed to receive data");
        } else if (result == 0) {
            // Connection closed by peer
            connected_ = false;
            return 0;
        }
        
        return static_cast<std::size_t>(result);
    }
    
    // Receive exactly the requested amount of data
    void receive_all(std::span<std::byte> buffer) {
        std::size_t total_received = 0;
        while (total_received < buffer.size()) {
            std::size_t received = receive(buffer.subspan(total_received));
            if (received == 0) {
                throw socket_error("Connection closed during receive_all");
            }
            total_received += received;
        }
    }
    
    // Send data with length prefix (4-byte length + payload)
    void send_dynamic(std::span<const std::byte> data) {
        std::uint32_t size = static_cast<std::uint32_t>(data.size());
        std::uint32_t network_size = htonl(size);
        
        // Send length prefix first
        send_all(std::as_bytes(std::span{&network_size, 1}));
        // Then send the payload
        send_all(data);
    }
    
    void send_dynamic(std::string_view str) {
        auto bytes = std::as_bytes(std::span{str});
        send_dynamic(bytes);
    }
    
    // Receive data with length prefix - reads length first, then exact payload
    std::vector<std::byte> receive_dynamic(std::size_t max_size = 1024 * 1024 * 100) {
        // First, read the 4-byte length prefix
        std::uint32_t network_size;
        receive_all(std::as_writable_bytes(std::span{&network_size, 1}));
        
        std::uint32_t size = ntohl(network_size);
        if (size > max_size) {
            throw socket_error("Message too large: " + std::to_string(size) + 
                             " bytes (max: " + std::to_string(max_size) + ")");
        }
        
        if (size == 0) {
            return {}; // Empty message
        }
        
        // Now read exactly 'size' bytes of payload
        std::vector<std::byte> buffer(size);
        receive_all(std::span{buffer});
        return buffer;
    }
    
    // Receive dynamic message as string
    std::string receive_dynamic_string(std::size_t max_size = 1024 * 1024 * 100) {
        auto bytes = receive_dynamic(max_size);
        return std::string{reinterpret_cast<const char*>(bytes.data()), bytes.size()};
    }
    
    // Set socket options
    void set_reuse_addr(bool enable = true) {
        if (fd_ == -1) {
            throw socket_error("Invalid socket");
        }
        
        int opt = enable ? 1 : 0;
        if (setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
            throw socket_error("Failed to set SO_REUSEADDR");
        }
    }
    
    void set_keep_alive(bool enable = true) {
        if (fd_ == -1) {
            throw socket_error("Invalid socket");
        }
        
        int opt = enable ? 1 : 0;
        if (setsockopt(fd_, SOL_SOCKET, SO_KEEPALIVE, &opt, sizeof(opt)) == -1) {
            throw socket_error("Failed to set SO_KEEPALIVE");
        }
    }
    
    void set_no_delay(bool enable = true) {
        if (fd_ == -1) {
            throw socket_error("Invalid socket");
        }
        
        int opt = enable ? 1 : 0;
        if (setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt)) == -1) {
            throw socket_error("Failed to set TCP_NODELAY");
        }
    }
};

class tcp_server {
private:
    int listen_fd_;
    std::uint16_t port_;
    bool listening_;

public:
    tcp_server() : listen_fd_(-1), port_(0), listening_(false) {}
    
    ~tcp_server() {
        stop();
    }
    
    // Move constructor
    tcp_server(tcp_server&& other) noexcept 
        : listen_fd_(other.listen_fd_), port_(other.port_), listening_(other.listening_) {
        other.listen_fd_ = -1;
        other.port_ = 0;
        other.listening_ = false;
    }
    
    // Move assignment
    tcp_server& operator=(tcp_server&& other) noexcept {
        if (this != &other) {
            stop();
            listen_fd_ = other.listen_fd_;
            port_ = other.port_;
            listening_ = other.listening_;
            other.listen_fd_ = -1;
            other.port_ = 0;
            other.listening_ = false;
        }
        return *this;
    }
    
    // Disable copy
    tcp_server(const tcp_server&) = delete;
    tcp_server& operator=(const tcp_server&) = delete;
    
    // Start listening on specified port
    void listen(std::uint16_t port, int backlog = 128) {
        if (listening_) {
            throw socket_error("Server already listening");
        }
        
        listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (listen_fd_ == -1) {
            throw socket_error("Failed to create listen socket");
        }
        
        // Set SO_REUSEADDR
        int opt = 1;
        if (setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
            ::close(listen_fd_);
            listen_fd_ = -1;
            throw socket_error("Failed to set SO_REUSEADDR");
        }
        
        struct sockaddr_in server_addr{};
        server_addr.sin_family = AF_INET;
        server_addr.sin_addr.s_addr = INADDR_ANY;
        server_addr.sin_port = htons(port);
        
        if (bind(listen_fd_, reinterpret_cast<struct sockaddr*>(&server_addr), sizeof(server_addr)) == -1) {
            ::close(listen_fd_);
            listen_fd_ = -1;
            throw socket_error("Failed to bind to port " + std::to_string(port));
        }
        
        if (::listen(listen_fd_, backlog) == -1) {
            ::close(listen_fd_);
            listen_fd_ = -1;
            throw socket_error("Failed to listen on port " + std::to_string(port));
        }
        
        port_ = port;
        listening_ = true;
    }
    
    // Accept incoming connection
    tcp_socket accept() {
        if (!listening_) {
            throw socket_error("Server not listening");
        }
        
        struct sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        
        int client_fd = ::accept(listen_fd_, reinterpret_cast<struct sockaddr*>(&client_addr), &client_len);
        if (client_fd == -1) {
            throw socket_error("Failed to accept connection");
        }
        
        return tcp_socket(client_fd);
    }
    
    // Stop listening
    void stop() {
        if (listen_fd_ != -1) {
            ::close(listen_fd_);
            listen_fd_ = -1;
            listening_ = false;
            port_ = 0;
        }
    }
    
    // Check if server is listening
    bool is_listening() const {
        return listening_;
    }
    
    // Get listening port
    std::uint16_t port() const {
        return port_;
    }
    
    // Get raw file descriptor
    int native_handle() const {
        return listen_fd_;
    }
};

// Utility functions for common patterns

// Convenience aliases for the dynamic methods
inline void send_message(tcp_socket& socket, std::span<const std::byte> data) {
    socket.send_dynamic(data);
}

inline void send_message(tcp_socket& socket, std::string_view str) {
    socket.send_dynamic(str);
}

inline std::vector<std::byte> receive_message(tcp_socket& socket, std::size_t max_size = 1024 * 1024 * 100) {
    return socket.receive_dynamic(max_size);
}

inline std::string receive_message_string(tcp_socket& socket, std::size_t max_size = 1024 * 1024 * 100) {
    return socket.receive_dynamic_string(max_size);
}

// Convert byte vector to string
inline std::string bytes_to_string(const std::vector<std::byte>& bytes) {
    return std::string{reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

} // namespace doca_stdexec::tcp
