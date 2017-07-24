#include <unistd.h>
#include <stdlib.h>
#include <iostream>
#include <chrono>
#include <thread>

#include "object_store/FullBladeObjectStore.h"
#include "tests/object_store/object_store_internal.h"
#include "cache_manager/CacheManager.h"
#include "cache_manager/LRAddedEvictionPolicy.h"
#include "utils/Time.h"
#include "utils/Stats.h"
#include "client/TCPClient.h"
#include "cache_manager/PrefetchPolicy.h"

using ObjectID = uint64_t;

// TODO(Tyler): Remove hardcoded IP and PORT
static const uint64_t GB = (1024*1024*1024);
const char PORT[] = "12345";
const char IP[] = "127.0.0.1";


/**
 * A simple custom prefetching policy that prefetches the next oid available.
 */
template<class T>
class SimpleCustomPolicy :public cirrus::PrefetchPolicy<T> {
 private:
    ObjectID first;
    ObjectID last;

 public:
    std::vector<ObjectID> get(const ObjectID& id,
        const T& /* obj */) override {
        if (id < first || id > last) {
            throw cirrus::Exception("Attempting to get id outside of "
                    "continuous range present at time of prefetch  mode "
                    "specification.");
        }
        // Math to make sure that prefetching loops back around
        // Formula is:
        // val = ((oid + i) - first) % (last - first + 1)) + first
        std::vector<ObjectID> to_return;
        ObjectID tenative_fetch = id + 1;
        ObjectID shifted = tenative_fetch - first;
        ObjectID modded = shifted % (last - first + 1);
        to_return.push_back(modded + first);

        return to_return;
    }
    /**
     * Sets the range that this policy will use.
     * @param first_ first objectID in a continuous range
     * @param last_ last objectID that will be used
     */
    void SetRange(ObjectID first_, ObjectID last_) {
        first = first_;
        last = last_;
    }
};



/**
  * Test simple synchronous put and get to/from the object store.
  * Uses simpler objects than test_fullblade_store.
  */
void test_cache_manager_simple() {
    cirrus::TCPClient client;
    cirrus::ostore::FullBladeObjectStoreTempl<int> store(IP, PORT, &client,
            cirrus::serializer_simple<int>,
            cirrus::deserializer_simple<int, sizeof(int)>);

    cirrus::LRAddedEvictionPolicy policy(10);
    cirrus::CacheManager<int> cm(&store, &policy, 10);
    for (int oid = 0; oid <  10; oid++) {
        cm.put(oid, oid);
    }

    for (int oid = 0; oid <  10; oid++) {
        int retval = cm.get(oid);
        if (retval != oid) {
            throw std::runtime_error("Wrong value returned.");
        }
    }
}

/**
  * This test tests the behavior of the cache manager when attempting to
  * get an ID that has never been put. Should throw a cirrus::NoSuchIDException.
  */
void test_nonexistent_get() {
    cirrus::TCPClient client;
    cirrus::ostore::FullBladeObjectStoreTempl<int> store(IP, PORT, &client,
            cirrus::serializer_simple<int>,
            cirrus::deserializer_simple<int, sizeof(int)>);

    cirrus::LRAddedEvictionPolicy policy(10);
    cirrus::CacheManager<int> cm(&store, &policy, 10);
    for (int oid = 0; oid <  10; oid++) {
        cm.put(oid, oid);
    }

    // Should fail
    cm.get(1492);
}

/**
  * This test tests the behavior of the cache manager when the cache is in
  * linear prefetching mode.
  */
void test_linear_prefetch() {
    cirrus::TCPClient client;
    cirrus::ostore::FullBladeObjectStoreTempl<int> store(IP, PORT, &client,
            cirrus::serializer_simple<int>,
            cirrus::deserializer_simple<int, sizeof(int)>);

    cirrus::LRAddedEvictionPolicy policy(10);
    cirrus::CacheManager<int> cm(&store, &policy, 10);

    for (int i = 0; i < 10; i++) {
        cm.put(i, i);
    }

    cm.setMode(cirrus::CacheManager<int>::kOrdered, 0, 9);
    cm.get(0);
    // Sleep for a bit to allow the items to be retrieved
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Ensure that oid 1 was prefetched
    auto start = std::chrono::system_clock::now();
    cm.get(1);
    auto end = std::chrono::system_clock::now();
    auto duration = end - start;
    auto duration_micro =
        std::chrono::duration_cast<std::chrono::microseconds>(duration);
    if (duration_micro.count() > 5) {
        std::cout << "Elapsed is: " << duration_micro.count() << std::endl;
        throw std::runtime_error("Get took too long likely not prefetched.");
    }
}

/**
  * This test tests that custom prefetching policies work properly.
  */
void test_custom_prefetch() {
    cirrus::TCPClient client;
    cirrus::ostore::FullBladeObjectStoreTempl<int> store(IP, PORT, &client,
            cirrus::serializer_simple<int>,
            cirrus::deserializer_simple<int, sizeof(int)>);

    cirrus::LRAddedEvictionPolicy policy(10);
    cirrus::CacheManager<int> cm(&store, &policy, 10);

    for (int i = 0; i < 10; i++) {
        cm.put(i, i);
    }

    SimpleCustomPolicy<int> prefetch_policy;
    prefetch_policy.SetRange(0, 9);
    cm.setMode(cirrus::CacheManager<int>::kCustom, &prefetch_policy);
    cm.get(0);
    // Sleep for a bit to allow the items to be retrieved
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Ensure that oid 1 was prefetched
    auto start = std::chrono::system_clock::now();
    cm.get(1);
    auto end = std::chrono::system_clock::now();
    auto duration = end - start;
    auto duration_micro =
        std::chrono::duration_cast<std::chrono::microseconds>(duration);
    if (duration_micro.count() > 5) {
        std::cout << "Elapsed is: " << duration_micro.count() << std::endl;
        throw std::runtime_error("Get took too long, "
            "likely not prefetched.");
    }
}

/**
  * This test tests the behavior of the cache manager when the cache is at
  * capacity. Should not exceed capacity as the policy should remove items.
  */
void test_capacity() {
    cirrus::TCPClient client;
    cirrus::ostore::FullBladeObjectStoreTempl<int> store(IP, PORT, &client,
            cirrus::serializer_simple<int>,
            cirrus::deserializer_simple<int, sizeof(int)>);

    cirrus::LRAddedEvictionPolicy policy(10);
    cirrus::CacheManager<int> cm(&store, &policy, 10);
    for (int oid = 0; oid <  15; oid++) {
        cm.put(oid, oid);
    }

    for (int oid = 0; oid < 10; oid++) {
        cm.get(oid);
    }

    // Should not fail
    cm.get(10);
}

/**
 * Tests to ensure that when the cache manager's remove() method is called
 * the given object is removed from the store as well.
 */
void test_remove() {
    cirrus::TCPClient client;
    cirrus::ostore::FullBladeObjectStoreTempl<int> store(IP, PORT, &client,
            cirrus::serializer_simple<int>,
            cirrus::deserializer_simple<int, sizeof(int)>);

    cirrus::LRAddedEvictionPolicy policy(10);
    cirrus::CacheManager<int> cm(&store, &policy, 10);

    cm.put(0, 0);

    // Remove item
    cm.remove(0);

    // Attempt to get item, this should fail
    cm.get(0);
}
/**
  * This test tests the behavior of the cache manager when instantiated with
  * a maximum capacity of zero. Should throw cirrus::CacheCapacityException.
  */
void test_instantiation() {
    cirrus::TCPClient client;
    cirrus::ostore::FullBladeObjectStoreTempl<int> store(IP, PORT, &client,
            cirrus::serializer_simple<int>,
            cirrus::deserializer_simple<int, sizeof(int)>);

    cirrus::LRAddedEvictionPolicy policy(10);
    cirrus::CacheManager<int> cm(&store, &policy, 0);
}

/**
 * This test checks the behavior of the LRAddedEvictionPolicy by ensuring
 * that it always returns the one oldest item, and only does so when at
 * capacity.
 */
void test_lradded() {
    cirrus::LRAddedEvictionPolicy policy(10);
    int i;
    for (i = 0; i < 10; i++) {
        auto ret_vec = policy.get(i);
        if (ret_vec.size() != 0) {
            std::cout << i << "id where error occured" << std::endl;
            throw std::runtime_error("Item evicted when cache not full");
         }
    }
    for (i = 10; i < 20; i++) {
        auto ret_vec = policy.get(i);
        if (ret_vec.size() != 1) {
            throw std::runtime_error("More or less than one item returned "
                    "when at capacity.");
        } else if (ret_vec.front() != i - 10) {
            throw std::runtime_error("Item returned was not oldest in "
                    "the cache.");
        }
    }
}

auto main() -> int {
    std::cout << "test starting" << std::endl;
    test_cache_manager_simple();

    try {
        test_capacity();
    } catch (const cirrus::CacheCapacityException& e) {
        std::cout << "Cache capacity exceeded when item should have been "
                     " removed by eviction policy." << std::endl;
        return -1;
    }

    try {
        test_remove();
        std::cout << "Exception not thrown after attempting to access item "
            "that should have been removed." << std::endl;
        return -1;
    } catch (const cirrus::NoSuchIDException& e) {
    }

    try {
        test_instantiation();
        std::cout << "Exception not thrown when cache"
                     " capacity set to zero." << std::endl;
        return -1;
    } catch (const cirrus::CacheCapacityException& e) {
    }

    try {
        test_nonexistent_get();
        std::cout << "Exception not thrown when get"
                     " called on nonexistent ID." << std::endl;
        return -1;
    } catch (const cirrus::NoSuchIDException & e) {
    }
    test_lradded();
    test_linear_prefetch();
    test_custom_prefetch();
    std::cout << "test successful" << std::endl;
    return 0;
}
