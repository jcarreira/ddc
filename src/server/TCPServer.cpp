#include "server/TCPServer.h"

#include <poll.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <map>
#include <vector>
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
  * @param queue_len the length of the queue to make connections with the
  * server.
  * @param pool_size_ the number of bytes to have in the memory pool.
  */
TCPServer::TCPServer(int port, uint64_t pool_size_, int queue_len) {
    port_ = port;
    queue_len_ = queue_len;
    pool_size = pool_size_;
    server_sock_ = 0;
}

/**
  * Initializer for the server. Sets up the socket it uses to listen for
  * incoming connections.
  */
void TCPServer::init() {
    struct sockaddr_in serv_addr;

    server_sock_ = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock_ < 0) {
        throw cirrus::ConnectionException("Server error creating socket");
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(port_);

    LOG<INFO>("Created socket in TCPServer");

    int opt = 1;
    if (setsockopt(server_sock_, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt,
                   sizeof(opt))) {
        throw cirrus::ConnectionException("Error forcing port binding");
    }

    int ret = bind(server_sock_, reinterpret_cast<sockaddr*>(&serv_addr),
            sizeof(serv_addr));
    if (ret < 0) {
        throw cirrus::ConnectionException("Error binding in port "
               + to_string(port_));
    }

    if (listen(server_sock_, queue_len_) == -1) {
        throw cirrus::ConnectionException("Error listening on port "
            + to_string(port_));
    }

    fds.at(curr_index).fd = server_sock_;
    // Only listen for data to read
    fds.at(curr_index++).events = POLLIN;
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
        int poll_status = poll(fds.data(), num_fds, timeout);
        LOG<INFO>("Poll returned with status: ", poll_status);
        if (poll_status == -1) {
            throw cirrus::ConnectionException("Server error calling poll.");
        } else if (poll_status == 0) {
            LOG<INFO>(timeout, " milliseconds elapsed without contact.");
        } else {
            // there is at least one pending event, find it.
            for (uint64_t i = 0; i < curr_index; i++) {
                struct pollfd& curr_fd = fds.at(i);
                if (curr_fd.revents != POLLIN) {
                    LOG<INFO>("Non read event on socket: ", curr_fd.fd);
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
                    LOG<INFO>("Created new socket: ", newsock);
                    fds.at(curr_index).fd = newsock;
                    fds.at(curr_index).events = POLLIN;
                    curr_index++;
                } else {
                    if (!process(curr_fd.fd)) {
                        // do not make future alerts on this fd
                        curr_fd.fd = -1;
                    }
                }
                curr_fd.revents = 0;  // Reset the event flags
            }
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
        sent = send(sock, data, len, 0);

        if (sent == -1) {
            throw cirrus::Exception("Server error sending data to client");
        }

        total_sent += sent;

        // Increment the pointer to data we're sending by the amount just sent
        data = static_cast<const char*>(data) + sent;
    }

    return total_sent;
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
    int current_buf_size = sizeof(uint32_t);
    buffer.reserve(current_buf_size);
    int bytes_read = 0;

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
    LOG<INFO>("Server received size from client");
    // Convert to host byte order
    uint32_t* incoming_size_ptr = reinterpret_cast<uint32_t*>(
                                                            buffer.data());
    int incoming_size = ntohl(*incoming_size_ptr);
    LOG<INFO>("Server received incoming size of ", incoming_size);
    // Resize the buffer to be larger if necessary
    if (incoming_size > current_buf_size) {
        buffer.resize(incoming_size);
    }

    bytes_read = 0;

    while (bytes_read < incoming_size) {
        int retval = read(sock, buffer.data() + bytes_read,
                          incoming_size - bytes_read);

        if (retval < 0) {
            throw cirrus::Exception("Serverside error while "
                                    "reading full message.");
        }

        bytes_read += retval;
        LOG<INFO>("Server received ", bytes_read, " bytes of ", incoming_size);
    }
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
    switch (msg->message_type()) {
        case message::TCPBladeMessage::Message_Write:
            {
                LOG<INFO>("Server processing write request.");

                ObjectID oid = msg->message_as_Write()->oid();

                // Throw error if put would exceed size of the store
                auto data_fb = msg->message_as_Write()->data();
                if (curr_size + data_fb->size() > pool_size) {
                    LOG<ERROR>("Put would go over capacity on server. "
                            "Curr_size, pool_size, incoming size:",
                            curr_size, pool_size, data_fb->size());
                    error_code =
                        cirrus::ErrorCodes::kServerMemoryErrorException;
                } else {
                    // Service the write request by
                    //  storing the serialized object
                    std::vector<int8_t> data(data_fb->begin(), data_fb->end());
                    // Create entry in store mapping the data to the id
                    curr_size += data_fb->size();
                    store[oid] = data;
                }

                // Create and send ack
                auto ack = message::TCPBladeMessage::CreateWriteAck(builder,
                                           true, oid);
                auto ack_msg =
                     message::TCPBladeMessage::CreateTCPBladeMessage(builder,
                                    txn_id,
                                    static_cast<int>(error_code),
                                    message::TCPBladeMessage::Message_WriteAck,
                                    ack.Union());
                builder.Finish(ack_msg);
                break;
            }
        case message::TCPBladeMessage::Message_Read:
            {
                /* Service the read request by sending the serialized object
                 to the client */
                LOG<INFO>("Processing read request");
                ObjectID oid = msg->message_as_Read()->oid();
                LOG<INFO>("Server extracted oid");
                bool exists = true;
                auto entry_itr = store.find(oid);
                LOG<INFO>("Got pair from store");
                if (entry_itr == store.end()) {
                    exists = false;
                    error_code = cirrus::ErrorCodes::kNoSuchIDException;
                    LOG<ERROR>("Oid ", oid, " does not exist on server");
                }
                std::vector<int8_t> data;
                if (exists) {
                    data = store[oid];
                }

                auto fb_vector = builder.CreateVector(data);
                LOG<INFO>("Server building response");
                // Create and send ack
                auto ack = message::TCPBladeMessage::CreateReadAck(builder,
                                            oid, exists, fb_vector);
                auto ack_msg =
                    message::TCPBladeMessage::CreateTCPBladeMessage(builder,
                                    txn_id,
                                    static_cast<int>(error_code),
                                    message::TCPBladeMessage::Message_ReadAck,
                                    ack.Union());
                builder.Finish(ack_msg);
                LOG<INFO>("Server done building response");
                break;
            }
        case message::TCPBladeMessage::Message_Remove:
            {
                ObjectID oid = msg->message_as_Remove()->oid();

                bool success = false;
                auto entry_itr = store.find(oid);
                if (entry_itr != store.end()) {
                    store.erase(entry_itr);
                    curr_size -= entry_itr->second.size();
                    success = true;
                }
                // Create and send ack
                auto ack = message::TCPBladeMessage::CreateWriteAck(builder,
                                         oid, success);
                auto ack_msg =
                   message::TCPBladeMessage::CreateTCPBladeMessage(builder,
                                    txn_id,
                                    static_cast<int>(error_code),
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
    if (send(sock, &network_order_size, sizeof(uint32_t), 0) == -1) {
        throw cirrus::Exception("Server error sending message back to client");
    }

    LOG<INFO>("Server sent size.");

    // Send main message
    if (send_all(sock, builder.GetBufferPointer(), message_size, 0)
        != message_size) {
        throw cirrus::Exception("Server error sending message back to client");
    }

    LOG<INFO>("Server sent ack of size: ", message_size);
    LOG<INFO>("Server done processing message from client");
    return true;
}

}  // namespace cirrus
