#include "client/RDMAClient.h"
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <errno.h>

#include <string>
#include <cstring>
#include <atomic>
#include <random>
#include <cassert>

#include "common/ThreadPinning.h"
#include "utils/utils.h"
#include "utils/Time.h"
#include "utils/logging.h"

#include "common/Synchronization.h"

namespace cirrus {

RDMAClient::RDMAClient(int timeout_ms) {
    id_ = nullptr;
    ec_ = nullptr;
    timeout_ms_ = timeout_ms;
}

RDMAClient::~RDMAClient() {
    // we can't free a running thread (?)
    // delete gen_ctx_.cq_poller_thread;
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

void RDMAClient::alloc_rdma_memory(ConnectionContext& ctx) {
    TEST_NZ(posix_memalign(reinterpret_cast<void **>(&ctx.recv_msg),
                sysconf(_SC_PAGESIZE),
                RECV_MSG_SIZE));
    TEST_NZ(posix_memalign(reinterpret_cast<void **>(&con_ctx_.send_msg),
                static_cast<size_t>(sysconf(_SC_PAGESIZE)),
                SEND_MSG_SIZE));
}

void RDMAClient::setup_memory(ConnectionContext& ctx) {
    alloc_rdma_memory(ctx);

    LOG<INFO>("Registering region with size: ",
            (RECV_MSG_SIZE / 1024 / 1024), " MB");
    TEST_Z(con_ctx_.recv_msg_mr =
            ibv_reg_mr(ctx.gen_ctx_.pd, con_ctx_.recv_msg,
                RECV_MSG_SIZE,
                IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE));

    TEST_Z(con_ctx_.send_msg_mr = ibv_reg_mr(ctx.gen_ctx_.pd, con_ctx_.send_msg,
                SEND_MSG_SIZE,
                IBV_ACCESS_LOCAL_WRITE |
                IBV_ACCESS_REMOTE_WRITE));

    default_recv_mem_ = RDMAMem(ctx.recv_msg,
            RECV_MSG_SIZE, con_ctx_.recv_msg_mr);
    default_send_mem_ = RDMAMem(ctx.send_msg,
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

int RDMAClient::post_receive(struct rdma_cm_id *id) {
    ConnectionContext *ctx =
        reinterpret_cast<ConnectionContext*>(id->context);

    LOG<INFO>("Posting receive");

    struct ibv_recv_wr wr, *bad_wr = nullptr;
    struct ibv_sge sge;

    memset(&wr, 0, sizeof(wr));

    auto op_info = new RDMAOpInfo(id, ctx->recv_sem);
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

void RDMAClient::on_completion(struct ibv_wc *wc) {
    RDMAOpInfo* op_info = reinterpret_cast<RDMAOpInfo*>(wc->wr_id);

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

    auto op_info  = new RDMAOpInfo(id, lock);
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
RDMAOpInfo* RDMAClient::write_rdma_async(struct rdma_cm_id *id, uint64_t size,
        uint64_t remote_addr, uint64_t peer_rkey, const RDMAMem& mem) {
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

    auto op_info = new RDMAOpInfo(id, l);
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
    RDMAOpInfo* op_info = read_rdma_async(id, size,
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
RDMAOpInfo* RDMAClient::read_rdma_async(struct rdma_cm_id *id, uint64_t size,
        uint64_t remote_addr, uint64_t peer_rkey, const RDMAMem& mem,
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

    auto op_info    = new RDMAOpInfo(id, l, apply_fn);
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

// Fetch and add
RDMAOpInfo* RDMAClient::fetchadd_rdma_async(struct rdma_cm_id *id,
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

    auto op_info = new RDMAOpInfo(id, l);
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

void RDMAClient::connect(const std::string& host, const std::string& port) {
    connect_rdma_cm(host, port);
    // connect_eth(host, port);
}

void RDMAClient::connect_eth(const std::string& host, const std::string& port) {
    // Get device
    int num_device;
    struct ibv_device **dev_list;
    dev_list = ibv_get_device_list(&num_device);

    if (num_device <= 0 || dev_list == nullptr) {
        throw std::runtime_error("Error in ibv_get_device_list");
    }

#ifdef DEBUG
    for (int i = 0; i < num_device; ++i) {
        std::cout << "device: ". ibv_get_device_name(dev_list[i]) << std::endl;
    }
#endif

    // we get first one
    struct ibv_device *ib_dev = dev_list[0];
    struct ibv_context* dev_ctx = ibv_open_device(ib_dev);

    if (nullptr == dev_ctx) {
        throw std::runtime_error("Error opening ib device");
    }

    LOG<INFO>("Opened ib device");

    ibv_query_device(dev_ctx, &con_ctx_.gen_ctx_.device_attr);

    // Check some information
    int ret = ibv_query_port(dev_ctx, 1, &con_ctx_.gen_ctx_.port_attr);
    ibv_query_gid(dev_ctx, 1, 0, &con_ctx_.gen_ctx_.gid);

    LOG<INFO>("Creating IB CQs");

    // creates PD
    // creates Comp channel
    // creates CQ
    // creates handling thread
    build_context(dev_ctx, &con_ctx_);
    build_qp_attr(&con_ctx_.gen_ctx_.qp_attr, &con_ctx_);

    con_ctx_.qp = ibv_create_qp(con_ctx_.gen_ctx_.pd,
                                &con_ctx_.gen_ctx_.qp_attr);

    if (nullptr == con_ctx_.qp)
        throw std::runtime_error("Error creating qp");

    LOG<INFO>("Created QP");
    // ******************
    // Connect to client
    // ******************
    LOG<INFO>("Doing eth connect");
    struct addrinfo *res;
    struct addrinfo hints;

    memset(&hints, 0, sizeof hints);
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    ret = getaddrinfo(host.c_str(), port.c_str(), &hints, &res);

    if (ret < 0) {
        throw std::runtime_error("Error getaddrinfo");
    }

    int sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);

    if (!::connect(sockfd, res->ai_addr, res->ai_addrlen)) {
        throw std::runtime_error("Error connecting to host");
    }

    freeaddrinfo(res);


    LOG<INFO>("Handshaking");
    // handshake

    struct {
        int lid        = 0;
        int qpn        = 0;
        int psn        = 0;
        int rkey       = 0;
        uint64_t vaddr = 0;
    } handshake_msg;

    handshake_msg.lid = con_ctx_.gen_ctx_.port_attr.lid;
    handshake_msg.qpn = con_ctx_.qp->qp_num;
    handshake_msg.psn = rand();
    // handshake_msg.rkey = con_ctx_.recv_msg_mr->rkey;
    // handshake_msg.vaddr = reinterpret_cast<uint64_t>(con_ctx_.recv_msg);

    ret = ::send(sockfd, &handshake_msg, sizeof(handshake_msg), 0);
    if (ret != sizeof(handshake_msg)) {
        throw std::runtime_error("Error sending handshake");
    }

    char recv_buffer[1000];
    ret = ::recv(sockfd, &recv_buffer, sizeof(recv_buffer), 0);

    if (ret <= 0)
        throw std::runtime_error("Error recv'ing");
}

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

            setup_memory(con_ctx_);
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

}  // namespace cirrus