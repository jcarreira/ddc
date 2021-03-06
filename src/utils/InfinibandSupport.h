#ifndef SRC_UTILS_INFINIBANDSUPPORT_H_
#define SRC_UTILS_INFINIBANDSUPPORT_H_

#include <rdma/rdma_cma.h>
#include <mutex>
#include "utils/logging.h"

namespace cirrus {

class InfinibandSupport {
 public:
    InfinibandSupport() = default;

    void check_mw_support(ibv_context* ctx);
    void check_odp_support(ibv_context* ctx);

    std::once_flag mw_support_check_;
    std::once_flag odp_support_check_;
};

}  // namespace cirrus

#endif  // SRC_UTILS_INFINIBANDSUPPORT_H_
