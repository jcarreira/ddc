#include <stdlib.h>
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <cctype>
#include <thread>
#include <memory>
#include <random>

#include "object_store/FullBladeObjectStore.h"
#include "tests/object_store/object_store_internal.h"
#include "common/Synchronization.h"
#include "utils/Time.h"
#include "client/TCPClient.h"

// TODO(Tyler): Remove hardcoded IP and PORT
static const uint64_t GB = (1024*1024*1024);
static const uint64_t MB = (1024*1024);
static const int N_THREADS = 20;
const char PORT[] = "12345";
const char IP[] = "127.0.0.1";
static const uint32_t SIZE = 1024;

uint64_t total_puts = 0;
cirrus::SpinLock total_puts_lock;

/**
 * Checks that the system works properly when multiple clients get and put.
 * These clients each use their own local store and client instances.
 */
void test_multiple_clients() {
    cirrus::TimerFunction tf("connect time", true);

    std::thread* threads[N_THREADS];

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(1, 10);
    int start = 0;
    int stop = 10;
    for (int i = 0; i < N_THREADS; ++i) {
        threads[i] = new std::thread([dis, gen, start, stop]() {
            cirrus::TCPClient client;
            cirrus::ostore::FullBladeObjectStoreTempl<cirrus::Dummy<SIZE>>
                store(IP, PORT, &client,
                    cirrus::serializer_simple<cirrus::Dummy<SIZE>>,
                    cirrus::deserializer_simple<cirrus::Dummy<SIZE>,
                        sizeof(cirrus::Dummy<SIZE>)>);
            for (int i = start; i < stop; i++) {
                int rnd = std::rand();
                struct cirrus::Dummy<SIZE> d(rnd);
                store.put(i, d);

                struct cirrus::Dummy<SIZE> d2 = store.get(i);

                if (d2.id != rnd) {
                    std::cout << "Expected " << rnd << " but got " << d2.id
                        << std::endl;
                    throw std::runtime_error("Wrong value returned.");
                }
                total_puts_lock.wait();
                total_puts++;
                total_puts_lock.signal();
            }
        });
        start += 10;
        stop += 10;
    }

    for (int i = 0; i < N_THREADS; ++i)
        threads[i]->join();

    std::cout << "Total puts: " << total_puts << std::endl;
}

auto main() -> int {
    test_multiple_clients();

    return 0;
}
