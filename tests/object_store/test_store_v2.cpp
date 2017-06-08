/* Copyright Joao Carreira 2016 */

#include <unistd.h>
#include <stdlib.h>

#include "src/object_store/FullBladeObjectStore.h"
#include "src/utils/Time.h"
#include "src/utils/Stats.h"

static const uint64_t GB = (1024*1024*1024);
const char PORT[] = "12345";
const char IP[] = "10.10.49.83";

std::pair<void*, unsigned int> serializer_simple(const int& v) {
    return std::make_pair((void *) &v, sizeof(int));
}

int deserializer_simple(void* data, unsigned int /* size */) {
    int *ptr = (int *) data;

    return int(*ptr);
}


/**
  * Test simple synchronous put and get to/from the object store
  */
void test_store_simple() {
    cirrus::ostore::FullBladeObjectStoreTempl<int> store(IP, PORT,
                        serializer_simple, deserializer_simple);

    for (int oid = 0; oid <  10; oid++) {
      store.put(oid, oid);
    }

    for (int oid = 0; oid <  10; oid++) {
      int retval = store.get(oid);
      if (retval != oid) {
        throw std::runtime_error("Wrong value returned.");
      }
    }
}

auto main() -> int {
    test_store_simple();

    //TODO: add tests for other sorts of objects (structs), raw mem

    return 0;
}
