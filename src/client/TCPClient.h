#ifndef SRC_CLIENT_TCPCLIENT_H_
#define SRC_CLIENT_TCPCLIENT_H_

#include <poll.h>
#include <string>
#include <thread>
#include <queue>
#include <vector>
#include <utility>
#include <map>
#include <atomic>
#include "common/schemas/TCPBladeMessage_generated.h"
#include "client/BladeClient.h"
#include "common/Exception.h"

namespace cirrus {

using ObjectID = uint64_t;
using TxnID = uint64_t;

/**
  * A TCP based client that inherits from BladeClient.
  */
class TCPClient : public BladeClient {
 public:
    ~TCPClient() override;
    void connect(const std::string& address,
        const std::string& port) override;

    bool write_sync(ObjectID oid, const void* data, uint64_t size) override;
    std::pair<std::shared_ptr<char>, unsigned int> read_sync(
        ObjectID oid) override;

    ClientFuture write_async(ObjectID oid, const void* data,
                                       uint64_t size) override;
    ClientFuture read_async(ObjectID oid) override;

    bool remove(ObjectID id) override;

    void open_additional_cxns(uint64_t num_additional);

 private:
    /**
      * A struct shared between futures and the receiver_thread. Used to
      * notify client of operation completeion, as well as to complete
      * transactions.
      */
    struct txn_info {
        /** result of the transaction */
        std::shared_ptr<bool> result;
        /** Boolean indicating whether transaction is complete */
        std::shared_ptr<bool> result_available;
        /** Error code if any were thrown on the server. */
        std::shared_ptr<cirrus::ErrorCodes> error_code;
        /** Semaphore for the transaction. */
        std::shared_ptr<cirrus::PosixSemaphore> sem;

        /** Pointer to shared ptr that points to any mem allocated for reads. */
        std::shared_ptr<std::shared_ptr<char>> mem_for_read_ptr;

        /** Pointer to size of mem for read. */
        std::shared_ptr<uint64_t> mem_size;

        txn_info() {
            result = std::make_shared<bool>();
            result_available = std::make_shared<bool>();
            *result_available = false;
            sem = std::make_shared<cirrus::PosixSemaphore>();
            error_code = std::make_shared<cirrus::ErrorCodes>();
            mem_for_read_ptr = std::make_shared<std::shared_ptr<char>>();
            mem_size = std::make_shared<uint64_t>(0);
        }
    };

    ssize_t send_all(int, const void*, size_t, int);
    ssize_t read_all(int sock, void* data, size_t len);

    ClientFuture enqueue_message(
                        std::unique_ptr<flatbuffers::FlatBufferBuilder> builder,
                        const int txn_id);
    void process_received();
    void process_send();
    void process_message(int sock);

    /** vector of sockets used to communicate w/ remote store */
    std::vector<int> sockets;

    /** Next txn_id to assign to a txn_info. Used as a unique identifier. */
    std::atomic<std::uint64_t> curr_txn_id = {0};

    /**
      * Map that allows receiver thread to map transactions to their
      * completion information. When a message is added to the send queue,
      * a struct txn_info is created and added to this map. This struct
      * allows the receiver thread to place information regarding completion
      * as well as data in a location that is accessible to the future
      * corresponding to the transaction.
      */
    std::map<TxnID, std::shared_ptr<struct txn_info>> txn_map;
    /**
     * Queue of FlatBufferBuilders that the sender_thread processes to send
     * messages to the server.
     */
    std::queue<std::unique_ptr<flatbuffers::FlatBufferBuilder>> send_queue;

    std::vector<struct pollfd> pollfds;

    /**
     * How long the client will wait during a call to poll() before
     * timing out (in ms).
     */
    int timeout = 60 * 1000 * 3;

    /**
     * Lock to prevent threads from modifying fds while poll call is underway.
     */
    cirrus::SpinLock pollfds_lock;

    /**
     * Lock to prevent threads from accessing sockets vector while it is
     * being modified.
     */
    cirrus::SpinLock sockets_lock;

    /** Lock on the txn_map. */
    cirrus::SpinLock map_lock;
    /** Lock on the send_queue. */
    cirrus::SpinLock queue_lock;
    /** Semaphore for the send_queue. */
    cirrus::PosixSemaphore queue_semaphore;
    /** Thread that runs the receiving loop. */
    std::thread* receiver_thread;
    /** Thread that runs the sending loop. */
    std::thread* sender_thread;

    /** The port that the remote store is listening on. */
    std::string port_string_;
    /** The IP address of the remote store. */
    std::string address_;

    /**
     * Bool that the process_send and process_received threads check.
     * If it is true, they exit their loops so that they may
     * finish execution and join may be called on them. Set to true
     * in the class destructor.
     */
    bool terminate_threads = false;

    /**
     * Bool that indicates whether the client has already connected to a remote
     * store.
     */
    std::atomic<bool> has_connected = {false};
};

}  // namespace cirrus

#endif  // SRC_CLIENT_TCPCLIENT_H_
