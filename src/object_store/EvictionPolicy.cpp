#include "object_store/EvictionPolicy.h"

namespace cirrus {

EvictionPolicy::EvictionPolicy(size_t max_num_objs) :
    max_num_objs_(max_num_objs) {
}

}  // namespace cirrus
