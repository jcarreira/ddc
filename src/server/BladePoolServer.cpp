#include <unistd.h>
#include <errno.h>
#include "server/BladePoolServer.h"
#include "utils/logging.h"
#include "utils/CirrusTime.h"
#include "utils/InfinibandSupport.h"
#include "common/schemas/BladeMessage_generated.h"

namespace cirrus {

static const int SIZE = 1000000;
static const int initial_buffer_size = 50;

BladePoolServer::BladePoolServer(int port,
        uint64_t pool_size,
        int timeout_ms) :
    RDMAServer(port, timeout_ms),
    mr_data_(0),
    mr_pool_(0),
   pool_size_(pool_size) {
}

void BladePoolServer::init() {
    // let upper layer initialize
    // all network related things
    RDMAServer::init();
}

void BladePoolServer::handle_connection(struct rdma_cm_id* /* id */) {
}

void BladePoolServer::handle_disconnection(struct rdma_cm_id* /* id */) {
}

uint32_t BladePoolServer::create_pool(uint64_t size,
        struct rdma_cm_id* /*id*/) {
    TimerFunction tf("create_pool", true);

    LOG<INFO>("Allocating memory pool of size: ",
        (size/1024/1024), "MB ",
        (size/1024/1024/1024), "GB");

    void *data;
    TEST_NZ(posix_memalign(reinterpret_cast<void **>(&data),
                static_cast<size_t>(sysconf(_SC_PAGESIZE)), size));
    mr_data_.push_back(data);

    LOG<INFO>("Creating memory region");
    ibv_mr* pool;
    TEST_Z(pool = ibv_reg_mr(
                gen_ctx_.pd, data, size,
                IBV_ACCESS_LOCAL_WRITE |
                IBV_ACCESS_REMOTE_WRITE |
                IBV_ACCESS_REMOTE_READ));

    mr_pool_.push_back(pool);

    LOG<INFO>("Memory region created");

    return 0;
}

void BladePoolServer::process_message(rdma_cm_id* id,
        void* message) {
    auto msg = message::BladeMessage::GetBladeMessage(message);
    auto ctx = reinterpret_cast<ConnectionContext*>(id->context);

    LOG<INFO>("Received message");

    // we should do this as earlier as possible (closer to when)
    // process starts but cant make it work like that (yet)
    std::call_once(pool_flag_,
            &BladePoolServer::create_pool, this, pool_size_, id);

    switch (msg->data_type()) {
        case  message::BladeMessage::Data_Alloc:
            {
                LOG<INFO>("ALLOC");

                // we always return pointer to big memory pool

                uint64_t mr_id = 42;
                uint64_t remote_addr =
                    reinterpret_cast<uint64_t>(mr_data_[0]);

                LOG<INFO>("Sending ack. ",
                    " remote_addr: ", remote_addr,
                    " rkey: ", mr_pool_[0]->rkey);



                flatbuffers::FlatBufferBuilder builder(initial_buffer_size);

                auto data =  message::BladeMessage::CreateAllocAck(builder,
                                           mr_id,
                                           remote_addr,
                                           mr_pool_[0]->rkey);

                auto alloc_ack_msg =
                              message::BladeMessage::CreateBladeMessage(builder,
                              message::BladeMessage::Data_AllocAck,
                              data.Union());

                builder.Finish(alloc_ack_msg);

                int message_size = builder.GetSize();
                // Copy message into send buffer
                std::memcpy(ctx->send_msg,
                            builder.GetBufferPointer(),
                            message_size);

                // send async message
                send_message(id, message_size);

                break;
            }
        default:
            LOG<ERROR>("Unknown message");
            exit(-1);
            break;
    }
}

}  // namespace cirrus
