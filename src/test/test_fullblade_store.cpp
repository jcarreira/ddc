/* Copyright Joao Carreira 2016 */

#include <stdlib.h>
#include <fstream>
#include <iterator>
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <map>
#include <string>
#include <cctype>

#include "src/object_store/FullBladeObjectStore.h"

static const uint64_t GB = (1024*1024*1024);
const char PORT[] = "12345";

struct Dummy {
    char data[15];
    int id;
};

int main() {
    sirius::FullBladeObjectStore store("10.10.49.87", PORT);

    void* d = reinterpret_cast<Dummy*>(new Dummy);
    reinterpret_cast<Dummy*>(d)->id = 42;

    try {
        store.put(&d, sizeof(Dummy), 1);
    } catch(...) {
        std::cerr << "Error inserting into hash table" << std::endl;
    }

    void *d2;
    store.get(1, d2);

    // should be 42
    std::cout << "d2.id: " << reinterpret_cast<Dummy*>(d2)->id << std::endl;

    return 0;
}

