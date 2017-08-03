#include "client/RDMAClient.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <errno.h>
#include <time.h>

#include <string>
#include <cstring>
#include <atomic>
#include <random>
#include <cassert>

#include "utils/utils.h"
#include "utils/CirrusTime.h"
#include "utils/logging.h"

#include "common/ThreadPinning.h"
#include "common/Synchronization.h"
#include "common/Exception.h"
#include "common/schemas/BladeMessage_generated.h"


namespace cirrus {

static const int initial_buffer_size = 50;

/**
  * Connects the client to the remote server.
  */

  void RDMAClient::connect(const std::string& host, const std::string& port) {
      seed = time(nullptr);
      connect_rdma_cm(host, port);
  }

/**
  * Asynchronously writes an object to remote storage under id.
  * @param id the id of the object the user wishes to write to remote memory.
  * @param data a pointer to the buffer where the serialized object should
  * be read read from.
  * @param size the size of the serialized object being read from
  * local memory.
  * @return A Future that contains information about the status of the
  * operation.
  */

  // TODO(Tyler): implement async method
// cirrus::Future RDMAClient::write_async(ObjectID oid,
//                                        void* data, uint64_t size) {
//     // Make sure that the pointer is not null
//     TEST_NZ(data == nullptr);
//     // Create flatbuffer builder
//     std::shared_ptr<flatbuffers::FlatBufferBuilder> builder =
//                             std::make_shared<flatbuffers::FlatBufferBuilder>(
//                                 initial_buffer_size);
//
//     // Create and send write request
//     int8_t *data_cast = reinterpret_cast<int8_t*>(data);
//     std::vector<int8_t> data_vector(data_cast, data_cast + size);
//     auto data_fb_vector = builder->CreateVector(data_vector);
//     auto msg_contents = message::BladeMessage::CreateWrite(*builder,
//                                                               oid,
//                                                               data_fb_vector);
//     auto msg = message::BladeMessage::CreateBladeMessage(
//                                         *builder,
//                                         curr_txn_id,
//                                         message::BladeMessage::Message_Write,
//                                         msg_contents.Union());
//     builder->Finish(msg);
//
//     return enqueue_message(builder);
// }

/**
  * Asynchronously reads an object corresponding to ObjectID
  * from the remote server.
  * @param id the id of the object the user wishes to read to local memory.
  * @param data a pointer to the buffer where the serialized object should
  * be read to.
  * @param size the size of the serialized object being read from
  * remote storage.
  * @return True if the object was successfully read from the server, false
  * otherwise.
  */

// TODO(Tyler): ADD ASYNC
// cirrus::Future RDMAClient::read_async(ObjectID oid, void* data,
//                                      uint64_t /* size */) {
//     std::shared_ptr<flatbuffers::FlatBufferBuilder> builder =
//                             std::make_shared<flatbuffers::FlatBufferBuilder>(
//                                 initial_buffer_size);
//
//     // Create and send read request
//
//     auto msg_contents = message::BladeMessage::CreateRead(*builder, oid);
//
//     auto msg = message::BladeMessage::CreateBladeMessage(
//                                         *builder,
//                                         curr_txn_id,
//                                         message::BladeMessage::Message_Read,
//                                         msg_contents.Union());
//     builder->Finish(msg);
//
//     return enqueue_message(builder, data);
// }

/**
  * Writes an object to remote storage under id.
  * @param id the id of the object the user wishes to write to remote memory.
  * @param data a pointer to the buffer where the serialized object should
  * be read read from.
  * @param size the size of the serialized object being read from
  * local memory.
  * @return True if the object was successfully written to the server, false
  * otherwise.
  */
bool RDMAClient::write_sync(ObjectID oid, const void* data, uint64_t size) {
    bool retval;
    BladeLocation loc;
    if (objects_.find(oid, loc)) {
        retval = writeRemote(data, loc, nullptr);
    } else {
        // we could merge this into a single message (?)
        cirrus::AllocationRecord allocRec;
        {
            TimerFunction tf("FullBladeObjectStoreTempl::put allocate", true);
            allocRec = allocate(size);
        }
        insertObjectLocation(oid, size, allocRec);
        LOG<INFO>("FullBladeObjectStoreTempl::writeRemote after alloc");
        retval = writeRemote(data,
                          BladeLocation(size, allocRec),
                          nullptr);
    }
    return retval;
}

/**
  * Reads an object corresponding to ObjectID from the remote server.
  * @param id the id of the object the user wishes to read to local memory.
  * @return An std pair containing a shared pointer to the buffer that the
  * serialized object read from the server resides in as well as the size of
  * the buffer.
  */
std::pair<std::shared_ptr<char>, unsigned int>
RDMAClient::read_sync(ObjectID oid) {
    BladeLocation loc;
    if (objects_.find(oid, loc)) {
        // Read into the section of memory you just allocated
        auto data = std::shared_ptr<char>(new char[loc.size],
            std::default_delete< char[]>());
        readToLocal(loc, data.get());
        return std::make_pair(data, loc.size);
    } else {
        throw cirrus::NoSuchIDException("Requested ObjectID "
                                        "does not exist remotely.");
    }
}

/**
  * Removes the object corresponding to the given ObjectID from the
  * remote store.
  * @param oid the ObjectID of the object to be removed.
  * @return True if the object was successfully removed from the server, false
  * if the object does not exist remotely or if another error occurred.
  */
bool RDMAClient::remove(ObjectID oid) {
    BladeLocation loc;
    if (objects_.find(oid, loc)) {
        objects_.erase(oid);
        return deallocate(loc.allocRec);
    } else {
        throw cirrus::NoSuchIDException("Error. Trying to remove "
                                        "nonnexistent object");
    }
    return false;
}

/**
 * Reads an object to local memory from the remote store.
 * @param loc the BladeLocation for the desired object. Contains the
 * address and size of the object.
 * @param ptr a pointer to the memory where the object will be read to.
 */
bool RDMAClient::readToLocal(BladeLocation loc, void* ptr) {
    RDMAMem mem(ptr, loc.size);
    rdma_read_sync(loc.allocRec, 0, loc.size, ptr, &mem);
    return true;
}

/**
 * Reads an object to local memory from the remote store asynchronously.
 * @param loc the BladeLocation for the desired object. Contains the
 * address and size of the object.
 * @param ptr a pointer to the memory where the object will be read to.
 * @return a std::shared_ptr to a FutureBladeOp. This FutureBladeOp
 * contains information about the status of the operation.
 */
std::shared_ptr<RDMAClient::FutureBladeOp> RDMAClient::readToLocalAsync(
        BladeLocation loc, void* ptr) {
    auto future = rdma_read_async(loc.allocRec, 0, loc.size, ptr);
    return future;
}

/**
 * Writes an object from local memory to the remote store.
 * @param data a pointer to the data to be written.
 * @param loc a BladeLocation, containing the size of the object and
 * the location to be written to.
 * @param mem a pointer to an RDMAMem, which is the registered memory
 * where the data currently resides. If null, a new RDMAMem is used
 * for this object.
 */
bool RDMAClient::writeRemote(const void *data, BladeLocation loc,
                             RDMAMem* mem) {
    RDMAMem mm(data, loc.size);
    {
        // TimerFunction tf("writeRemote", true);
        // LOG<INFO>("FullBladeObjectStoreTempl:: writing sync");
        rdma_write_sync(loc.allocRec, 0, loc.size, data,
                nullptr == mem ? &mm : mem);
    }
    return true;
}

/**
 * Writes an object from local memory to the remote store asynchronously.
 * @param data a pointer to the data to be written.
 * @param loc a BladeLocation, containing the size of the object and
 * the location to be written to.
 * @param mem a pointer to an RDMAMem, which is the registered memory
 * where the data currently resides. If null, a new RDMAMem is used
 * for this object.
 * @return a std::shared_ptr to a FutureBladeOp. This FutureBladeOp
 * contains information about the status of the operation.
 */
std::shared_ptr<RDMAClient::FutureBladeOp> RDMAClient::writeRemoteAsync(
        const void *data, BladeLocation loc) {
    auto future = rdma_write_async(loc.allocRec, 0, loc.size, data);
    return future;
}

/**
 * Inserts an object into objects_ , which maps ObjectIDs to
 * BladeLocation objects.
 * @param id the ObjectID of the object.
 * @param size the size of the object.
 * @param allocRec the AllocationRecord being used for this object.
 */
bool RDMAClient::insertObjectLocation(ObjectID id,
                                      uint64_t size,
                                      const AllocationRecord& allocRec) {
    objects_[id] = BladeLocation(size, allocRec);
    return true;
}

/**
  * Initializes rdma params.
  * This method takes a pointer to a struct rdma_conn_param and assigns initial
  * values.
  * @param params the struct rdma_conn_param to be initialized.
  */
void RDMAClient::build_params(struct rdma_conn_param *params) {
    memset(params, 0, sizeof(*params));
    params->initiator_depth = params->responder_resources = 10;
    params->rnr_retry_count = 70;
    params->retry_count = 7;
}

void RDMAClient::alloc_rdma_memory(ConnectionContext *ctx) {
    TEST_NZ(posix_memalign(reinterpret_cast<void **>(&ctx->recv_msg),
                sysconf(_SC_PAGESIZE),
                RECV_MSG_SIZE));
    TEST_NZ(posix_memalign(reinterpret_cast<void **>(&con_ctx_.send_msg),
                static_cast<size_t>(sysconf(_SC_PAGESIZE)),
                SEND_MSG_SIZE));
}

void RDMAClient::setup_memory(ConnectionContext *ctx) {
    alloc_rdma_memory(ctx);

    LOG<INFO>("Registering region with size: ",
            (RECV_MSG_SIZE / 1024 / 1024), " MB");
    TEST_Z(con_ctx_.recv_msg_mr =
            ibv_reg_mr(ctx->gen_ctx_.pd, con_ctx_.recv_msg,
                RECV_MSG_SIZE,
                IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE));

    TEST_Z(con_ctx_.send_msg_mr =
            ibv_reg_mr(ctx->gen_ctx_.pd, con_ctx_.send_msg,
                SEND_MSG_SIZE,
                IBV_ACCESS_LOCAL_WRITE |
                IBV_ACCESS_REMOTE_WRITE));

    default_recv_mem_ = RDMAMem(ctx->recv_msg,
            RECV_MSG_SIZE, con_ctx_.recv_msg_mr);
    default_send_mem_ = RDMAMem(ctx->send_msg,
            SEND_MSG_SIZE, con_ctx_.send_msg_mr);

    con_ctx_.setup_done = true;
    LOG<INFO>("Set up memory done");
}

void RDMAClient::build_qp_attr(struct ibv_qp_init_attr *qp_attr,
        ConnectionContext* ctx) {
    memset(qp_attr, 0, sizeof(*qp_attr));

    qp_attr->send_cq = ctx->gen_ctx_.cq;
    qp_attr->recv_cq = ctx->gen_ctx_.cq;
    qp_attr->qp_type = IBV_QPT_RC;

    qp_attr->cap.max_send_wr = MAX_SEND_WR;
    qp_attr->cap.max_recv_wr = MAX_RECV_WR;
    qp_attr->cap.max_send_sge = MAX_SEND_SGE;
    qp_attr->cap.max_recv_sge = MAX_RECV_SGE;
}

/**
 * Posts a work request to the receive queue.
 * @param id a pointer to the rdma_cm_id that contains the information
 * needed to construct the receive request.
 */
int RDMAClient::post_receive(struct rdma_cm_id *id) {
    ConnectionContext *ctx =
        reinterpret_cast<ConnectionContext*>(id->context);

    LOG<INFO>("Posting receive");

    struct ibv_recv_wr wr, *bad_wr = nullptr;
    struct ibv_sge sge;

    memset(&wr, 0, sizeof(wr));

    auto op_info = new RDMAClient::RDMAOpInfo(id, ctx->recv_sem);
    wr.wr_id = reinterpret_cast<uint64_t>(op_info);
    wr.sg_list = &sge;
    wr.num_sge = 1;

    sge.addr = reinterpret_cast<uint64_t>(ctx->recv_msg);
    sge.length = 1000;  // FIX
    sge.lkey = ctx->recv_msg_mr->lkey;

    return ibv_post_recv(id->qp, &wr, &bad_wr);
}

void RDMAClient::build_connection(struct rdma_cm_id *id) {
    struct ibv_qp_init_attr qp_attr;

    auto ctx = reinterpret_cast<ConnectionContext*>(id->context);

    build_context(id->verbs, ctx);
    build_qp_attr(&qp_attr, ctx);

    // create the QP we are going to use to communicate with
    TEST_NZ(rdma_create_qp(id, ctx->gen_ctx_.pd, &qp_attr));
}

void RDMAClient::build_context(struct ibv_context *verbs,
        ConnectionContext* ctx) {
    // FIX: we only need one context?
    ctx->gen_ctx_.ctx = verbs;

    TEST_Z(ctx->gen_ctx_.pd = ibv_alloc_pd(ctx->gen_ctx_.ctx));
    TEST_Z(ctx->gen_ctx_.comp_channel =
            ibv_create_comp_channel(ctx->gen_ctx_.ctx));
    TEST_Z(ctx->gen_ctx_.cq = ibv_create_cq(ctx->gen_ctx_.ctx,
                CQ_DEPTH, nullptr, ctx->gen_ctx_.comp_channel, 0));
    TEST_NZ(ibv_req_notify_cq(ctx->gen_ctx_.cq, 0));

    ctx->gen_ctx_.cq_poller_thread =
        new std::thread(&RDMAClient::poll_cq, this, ctx);

    unsigned n_threads = std::thread::hardware_concurrency();

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> rv(1, n_threads);

    ThreadPinning::pinThread(ctx->gen_ctx_.cq_poller_thread->native_handle(),
            rv(gen));  // random core
}

/**
 * Polls the completion queue, monitoring it for any event completions.
 * If a request is completed successfully, it is acted upon.
 * @param ctx a pointer to the ConnectionContext that should be monitored.
 */
void* RDMAClient::poll_cq(ConnectionContext* ctx) {
    struct ibv_cq *cq;
    struct ibv_wc wc;
    void* cq_ctx;

    while (1) {
        TEST_NZ(ibv_get_cq_event(ctx->gen_ctx_.comp_channel, &cq, &cq_ctx));
        ibv_ack_cq_events(cq, 1);
        TEST_NZ(ibv_req_notify_cq(cq, 0));

        while (ibv_poll_cq(cq, 1, &wc)) {
            if (wc.status == IBV_WC_SUCCESS) {
                on_completion(&wc);
            } else {
                std::cerr <<
                    "poll_cq: status is not IBV_WC_SUCCESS" << std::endl;
                std::cerr << "wr_id: " << wc.wr_id << std::endl;
                std::cerr << "status: " << wc.status << std::endl;
                DIE("Leaving..");
            }
        }
    }

    return nullptr;
}

/**
 * Method that handles ibv Work completion structs. Signals the op_sem
 * attached to the op_info that tracks the ibv_wc.
 * @param wc a pointer to the ibv_wc to be handled.
 */
void RDMAClient::on_completion(struct ibv_wc *wc) {
    RDMAClient::RDMAOpInfo* op_info = reinterpret_cast<RDMAClient::RDMAOpInfo*>(
                                                                    wc->wr_id);

    LOG<INFO>("on_completion. wr_id: ", wc->wr_id,
        " opcode: ", wc->opcode,
        " byte_len: ", wc->byte_len);

    op_info->apply();

    switch (wc->opcode) {
        case IBV_WC_RECV:
            if (op_info->op_sem)
                op_info->op_sem->signal();
            break;
        case IBV_WC_RDMA_READ:
        case IBV_WC_RDMA_WRITE:
            assert(outstanding_send_wr > 0);
            outstanding_send_wr--;
            if (op_info->op_sem)
                op_info->op_sem->signal();
            break;
        case IBV_WC_SEND:
            assert(outstanding_send_wr > 0);
            outstanding_send_wr--;
            if (op_info->op_sem)
                op_info->op_sem->signal();
            break;
        default:
            LOG<ERROR>("Unknown opcode");
            exit(-1);
            break;
    }
}

/**
  * Sends + Receives synchronously.
  * This function sends a message and then waits for a reply.
  * @param id a pointer to a struct rdma_cm_id that holds the message to send
  * @param size the size of the message being sent
  * @return false if message fails to send, true otherwise. Only returns
  * once a reply is received.
  */
bool RDMAClient::send_receive_message_sync(struct rdma_cm_id *id,
        uint64_t size) {
    auto con_ctx = reinterpret_cast<ConnectionContext*>(id->context);

    // post receive. We are going to receive a reply
    TEST_NZ(post_receive(id_));

    // send our RPC

    Lock* l = new SpinLock();
    if (!send_message(id, size, l))
        return false;

    // wait for SEND completion
    l->wait();

    LOG<INFO>("Sent is done. Waiting for receive");

    // Wait for reply
    con_ctx->recv_sem->wait();

    return true;
}

/**
  * Sends an RDMA message.
  * This function sends a message using RDMA.
  * @param id a pointer to a struct rdma_cm_id that holds the message to send
  * @param size the size of the message being sent
  * @param lock lock used to create an RDMAOpInfo, then passed to
  * a struct ibv_send_wr, which is passed to post_send()
  * @return success of a call to post_send()
  */
bool RDMAClient::send_message(struct rdma_cm_id *id, uint64_t size,
        Lock* lock) {
    auto ctx = reinterpret_cast<ConnectionContext*>(id->context);

    struct ibv_send_wr wr, *bad_wr = nullptr;
    struct ibv_sge sge;

    memset(&wr, 0, sizeof(wr));

    if (lock != nullptr)
        lock->wait();

    auto op_info  = new RDMAClient::RDMAOpInfo(id, lock);
    wr.wr_id      = reinterpret_cast<uint64_t>(op_info);
    wr.opcode     = IBV_WR_SEND;
    wr.sg_list    = &sge;
    wr.num_sge    = 1;
    wr.send_flags = IBV_SEND_SIGNALED;
    // XXX doesn't work for now
    // if (size <= MAX_INLINE_DATA)
    //    wr.send_flags |= IBV_SEND_INLINE;

    sge.addr   = reinterpret_cast<uint64_t>(ctx->send_msg);
    sge.length = size;
    sge.lkey   = ctx->send_msg_mr->lkey;

    return post_send(id->qp, &wr, &bad_wr);
}


/**
  * Writes over RDMA synchronously.
  * This function writes synchronously using RDMA. It will not return
  * until the message is sent.
  * @param id a pointer to a struct rdma_cm_id that holds the message to send
  * @param size the size of the message being sent
  * @param remote_addr the remote address to write to
  * @param peer_rkey
  * @param mem a reference to an RDMAMem.
  * @return Success.
  */
bool RDMAClient::write_rdma_sync(struct rdma_cm_id *id, uint64_t size,
        uint64_t remote_addr, uint64_t peer_rkey, const RDMAMem& mem) {
    LOG<INFO>("RDMAClient:: write_rdma_async");
    auto op_info = write_rdma_async(id, size, remote_addr, peer_rkey, mem);
    LOG<INFO>("RDMAClient:: waiting");

    if (nullptr == op_info) {
        throw std::runtime_error("Error writing rdma async");
    }

    {
        TimerFunction tf("waiting semaphore", true);
        op_info->op_sem->wait();
    }

    delete op_info->op_sem;
    delete op_info;

    return true;
}

/**
  * Posts send request.
  * This function calls the function ibv_post_send() to post a wr (work request)
  * to the send queue.
  * @param qp the qp of the struct rdma_cm_id
  * @param wr the work request to post
  * @param bad_wr address of a null pointer
  * @return Success of call to ibv_post_send().
  */
bool RDMAClient::post_send(ibv_qp* qp, ibv_send_wr* wr, ibv_send_wr** bad_wr) {
    if (outstanding_send_wr == MAX_SEND_WR) {
        return false;
    }

    outstanding_send_wr++;
    if (ibv_post_send(qp, wr, bad_wr)) {
        LOG<ERROR>("Error post_send.",
            " errno: ", errno);
    }

    return true;
}


/**
  * Writes over RDMA asynchronously.
  * This function writes asynchronously using RDMA. It will return immediately
  * after posting the send request
  * @param id a pointer to a struct rdma_cm_id that holds the message to send
  * @param size the size of the message being sent
  * @param remote_addr the remote address to write to
  * @param peer_rkey
  * @param mem a reference to an RDMAMem.
  * @return A pointer to an RDMAOpInfo containing information about the status
  * of the write.
  */
RDMAClient::RDMAOpInfo* RDMAClient::write_rdma_async(struct rdma_cm_id *id,
                                                    uint64_t size,
                                                    uint64_t remote_addr,
                                                    uint64_t peer_rkey,
                                                    const RDMAMem& mem) {
#if __GNUC__ >= 7
    [[maybe_unused]]
#else
    __attribute__((unused))
#endif
    auto ctx = reinterpret_cast<ConnectionContext*>(id->context);

    struct ibv_send_wr wr, *bad_wr = nullptr;
    struct ibv_sge sge;

    memset(&wr, 0, sizeof(wr));
    memset(&sge, 0, sizeof(sge));

    Lock* l = new SpinLock();
    l->wait();

    auto op_info = new RDMAClient::RDMAOpInfo(id, l);
    wr.wr_id = reinterpret_cast<uint64_t>(op_info);
    wr.opcode = IBV_WR_RDMA_WRITE;
    wr.wr.rdma.remote_addr = remote_addr;
    wr.wr.rdma.rkey = peer_rkey;
    wr.sg_list = &sge;
    wr.num_sge = 1;
    wr.send_flags = IBV_SEND_SIGNALED;
    // XXX doesn't work for now
    // if (size <= MAX_INLINE_DATA)
    //    wr.send_flags |= IBV_SEND_INLINE;

    sge.addr   = mem.addr_;
    sge.length = size;
    sge.lkey   = mem.mr->lkey;

    if (!post_send(id->qp, &wr, &bad_wr))
        throw std::runtime_error("write_rdma_async: error post_send");

    return op_info;
}

/**
  * Reads over RDMA synchronously.
  * This function reads synchronously using RDMA. It will not return
  * until the read is complete.
  * @param id a pointer to a struct rdma_cm_id that holds the message to send
  * @param size the size of the message being sent
  * @param remote_addr the remote address to write to
  * @param peer_rkey
  * @param mem a reference to an RDMAMem.
  */
void RDMAClient::read_rdma_sync(struct rdma_cm_id *id, uint64_t size,
        uint64_t remote_addr, uint64_t peer_rkey, const RDMAMem& mem) {
        RDMAClient::RDMAOpInfo* op_info = read_rdma_async(id, size,
            remote_addr, peer_rkey, mem);

    // wait until operation is completed

    {
        TimerFunction tf("read_rdma_async wait for lock", true);
        op_info->op_sem->wait();
    }

    delete op_info->op_sem;
    delete op_info;
}


/**
  * Reads over RDMA asynchronously.
  * This function reads asynchronously using RDMA. It will return immediately
  * after posting the read request
  * @param id a pointer to a struct rdma_cm_id that holds the message to send
  * @param size the size of the message being sent
  * @param remote_addr the remote address to read from
  * @param peer_rkey
  * @param mem a reference to an RDMAMem.
  * @return A pointer to an RDMAOpInfo containing information about the status
  * of the read.
  */
RDMAClient::RDMAOpInfo* RDMAClient::read_rdma_async(struct rdma_cm_id *id,
                                            uint64_t size,
                                            uint64_t remote_addr,
                                            uint64_t peer_rkey,
                                            const RDMAMem& mem,
                                            std::function<void()> apply_fn) {
#if __GNUC__ >= 7
    [[maybe_unused]]
#else
    __attribute__((unused))
#endif
    auto ctx = reinterpret_cast<ConnectionContext*>(id->context);

    struct ibv_send_wr wr, *bad_wr = nullptr;
    struct ibv_sge sge;

    memset(&wr, 0, sizeof(wr));
    memset(&sge, 0, sizeof(sge));

    Lock* l = new SpinLock();
    l->wait();

    auto op_info    = new RDMAClient::RDMAOpInfo(id, l, apply_fn);
    wr.wr_id        = reinterpret_cast<uint64_t>(op_info);
    wr.opcode       = IBV_WR_RDMA_READ;
    wr.wr.rdma.remote_addr = remote_addr;
    wr.wr.rdma.rkey = peer_rkey;
    wr.sg_list      = &sge;
    wr.num_sge      = 1;
    wr.send_flags   = IBV_SEND_SIGNALED;
    // if (size <= MAX_INLINE_DATA)
    //    wr.send_flags |= IBV_SEND_INLINE;

    sge.addr   = mem.addr_;
    sge.length = size;
    sge.lkey   = mem.mr->lkey;

    if (!post_send(id->qp, &wr, &bad_wr))
        throw std::runtime_error("read_rdma_async: error post_send");

    return op_info;
}

/**
 * Method that fetches value from a remote address, then adds a value to it
 * before pushing it back to the remote store.
 */
void RDMAClient::fetchadd_rdma_sync(struct rdma_cm_id *id,
        uint64_t remote_addr, uint64_t peer_rkey, uint64_t value) {
    LOG<INFO>("RDMAClient:: fetchadd_rdma_sync");
    auto op_info = fetchadd_rdma_async(id, remote_addr, peer_rkey, value);
    LOG<INFO>("RDMAClient:: waiting");

    // wait until operation is completed
    {
        TimerFunction tf("waiting semaphore", true);
        op_info->op_sem->wait();
    }

    delete op_info->op_sem;
    delete op_info;
}

/**
 * Method that fetches value from a remote address, then adds a value to it
 * before pushing it back to the remote store.
 */
RDMAClient::RDMAOpInfo* RDMAClient::fetchadd_rdma_async(struct rdma_cm_id *id,
        uint64_t remote_addr, uint64_t peer_rkey, uint64_t /*value*/) {
#if __GNUC__ >= 7
    [[maybe_unused]]
#else
    __attribute__((unused))
#endif
    auto ctx = reinterpret_cast<ConnectionContext*>(id->context);

    struct ibv_send_wr wr, *bad_wr = nullptr;
    struct ibv_sge sge;

    memset(&wr, 0, sizeof(wr));
    memset(&sge, 0, sizeof(sge));

    Lock* l = new SpinLock();
    l->wait();

    auto op_info = new RDMAClient::RDMAOpInfo(id, l);
    wr.wr_id = reinterpret_cast<uint64_t>(op_info);
    wr.opcode = IBV_WR_ATOMIC_FETCH_AND_ADD;
    wr.wr.atomic.remote_addr = remote_addr;
    wr.wr.atomic.rkey = peer_rkey;
    wr.wr.atomic.compare_add = peer_rkey;
    wr.sg_list = &sge;
    wr.num_sge = 1;
    wr.send_flags = IBV_SEND_SIGNALED;

    if (!post_send(id->qp, &wr, &bad_wr))
        throw std::runtime_error("fetchadd_rdma_async: error post_send");

    return op_info;
}

/**
 * Connects to the RDMA host at the given ip and port.
 * @param host the ip address of the host
 * @param port the port the host is listening on
 */
void RDMAClient::connect_rdma_cm(const std::string& host,
                                 const std::string& port) {
    struct addrinfo *addr;
    struct rdma_conn_param cm_params;
    struct rdma_cm_event *event = nullptr;

    TimerFunction tf("connection end-to-end time", true);

    // use connection manager to resolve address
    TEST_NZ(getaddrinfo(host.c_str(), port.c_str(), nullptr, &addr));
    ec_ = rdma_create_event_channel();
    TEST_Z(ec_ = rdma_create_event_channel());
    TEST_NZ(rdma_create_id(ec_, &id_, nullptr, RDMA_PS_TCP));
    TEST_NZ(rdma_resolve_addr(id_, nullptr, addr->ai_addr, timeout_ms_));

    LOG<INFO>("Created rdma_cm_id: ",
        reinterpret_cast<uint64_t>(id_));

    freeaddrinfo(addr);

    id_->context = &con_ctx_;
    build_params(&cm_params);

    while (rdma_get_cm_event(ec_, &event) == 0) {
        struct rdma_cm_event event_copy;

        LOG<INFO>("New rdma_get_cm_event");

        memcpy(&event_copy, event, sizeof(*event));
        rdma_ack_cm_event(event);

        if (event_copy.event == RDMA_CM_EVENT_ADDR_RESOLVED) {
            // create connection and event loop
            build_connection(event_copy.id);

            LOG<INFO>("id: ",
                reinterpret_cast<uint64_t>(event_copy.id));

            setup_memory(&con_ctx_);
            TEST_NZ(rdma_resolve_route(event_copy.id, timeout_ms_));
            LOG<INFO>("resolved route");
        } else if (event_copy.event == RDMA_CM_EVENT_ROUTE_RESOLVED) {
            LOG<INFO>("Connecting (rdma_connect) to ", host, ":", port);
            TEST_NZ(rdma_connect(event_copy.id, &cm_params));
            LOG<INFO>("id: ",
                reinterpret_cast<uint64_t>(event_copy.id));
        } else if (event_copy.event == RDMA_CM_EVENT_ESTABLISHED) {
            LOG<INFO>("RDMA_CM_EVENT_ESTABLISHED");
            LOG<INFO>("id: ",
                reinterpret_cast<uint64_t>(event_copy.id));
            break;
        } else if (event_copy.event == RDMA_CM_EVENT_UNREACHABLE) {
            LOG<ERROR>("Server unreachable.");
            exit(-1);
        } else if (event_copy.event == RDMA_CM_EVENT_REJECTED) {
            LOG<ERROR>("Connection Rejected. Server may not be running.");
            exit(-1);
        } else if (event_copy.event == RDMA_CM_EVENT_CONNECT_ERROR) {
            LOG<ERROR>("Error establishing connection.");
            exit(-1);
        } else {
           LOG<ERROR>("Unexpected event type when connecting.");
           exit(-1);
        }
    }

    LOG<INFO>("Connection successful");
}

/**
  * A function that requests a given number of bytes be allocated
  * from the server.
  * @param size the number of bytes the client is requesting be allocated
  * @return AllocationRecord for the new AllocationRecord
  */
AllocationRecord RDMAClient::allocate(uint64_t size) {
    LOG<INFO>("Allocating ",
        size, " bytes");

    // Create message using flatbuffers
    flatbuffers::FlatBufferBuilder builder(initial_buffer_size);
    auto data = message::BladeMessage::CreateAlloc(builder, size);
    auto alloc_msg = message::BladeMessage::CreateBladeMessage(builder,
                              message::BladeMessage::Data_Alloc, data.Union());
    builder.Finish(alloc_msg);

    int message_size = builder.GetSize();

    // Copy message contents into send buffer
    std::memcpy(con_ctx_.send_msg,
                builder.GetBufferPointer(),
                message_size);


    // post receive
    LOG<INFO>("Sending alloc msg size: ", message_size);
    send_receive_message_sync(id_, message_size);
    LOG<INFO>("send_receive_message_sync done: ", message_size);

    auto msg = message::BladeMessage::GetBladeMessage(con_ctx_.recv_msg);

    if (msg->data_as_AllocAck()->remote_addr() == 0) {
      // Throw error message
      LOG<ERROR>("Server threw exception when allocating memory.");
      throw cirrus::ServerMemoryErrorException("Server threw "
               "exception when allocating memory.");
    }

    AllocationRecord alloc(
                msg->data_as_AllocAck()->mr_id(),
                msg->data_as_AllocAck()->remote_addr(),
                msg->data_as_AllocAck()->peer_rkey());

    LOG<INFO>("Received allocation. mr_id: ", msg->data_as_AllocAck()->mr_id(),
            " remote_addr: " , msg->data_as_AllocAck()->remote_addr(),
            " peer_rkey: ", msg->data_as_AllocAck()->peer_rkey());

    if (msg->data_as_AllocAck()->remote_addr() == 0)
        throw std::runtime_error("Error with allocation");

    LOG<INFO>("Received allocation from Blade. remote_addr: ",
        msg->data_as_AllocAck()->remote_addr(),
        " mr_id: ", msg->data_as_AllocAck()->mr_id());
    return alloc;
}

/**
  * A function that requests a given allocation be freed in the remote store.
  * @param ar the allocation record that records the allocation to be freed.
  * @return success
  */
bool RDMAClient::deallocate(const AllocationRecord& ar) {
    LOG<INFO>("Deallocating addr: ", ar.remote_addr);

    flatbuffers::FlatBufferBuilder builder(initial_buffer_size);
    auto data = message::BladeMessage::CreateDealloc(builder, ar.remote_addr);
    auto dealloc_msg = message::BladeMessage::CreateBladeMessage(builder,
                            message::BladeMessage::Data_Dealloc, data.Union());
    builder.Finish(dealloc_msg);
    int message_size = builder.GetSize();
    // Copy message into send buffer
    std::memcpy(con_ctx_.send_msg,
                builder.GetBufferPointer(),
                message_size);


    // post receive
    LOG<INFO>("Sending dealloc msg size: ", message_size);
    send_receive_message_sync(id_, message_size);
    LOG<INFO>("send_receive_message_sync done: ", message_size);

    auto msg = message::BladeMessage::GetBladeMessage(con_ctx_.recv_msg);

    if (msg->data_as_DeallocAck()->result() == 0)
        throw std::runtime_error("Error with deallocation");
    return true;
}

/**
  * Write synchronously.
  * A function that writes data to the remote store synchronously. Reads from
  * data and passes length bytes.
  * @param alloc_rec the AllocationRecord corresponding to an adequately sized
  * allocation.
  * @param offset the offset from data at which to start reading
  * @param length the number of bytes to send
  * @param mem pointer to RDMAMem struct for this write
  * @return write success
  * @see AllocationRecord
  */
bool RDMAClient::rdma_write_sync(const AllocationRecord& alloc_rec,
        uint64_t offset,
        uint64_t length,
        const void* data,
        RDMAMem* mem) {
    LOG<INFO>("writing rdma",
        " length: ", length,
        " offset: ", offset,
        " remote_addr: ", alloc_rec.remote_addr,
        " rkey: ", alloc_rec.peer_rkey);

    if (length > SEND_MSG_SIZE)
        return false;

    if (mem) {
        {
            // TimerFunction tf("BladeClient::write_sync prepare", true);
            mem->addr_ = reinterpret_cast<uint64_t>(data);
            mem->prepare(con_ctx_.gen_ctx_);
        }

        write_rdma_sync(id_, length,
                alloc_rec.remote_addr + offset,
                alloc_rec.peer_rkey,
                *mem);

    } else {
        std::memcpy(con_ctx_.send_msg, data, length);
        write_rdma_sync(id_, length,
                alloc_rec.remote_addr + offset, alloc_rec.peer_rkey,
                default_send_mem_);
    }

    return true;
}

/**
  * Write asynchronously.
  * A function that writes data to the remote store asynchronously. Reads from
  * data and passes length bytes.
  * @param alloc_rec the AllocationRecord corresponding to an adequately sized
  * allocation.
  * @param offset the offset from data at which to start reading
  * @param length the number of bytes to send
  * @param mem pointer to RDMAMem struct for this write
  * @return std::shared_ptr<FutureBladeOp> a shared_ptr to a FutureBladeOp
  * that the caller can then extract a future from. Null if length >
  * the max sendable message size
  * @see AllocationRecord
  */
std::shared_ptr<RDMAClient::FutureBladeOp> RDMAClient::rdma_write_async(
        const AllocationRecord& alloc_rec,
        uint64_t offset,
        uint64_t length,
        const void* data,
        RDMAMem* mem) {
    LOG<INFO>("writing rdma",
        " length: ", length,
        " offset: ", offset,
        " remote_addr: ", alloc_rec.remote_addr,
        " rkey: ", alloc_rec.peer_rkey);

    if (length > SEND_MSG_SIZE)
        return nullptr;

    RDMAClient::RDMAOpInfo* op_info = nullptr;
    if (mem) {
        mem->addr_ = reinterpret_cast<uint64_t>(data);
        mem->size_ = length;
        mem->prepare(con_ctx_.gen_ctx_);

        op_info = write_rdma_async(id_, length,
                alloc_rec.remote_addr + offset,
                alloc_rec.peer_rkey,
                *mem);
    } else {
        RDMAMem* mem = new RDMAMem(data, length);

        mem->addr_ = reinterpret_cast<uint64_t>(data);
        mem->prepare(con_ctx_.gen_ctx_);

        op_info = write_rdma_async(id_, length,
                alloc_rec.remote_addr + offset,
                alloc_rec.peer_rkey,
                *mem);
    }

    return std::make_shared<RDMAClient::FutureBladeOp>(op_info);
}

/**
  * Read synchronously.
  * A function that reads data from the remote store synchronously. Reads
  * length bytes offset by offset bytes from data.
  * @param alloc_rec the AllocationRecord containing the necessary info
  * @param offset the offset from the remote_addr at which to start reading
  * @param length the number of bytes to read
  * @param data the location in local memory to write to
  * @param mem pointer to RDMAMem struct for this read
  * @return success if length is less than max receivable size
  */
bool RDMAClient::rdma_read_sync(const AllocationRecord& alloc_rec,
        uint64_t offset,
        uint64_t length,
        void *data,
        RDMAMem* mem) {
    if (length > RECV_MSG_SIZE)
        return false;

    LOG<INFO>("reading rdma",
        " length: ", length,
        " offset: ", offset,
        " remote_addr: ", alloc_rec.remote_addr,
        " rkey: ", alloc_rec.peer_rkey);

    if (mem) {
        mem->addr_ = reinterpret_cast<uint64_t>(data);
        mem->size_ = length;
        mem->prepare(con_ctx_.gen_ctx_);

        read_rdma_sync(id_, length,
                alloc_rec.remote_addr + offset,
                alloc_rec.peer_rkey, *mem);
    } else {
        read_rdma_sync(id_, length,
                alloc_rec.remote_addr + offset,
                alloc_rec.peer_rkey, default_recv_mem_);

        {
            TimerFunction tf("Memcpy time", true);
            std::memcpy(data, con_ctx_.recv_msg, length);
        }
    }

    return true;
}

/**
  * Read asynchronously.
  * A function that reads data from the remote store asynchronously. Reads
  * length bytes offset by offset bytes from the remote address.
  * @param alloc_rec the AllocationRecord containing the necessary info
  * @param offset the offset from the remote_addr at which to start reading
  * @param length the number of bytes to read
  * @param data the location in local memory to write to
  * @param mem pointer to RDMAMem struct for this read
  * @return std::shared_ptr<FutureBladeOp> a shared_ptr to a FutureBladeOp
  * that the caller can then extract a future from. Null if length >
  * the max receivable message size
  */
std::shared_ptr<RDMAClient::FutureBladeOp> RDMAClient::rdma_read_async(
        const AllocationRecord& alloc_rec,
        uint64_t offset,
        uint64_t length,
        void *data,
        RDMAMem* mem) {
    if (length > RECV_MSG_SIZE)
        return nullptr;

    LOG<INFO>("reading rdma",
        " length: ", length,
        " offset: ", offset,
        " remote_addr: ", alloc_rec.remote_addr,
        " rkey: ", alloc_rec.peer_rkey);

    RDMAClient::RDMAOpInfo* op_info;
    if (mem) {
        mem->addr_ = reinterpret_cast<uint64_t>(data);
        mem->prepare(con_ctx_.gen_ctx_);

        op_info = read_rdma_async(id_, length,
                alloc_rec.remote_addr + offset, alloc_rec.peer_rkey,
                *mem,
                []() -> void {});
    } else {
        RDMAMem* mem = new RDMAMem(data, length);

        mem->addr_ = reinterpret_cast<uint64_t>(data);
        mem->prepare(con_ctx_.gen_ctx_);

        op_info = read_rdma_async(id_, length,
                alloc_rec.remote_addr + offset, alloc_rec.peer_rkey,
                *mem,
                [mem]() -> void { delete mem; });
    }

    return std::make_shared<RDMAClient::FutureBladeOp>(op_info);
}

/**
 * A wrapper for fetchadd_rdma_sync.
 */
bool RDMAClient::fetchadd_sync(const AllocationRecord& alloc_rec,
        uint64_t offset,
        uint64_t value) {
    LOG<INFO>("fetchadd (sync) rdma",
            " offset: ", offset,
            " value: ", value);

    fetchadd_rdma_sync(id_,
            alloc_rec.remote_addr + offset, alloc_rec.peer_rkey,
            value);

    return true;
}

/**
 * A wrapper for fetchadd_rdma_async.
 */
std::shared_ptr<RDMAClient::FutureBladeOp> RDMAClient::fetchadd_async(
        const AllocationRecord& alloc_rec,
        uint64_t offset,
        uint64_t value) {
    LOG<INFO>("fetchadd (sync) rdma",
            " offset: ", offset,
            " value: ", value);

    RDMAClient::RDMAOpInfo* op_info = fetchadd_rdma_async(id_,
            alloc_rec.remote_addr + offset, alloc_rec.peer_rkey,
            value);

    return std::make_shared<RDMAClient::FutureBladeOp>(op_info);
}


/**
  * Wait for operation to finish.
  * A function that calls the FutureBladeOp's operation's wait() method.
  * Waits until the operation is complete.
  * @see RDMAOpInfo
  * @see Lock
  */
void RDMAClient::FutureBladeOp::wait() {
    op_info->op_sem->wait();
}

/**
  * Check operation status.
  * A function that calls the FutureBladeOp's operation's try_wait() method.
  * Returns instantly with the status of the operation.
  * @return true if operation is finished, false otherwise
  * @see RDMAOpInfo
  * @see Lock
  */
bool RDMAClient::FutureBladeOp::try_wait() {
    return op_info->op_sem->trywait();
}


}  // namespace cirrus
