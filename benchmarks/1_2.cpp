/* Copyright Joao Carreira 2016 */

#include <unistd.h>
#include <stdlib.h>
#include <fstream>
#include <iterator>
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <map>
#include <string>
#include <cctype>
#include <chrono>
#include <thread>
#include <memory>
#include <random>

#include "src/object_store/FullBladeObjectStore.h"
#include "src/utils/Time.h"
#include "src/utils/Stats.h"

static const uint64_t GB = (1024*1024*1024);
const char PORT[] = "12345";
const char IP[] = "10.10.49.83";
static const uint32_t SIZE = 128;

struct Dummy {
    char data[SIZE];
    int id;
};

void test_async(int N) {
    cirrus::ostore::FullBladeObjectStoreTempl<Dummy> store(IP, PORT);
    cirrus::Stats stats;

    std::unique_ptr<Dummy> d = std::make_unique<Dummy>();
    cirrus::TimerFunction tfs[N];
    d->id = 42;

    std::function<bool(bool)> futures[N];

    // warm up
    for (int i = 0; i < N; ++i) {
        store.put(d.get(), sizeof(Dummy), i);
    }

    std::cout << "Warm up done" << std::endl;

    bool done[N];
    std::memset(done, 0, sizeof(done));
    int total_done = 0;

    for (int i = 0; i < N; ++i) {
        tfs[i].reset();
        futures[i] = store.put_async(d.get(), sizeof(Dummy), i);
    }

    while (total_done != N) {
        for (int i = 0; i < N; ++i) {
            if (!done[i]) {
                bool ret = futures[i](true);
                if (ret) {
                    done[i] = true;
                    total_done++;
                    auto elapsed = tfs[i].getUsElapsed();
                    stats.add(elapsed);
                }
            }
        }
    }

    std::cout << "count: " << stats.getCount() << std::endl;
    std::cout << "min: " << stats.min() << std::endl;
    std::cout << "avg: " << stats.avg() << std::endl;
    std::cout << "max: " << stats.max() << std::endl;
    std::cout << "sd: " << stats.sd() << std::endl;
    std::cout << "99%: " << stats.getPercentile(0.99) << std::endl;
}

auto main() -> int {
    // test burst of 1000 async writes
    test_async(1000);

    return 0;
}

