/* Copyright 2016 Joao Carreira */

#ifndef _BLADE_SERVER_H_
#define _BLADE_SERVER_H_

#include <rdma/rdma_cma.h>
#include "src/utils/utils.h"
#include "RDMAServer.h"
#include <vector>
#include <thread>
#include <semaphore.h>

namespace sirius {

class BladeServer : public RDMAServer {
public:
    BladeServer(int port, uint64_t pool_size,
            int timeout_ms = 500);
    virtual ~BladeServer();
    virtual void init();

private:
    void process_message(rdma_cm_id*, void* msg);
    void handle_connection(struct rdma_cm_id* id);
    void handle_disconnection(struct rdma_cm_id* id);

    void create_pool(uint64_t size);
    uint32_t create_pool2(uint64_t size, struct rdma_cm_id*);

    std::vector<void*> mr_data_;
    std::vector<ibv_mr*> mr_pool_;
    uint64_t pool_size_;

    std::once_flag pool_flag_;
};

}

#endif // _BLADE_SERVER_H_
