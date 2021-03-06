#include "server/TCPServer.h"

#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <unistd.h>
#include <string.h>
#include <map>
#include <vector>
#include <algorithm>
#include <iostream>
#include <cstdint>

#include "MemoryBackend.h"
#include "NVStorageBackend.h"

#include "utils/logging.h"
#include "common/Exception.h"
#include "common/schemas/TCPBladeMessage_generated.h"

namespace cirrus {

using TxnID = uint64_t;

// size for Flatbuffer's buffer
static const int initial_buffer_size = 50;

/**
  * Constructor for the server. Given a port and queue length, sets the values
  * of the variables.
  * @param port the port the server will listen on
  * @param pool_size_ the number of bytes to have in the memory pool.
  * @param backend the Type of backend: "Memory" or "Storage"
  * @param storage_path Path to disk storage. Used when backend is "Storage"
  * @param max_fds_ the maximum number of clients that can be connected to the
  * server at the same time.
  */
TCPServer::TCPServer(int port, uint64_t pool_size_,
                     const std::string& backend,
                     const std::string& storage_path,
                     uint64_t max_fds_) :
    port_(port), pool_size(pool_size_), max_fds(max_fds_ + 1) {
    if (max_fds_ + 1 == 0) {
        throw cirrus::Exception("Max_fds value too high, "
            "overflow occurred.");
    }

    if (backend == "Memory") {
        mem = std::make_unique<MemoryBackend>(100'000'000);
    } else if (backend == "Storage") {
        mem = std::make_unique<NVStorageBackend>(storage_path);
    } else {
        throw std::runtime_error("Wrong backend option");
    }

    mem->init();  // initialize memory backend
}

/**
  * Initializer for the server. Sets up the socket it uses to listen for
  * incoming connections.
  */
void TCPServer::init() {
    server_sock_ = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock_ < 0) {
        throw cirrus::ConnectionException("Server error creating socket");
    }

    LOG<INFO>("Created socket in TCPServer");

    int opt = 1;
    if (setsockopt(server_sock_, SOL_SOCKET, SO_REUSEADDR, &opt,
                   sizeof(opt))) {
        switch (errno) {
            case EBADF:
                LOG<ERROR>("EBADF");
                break;
            case ENOTSOCK:
                LOG<ERROR>("ENOTSOCK");
                break;
            case ENOPROTOOPT:
                LOG<ERROR>("ENOPROTOOPT");
                break;
            case EFAULT:
                LOG<ERROR>("EFAULT");
                break;
            case EDOM:
                LOG<ERROR>("EDOM");
                break;
        }
        throw cirrus::ConnectionException("Error forcing port binding");
    }

    if (setsockopt(server_sock_, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt))) {
        throw cirrus::ConnectionException("Error setting socket options.");
    }

    if (setsockopt(server_sock_, SOL_SOCKET, SO_REUSEPORT, &opt,
                   sizeof(opt))) {
        switch (errno) {
            case EBADF:
                LOG<ERROR>("EBADF");
                break;
            case ENOTSOCK:
                LOG<ERROR>("ENOTSOCK");
                break;
            case ENOPROTOOPT:
                LOG<ERROR>("ENOPROTOOPT");
                break;
            case EFAULT:
                LOG<ERROR>("EFAULT");
                break;
            case EDOM:
                LOG<ERROR>("EDOM");
                break;
        }
        throw cirrus::ConnectionException("Error forcing port binding");
    }

    struct sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(port_);
    std::memset(serv_addr.sin_zero, 0, sizeof(serv_addr.sin_zero));

    int ret = bind(server_sock_, reinterpret_cast<sockaddr*>(&serv_addr),
            sizeof(serv_addr));
    if (ret < 0) {
        throw cirrus::ConnectionException("Error binding in port "
               + to_string(port_));
    }

    // SOMAXCONN is the "max reasonable backlog size" defined in socket.h
    if (listen(server_sock_, SOMAXCONN) == -1) {
        throw cirrus::ConnectionException("Error listening on port "
            + to_string(port_));
    }

    fds.at(curr_index).fd = server_sock_;
    // Only listen for data to read
    fds.at(curr_index++).events = POLLIN;
}

/**
 * Function passed into std::remove_if
 * @param x a struct pollfd being examined
 * @return True if the struct should be removed. This is true if the fd is -1,
 * meaning that it is being ignored.
 */
bool TCPServer::testRemove(struct pollfd x) {
    // If this pollfd will be removed, the index of the next location to insert
    // should be reduced by one correspondingly.
    if (x.fd == -1) {
        curr_index -= 1;
    }
    return x.fd == -1;
}
/**
  * Server processing loop. When called, server loops infinitely, accepting
  * new connections and acting on messages received.
  */
void TCPServer::loop() {
    struct sockaddr_in cli_addr;
    socklen_t clilen = sizeof(cli_addr);

    while (1) {
        LOG<INFO>("Server calling poll.");
        int poll_status = poll(fds.data(), curr_index, timeout);
        LOG<INFO>("Poll returned with status: ", poll_status);

        if (poll_status == -1) {
            throw cirrus::ConnectionException("Server error calling poll.");
        } else if (poll_status == 0) {
            LOG<INFO>(timeout, " milliseconds elapsed without contact.");
        } else {
            // there is at least one pending event, find it.
            for (uint64_t i = 0; i < curr_index; i++) {
                struct pollfd& curr_fd = fds.at(i);
                // Ignore the fd if we've said we don't care about it
                if (curr_fd.fd == -1) {
                    continue;
                }
                if (curr_fd.revents != POLLIN) {
                    LOG<ERROR>("Non read event on socket: ", curr_fd.fd);
                    if (curr_fd.revents & POLLHUP) {
                        LOG<INFO>("Connection was closed by client");
                        LOG<INFO>("Closing socket: ", curr_fd.fd);
                        close(curr_fd.fd);
                        curr_fd.fd = -1;
                    }

                } else if (curr_fd.fd == server_sock_) {
                    LOG<INFO>("New connection incoming");

                    // New data on main socket, accept and connect
                    // TODO(Tyler): loop this to accept multiple at once?
                    // TODO(Tyler): Switch to non blocking sockets?
                    int newsock = accept(server_sock_,
                            reinterpret_cast<struct sockaddr*>(&cli_addr),
                            &clilen);
                    if (newsock < 0) {
                        throw std::runtime_error("Error accepting socket");
                    }
                    // If at capacity, reject connection
                    if (curr_index == max_fds) {
                        close(newsock);
                    } else {
                        LOG<INFO>("Created new socket: ", newsock);
                        fds.at(curr_index).fd = newsock;
                        fds.at(curr_index).events = POLLIN;
                        curr_index++;
                    }
                } else {
                    if (!process(curr_fd.fd)) {
                        LOG<INFO>("Processing failed on socket: ", curr_fd.fd);
                        // do not make future alerts on this fd
                        curr_fd.fd = -1;
                    }
                }
                curr_fd.revents = 0;  // Reset the event flags
            }
        }
        // If at max capacity, try to make room
        if (curr_index == max_fds) {
            // Try to purge unused fds, those with fd == -1
            std::remove_if(fds.begin(), fds.end(),
                std::bind(&TCPServer::testRemove, this,
                    std::placeholders::_1));
        }
    }
}

/**
 * Guarantees that an entire message is sent.
 * @param sock the fd of the socket to send on.
 * @param data a pointer to the data to send.
 * @param len the number of bytes to send.
 * @param flags for the send call.
 * @return the number of bytes sent.
 */
ssize_t TCPServer::send_all(int sock, const void* data, size_t len,
    int /* flags */) {
    uint64_t to_send = len;
    uint64_t total_sent = 0;
    int64_t sent = 0;

    while (to_send != total_sent) {
        sent = send(sock, data, len - total_sent, 0);

        if (sent == -1) {
            LOG<ERROR>("Server error sending data to client, "
                "possible client died");
            return -1;
        }

        total_sent += sent;

        // Increment the pointer to data we're sending by the amount just sent
        data = static_cast<const char*>(data) + sent;
    }

    return total_sent;
}

/**
 * Guarantees that an entire message is read.
 * @param sock the fd of the socket to read on.
 * @param data a pointer to the buffer to read into.
 * @param len the number of bytes to read.
 * @return the number of bytes sent.
 */
ssize_t TCPServer::read_all(int sock, void* data, size_t len) {
    uint64_t bytes_read = 0;

    while (bytes_read < len) {
        int64_t retval = read(sock, reinterpret_cast<char*>(data) + bytes_read,
            len - bytes_read);

        if (retval == -1) {
            char *error = strerror(errno);
            LOG<ERROR>(error);
            throw cirrus::Exception("Error reading from client");
        }

        bytes_read += retval;
    }

    return bytes_read;
}

/**
  * Read header from client's message or return false if client has disconnected
  * @param buffer Buffer where data is stored
  * @param sock Socket used for communication
  * @param bytes_read Keeps track of how many bytes have been read
  * @param Return false if client disconnected, true otherwise
  */
bool TCPServer::read_from_client(
        std::vector<char>& buffer, int sock, uint64_t& bytes_read) {
    bool first_loop = true;
    while (bytes_read < static_cast<int>(sizeof(uint32_t))) {
        int retval = read(sock, buffer.data() + bytes_read,
                          sizeof(uint32_t) - bytes_read);

        if (first_loop && retval == 0) {
            // Socket is closed by client if 0 bytes are available
            close(sock);
            LOG<INFO>("Closing socket: ", sock);
            return false;
        }
        if (retval < 0) {
            throw cirrus::Exception("Server issue in reading "
                                    "socket during size read.");
        }

        bytes_read += retval;
        first_loop = false;
    }
    return true;
}

int64_t checksum(const std::vector<int8_t>& data) {
    int64_t sum = 0;
    for (const auto& d : data) {
        sum += d;
    }
    return sum;
}

/**
 * Process the message incoming on a particular socket. Reads in the message
 * from the socket, extracts the flatbuffer, and then acts depending on
 * the type of the message.
 * @param sock the file descriptor for the socket with an incoming message.
 */
bool TCPServer::process(int sock) {
    LOG<INFO>("Processing socket: ", sock);
    std::vector<char> buffer;

    // Read in the incoming message

    // Reserve the size of a 32 bit int
    uint64_t current_buf_size = sizeof(uint32_t);
    buffer.reserve(current_buf_size);
    uint64_t bytes_read = 0;

    bool ret = read_from_client(buffer, sock, bytes_read);
    if (!ret) {
        return false;
    }

    LOG<INFO>("Server received size from client");
    // Convert to host byte order
    uint32_t* incoming_size_ptr = reinterpret_cast<uint32_t*>(buffer.data());
    uint32_t incoming_size = ntohl(*incoming_size_ptr);
    LOG<INFO>("Server received incoming size of ", incoming_size);

    // Resize the buffer to be larger if necessary
#ifdef PERF_LOG
    TimerFunction resize_time;
#endif
    if (incoming_size > current_buf_size) {
        // We use reserve() in place of resize() as it does not initialize the
        // memory that it allocates. This is safe, but buffer.capacity() must
        // be used to find the length of the buffer rather than buffer.size()
        buffer.reserve(incoming_size);
    }
#ifdef PERF_LOG
    LOG<PERF>("TCPServer::process resize time (us): ",
            resize_time.getUsElapsed());
#endif


#ifdef PERF_LOG
    TimerFunction receive_time;
#endif

    read_all(sock, buffer.data(), incoming_size);

#ifdef PERF_LOG
    double recv_mbps = bytes_read / (1024.0 * 1024) /
        (receive_time.getUsElapsed() / 1000000.0);
    LOG<PERF>("TCPServer::process receive time (us): ",
            receive_time.getUsElapsed(),
            " bw (MB/s): ", recv_mbps);
#endif
    LOG<INFO>("Server received full message from client");

    // Extract the message from the buffer
    auto msg = message::TCPBladeMessage::GetTCPBladeMessage(buffer.data());
    TxnID txn_id = msg->txnid();
    // Instantiate the builder
    flatbuffers::FlatBufferBuilder builder(initial_buffer_size);

    // Initialize the error code
    cirrus::ErrorCodes error_code = cirrus::ErrorCodes::kOk;

    LOG<INFO>("Server checking type of message");
    // Check message type
    bool success = true;
    switch (msg->message_type()) {
        case message::TCPBladeMessage::Message_Write:
            {
#ifdef PERF_LOG
                TimerFunction write_time;
#endif
                // first see if the object exists on the server.
                // If so, overwrite it and account for the size change.
                ObjectID oid = msg->message_as_Write()->oid();
                LOG<INFO>("Server processing WRITE request to oid: .", oid);

                // update current used size
                // XXX maybe tracking this size should be done by the backend
                if (mem->exists(oid)) {
                    curr_size -= mem->size(oid);
                }

                // Throw error if put would exceed size of the store
                auto data_fb = msg->message_as_Write()->data();
                if (curr_size + data_fb->size() > pool_size) {
                    LOG<ERROR>("Put would go over capacity on server. ",
                                "Current size: ", curr_size,
                                " Incoming size: ", data_fb->size(),
                                " Pool size: ", pool_size);
                    error_code =
                        cirrus::ErrorCodes::kServerMemoryErrorException;
                    success = false;
                } else {
                    // Service the write request by
                    // storing the serialized object
                    mem->put(oid, MemSlice(data_fb));

                    curr_size += data_fb->size();
                }

                // Create and send ack
                auto ack = message::TCPBladeMessage::CreateWriteAck(builder,
                                           oid, success);
                auto ack_msg =
                     message::TCPBladeMessage::CreateTCPBladeMessage(builder,
                                    txn_id,
                                    static_cast<int64_t>(error_code),
                                    message::TCPBladeMessage::Message_WriteAck,
                                    ack.Union());
                builder.Finish(ack_msg);
#ifdef PERF_LOG
                double write_mbps = data_fb->size() / (1024.0 * 1024) /
                    (write_time.getUsElapsed() / 1000000.0);
                LOG<PERF>("TCPServer::process write time (us): ",
                        write_time.getUsElapsed(),
                        " bw (MB/s): ", write_mbps,
                        " size: ", data_fb->size());
#endif
                break;
            }
        case message::TCPBladeMessage::Message_WriteBulk:
            {
#ifdef PERF_LOG
                TimerFunction write_time;
#endif
                // first see if the object exists on the server.
                // If so, overwrite it and account for the size change.
                uint64_t num_oids = msg->message_as_WriteBulk()->num_oids();
                auto oids = msg->message_as_WriteBulk()->oids();
                auto data_fb = msg->message_as_WriteBulk()->data();
                LOG<INFO>("Server processing WRITE-BULK request");

                assert(num_oids = oids->size());

                const char* data_ptr =
                    reinterpret_cast<const char*>(data_fb->data());
                for (const auto& oid : *oids) {
                    if (mem->exists(oid)) {
                        curr_size -= mem->size(oid);
                    }

                    uint64_t obj_size =
                        ntohl(*reinterpret_cast<const uint64_t*>(data_ptr));
                    data_ptr += sizeof(uint64_t);

                    // Throw error if put would exceed size of the store
                    if (curr_size + obj_size > pool_size) {
                        LOG<ERROR>("Put would go over capacity on server. ",
                                "Current size: ", curr_size,
                                " Incoming size: ", data_fb->size(),
                                " Pool size: ", pool_size);
                        error_code =
                            cirrus::ErrorCodes::kServerMemoryErrorException;
                        success = false;
                        break;
                    } else {
                        // Service the write request by
                        // storing the serialized object
                        LOG<INFO>("Writing object with size: " , obj_size);
                        const char* begin = data_ptr;
                        const char* end = data_ptr + obj_size;
                        mem->put(oid, MemSlice(begin, end));

                        curr_size += obj_size;
                    }

                    data_ptr += obj_size;  // advance cursor
                }

                // Create and send ack
                auto ack = message::TCPBladeMessage::CreateWriteBulkAck(builder,
                                           success);
                auto ack_msg =
                     message::TCPBladeMessage::CreateTCPBladeMessage(builder,
                                 txn_id,
                                 static_cast<int64_t>(error_code),
                                 message::TCPBladeMessage::Message_WriteBulkAck,
                                 ack.Union());
                builder.Finish(ack_msg);
#ifdef PERF_LOG
                double write_mbps = data_fb->size() / (1024.0 * 1024) /
                    (write_time.getUsElapsed() / 1000000.0);
                LOG<PERF>("TCPServer::process write-bulk time (us): ",
                        write_time.getUsElapsed(),
                        " bw (MB/s): ", write_mbps,
                        " size: ", data_fb->size());
#endif
                break;
            }
        case message::TCPBladeMessage::Message_Read:
            {
#ifdef PERF_LOG
                TimerFunction read_time;
#endif
                /* Service the read request by sending the serialized object
                 to the client */
                LOG<INFO>("Processing READ request");
                ObjectID oid = msg->message_as_Read()->oid();

                LOG<INFO>("Server extracted oid: ", oid);

                // If the oid is not on the server, this operation has failed

                if (!mem->exists(oid)) {
                    success = false;
                    error_code = cirrus::ErrorCodes::kNoSuchIDException;
                    LOG<ERROR>("Oid ", oid, " does not exist on server");
                }

                flatbuffers::Offset<flatbuffers::Vector<int8_t>> fb_vector;
                if (success) {
                    // XXX Getting the item twice is inefficient
                    fb_vector = builder.CreateVector(
                            std::vector<int8_t>(mem->get(oid).get()));
                } else {
                    std::vector<int8_t> data;
                    fb_vector = builder.CreateVector(data);
                }

                LOG<INFO>("Server building response");
                // Create and send ack
                auto ack = message::TCPBladeMessage::CreateReadAck(builder,
                                            oid, success, fb_vector);
                auto ack_msg =
                    message::TCPBladeMessage::CreateTCPBladeMessage(builder,
                                    txn_id,
                                    static_cast<int64_t>(error_code),
                                    message::TCPBladeMessage::Message_ReadAck,
                                    ack.Union());
                builder.Finish(ack_msg);
                LOG<INFO>("Server done building response");
#ifdef PERF_LOG
                double read_mbps = mem->size(oid) / (1024.0 * 1024) /
                    (read_time.getUsElapsed() / 1000000.0);
                LOG<PERF>("TCPServer::process read time (us): ",
                        read_time.getUsElapsed(),
                        " bw (MB/s): ", read_mbps,
                        " size: ", mem->size(oid));
#endif
                break;
            }
        case message::TCPBladeMessage::Message_ReadBulk:
            {
                /** Read Bulk operation
                  * We assume objects do not change size during this operation
                  * Warning: No atomicity guarantees
                  */
#ifdef PERF_LOG
                TimerFunction read_time;
#endif
                LOG<INFO>("Processing READ BULK request");
                // number of objects to be transfered
                uint32_t num_oids = msg->message_as_ReadBulk()->num_oids();
                auto data_fb_oids = msg->message_as_ReadBulk()->oids();

                // first we figure out the total size to send back
                uint32_t data_size = sizeof(uint32_t);  //< size of main header
                for (const auto& oid : *data_fb_oids) {
                    if (!mem->exists(oid)) {
                        success = false;
                        error_code = cirrus::ErrorCodes::kNoSuchIDException;
                        LOG<ERROR>("Oid ", oid, " does not exist on server");
                        break;
                    }

                    // size of an header containing size of object
                    data_size += sizeof(uint32_t);
                    // size of the data
                    data_size += mem->size(oid);
                }

                flatbuffers::Offset<flatbuffers::Vector<int8_t>> data_fb_vector;

                if (success) {
                    int8_t* raw_mem;
                    // build the flatbuffer vector with the right size
                    data_fb_vector =
                        builder.CreateUninitializedVector(data_size, &raw_mem);
                    // for each oid to be transfered
                    // we copy the size of the object
                    // and the content to the buffer
                    *reinterpret_cast<uint32_t*>(raw_mem) = num_oids;
                    raw_mem += sizeof(uint32_t);
                    for (uint32_t i = 0; i < num_oids; ++i) {
                        auto oid = *(data_fb_oids->begin() + i);

                        auto oid_data = mem->get(oid).get();
                        uint32_t size = oid_data.size();
                        uint32_t* data_ptr =
                                         reinterpret_cast<uint32_t*>(raw_mem);
                        *data_ptr++ = htonl(size);

                        raw_mem = reinterpret_cast<int8_t*>(data_ptr);
                        std::memcpy(raw_mem, oid_data.data(), size);
                        raw_mem = reinterpret_cast<int8_t*>(
                                reinterpret_cast<char*>(raw_mem) + size);
                    }
                } else {
                    data_fb_vector = builder.CreateVector(
                            std::vector<int8_t>());
                }

                LOG<INFO>("Server building readbulk response");
                // Create and send ack
                auto ack = message::TCPBladeMessage::CreateReadBulkAck(builder,
                                                      success, data_fb_vector);
                auto ack_msg =
                    message::TCPBladeMessage::CreateTCPBladeMessage(builder,
                                  txn_id,
                                  static_cast<int64_t>(error_code),
                                  message::TCPBladeMessage::Message_ReadBulkAck,
                                  ack.Union());
                builder.Finish(ack_msg);
                LOG<INFO>("Server done building response");
#ifdef PERF_LOG
                double read_mbps = data_size / (1024.0 * 1024) /
                    (read_time.getUsElapsed() / 1000000.0);
                LOG<PERF>("TCPServer::process readbulk time (us): ",
                        read_time.getUsElapsed(),
                        " bw (MB/s): ", read_mbps,
                        " size: ", entry_itr->second.size());
#endif
                break;
            }
        case message::TCPBladeMessage::Message_Remove:
            {
                LOG<INFO>("Processing REMOVE request");
                ObjectID oid = msg->message_as_Remove()->oid();

                success = false;
                // Remove the object if it exists on the server.
                if (mem->exists(oid)) {
                    curr_size -= mem->size(oid);
                    mem->delet(oid);
                    success = true;
                }
                // Create and send ack
                auto ack = message::TCPBladeMessage::CreateWriteAck(builder,
                                         oid, success);
                auto ack_msg =
                   message::TCPBladeMessage::CreateTCPBladeMessage(builder,
                                    txn_id,
                                    static_cast<int64_t>(error_code),
                                    message::TCPBladeMessage::Message_RemoveAck,
                                    ack.Union());
                builder.Finish(ack_msg);
                break;
            }
        default:
            LOG<ERROR>("Unknown message", " type:", msg->message_type());
            throw cirrus::Exception("Unknown message "
                                    "type received from client.");
            break;
    }

    int message_size = builder.GetSize();
    // Convert size to network order and send
    uint32_t network_order_size = htonl(message_size);
    if (send_all(sock, &network_order_size, sizeof(uint32_t), 0) == -1) {
        LOG<ERROR>("Server error sending message back to client. "
            "Possible client died");
        return false;
    }

    LOG<INFO>("Server sent size.");
    LOG<INFO>("On server error code is: ", static_cast<int64_t>(error_code));
    // Send main message
#ifdef PERF_LOG
    TimerFunction reply_time;
#endif
    if (send_all(sock, builder.GetBufferPointer(), message_size, 0)
        != message_size) {
        LOG<ERROR>("Server error sending message back to client. "
            "Possible client died");
        return false;
    }
#ifdef PERF_LOG
    double reply_mbps = message_size / (1024.0 * 1024) /
        (reply_time.getUsElapsed() / 1000000.0);
    LOG<PERF>("TCPServer::process reply time (us): ",
            reply_time.getUsElapsed(),
            " bw (MB/s): ", reply_mbps);
#endif

    LOG<INFO>("Server sent ack of size: ", message_size);
    LOG<INFO>("Server done processing message from client");
    return true;
}


}  // namespace cirrus
