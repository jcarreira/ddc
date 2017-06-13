/* Copyright 2016 Joao Carreira */

#ifndef _FULLBLADE_OBJECT_STORE_H_
#define _FULLBLADE_OBJECT_STORE_H_

#include <string>
#include <iostream>

#include "src/object_store/ObjectStore.h"
#include "src/client/BladeClient.h"
#include "src/utils/utils.h"
#include "src/utils/Time.h"
#include "src/utils/logging.h"
#include "src/common/Exception.h"

#include "third_party/libcuckoo/src/cuckoohash_map.hh"
#include "third_party/libcuckoo/src/city_hasher.hh"

/*
 * This store keeps objects in blades at all times
 * method get copies objects from blade to local nodes
 * method put copies object from local dram to remote blade
 */

namespace cirrus {
namespace ostore {

class BladeLocation {
public:
    BladeLocation(uint64_t sz, const AllocationRecord& ar) :
        size(sz), allocRec(ar) {}
    explicit BladeLocation(uint64_t sz = 0) :
        size(sz) {}

    uint64_t size;
    AllocationRecord allocRec;
};

template<class T>
class FullBladeObjectStoreTempl : public ObjectStore<T> {
public:
    FullBladeObjectStoreTempl(const std::string& bladeIP,
                            const std::string& port,
            std::function<std::pair<void*, unsigned int>(const T&)> serializer,
            std::function<T(void*,unsigned int)> deserializer);

    T get(const ObjectID& oid) const override;
    bool get(ObjectID, T*) const;
    std::function<bool(bool)> get_async(ObjectID, T*) const;

    bool getHandle(ObjectID, BladeLocation& bl) const;

    bool getHandle(const ObjectID&, BladeLocation&) const;

    bool put(ObjectID id, T obj, RDMAMem* mem = nullptr);
    std::function<bool(bool)> put_async(Object, uint64_t, ObjectID);
    virtual void printStats() const noexcept override;

    bool remove(ObjectID);

private:
    bool readToLocal(BladeLocation loc, void*) const;
    std::shared_ptr<FutureBladeOp> readToLocalAsync(BladeLocation loc,
            void* ptr) const;

    bool writeRemote(Object obj, BladeLocation loc, RDMAMem* mem = nullptr);
    std::shared_ptr<FutureBladeOp> writeRemoteAsync(Object obj,
            BladeLocation loc);
    bool insertObjectLocation(ObjectID id,
            uint64_t size, const AllocationRecord& allocRec);

    // hash to map oid and location
    // if oid is not found, object is not in store
    cuckoohash_map<ObjectID, BladeLocation, CityHasher<ObjectID> > objects_;
    mutable BladeClient client;

    uint64_t serialized_size;

    /* The pointer returned by the serializer should be obtained from malloc. */
    std::function<std::pair<void*, unsigned int>(const T&)> serializer;
    /* Should return a new object based on the buffer passed in. */
    std::function<T(void*, unsigned int)> deserializer;
};

template<class T>
FullBladeObjectStoreTempl<T>::FullBladeObjectStoreTempl(const std::string& bladeIP,
        const std::string& port,
        std::function<std::pair<void*, unsigned int>(const T&)> serializer,
        std::function<T(void*, unsigned int)> deserializer) :
    ObjectStore<T>(), serializer(serializer), deserializer(deserializer) {
    client.connect(bladeIP, port);
}

template<class T>
T FullBladeObjectStoreTempl<T>::get(const ObjectID& id) const {
    BladeLocation loc;
    if (objects_.find(id, loc)) {
        /* This is safe as we will only reach here if a previous put has
           occured, thus setting the value of serialized_size. */

        /* In the case of a failed alloc, will throw std::bad_alloc. This
           allocation provides a buffer to read the serialized object into. */
        void* ptr = ::operator new (serialized_size);

        // Read into the section of memory you just allocated
        readToLocal(loc, ptr);

        // Deserialize the memory at ptr and return an object
        T retval = deserializer(ptr, serialized_size);

        /* Cast back to char pointer to allow for deletion. */
        ::operator delete (ptr);
        return retval;
    } else {
        throw cirrus::Exception("Requested ObjectID does not exist remotely.");
    }
}

template<class T>
bool FullBladeObjectStoreTempl<T>::getHandle(const ObjectID& id, BladeLocation& loc) const {
    if (objects_.find(id, loc)) {
        return true;
    } else {
        return false;
    }
}

template<class T>
std::function<bool(bool)>
FullBladeObjectStoreTempl<T>::get_async(ObjectID id, T* ptr) const {
    BladeLocation loc;
    if (objects_.find(id, loc)) {
        auto future = readToLocalAsync(loc, reinterpret_cast<void*>(ptr));

        auto fun = [future](bool try_wait) -> bool {
            if (try_wait) {
                return future->try_wait();
            } else {
                future->wait();
                return true;
            }
        };
        return fun;
    } else {
        // object is not in store
        return std::function<bool(bool)>();
    }
}

template<class T>
bool FullBladeObjectStoreTempl<T>::put(ObjectID id, T obj, RDMAMem* mem) {
    BladeLocation loc;

    // Approach: serialize object passed in, push it to oid
    // serialized_size is saved in the class, it is the size of pushed objects

    // TODO: convert to smart pointers?
    std::pair<void*, unsigned int> serializer_out = this->serializer(obj);
    void * serial_ptr = serializer_out.first;
    this->serialized_size = serializer_out.second;
    bool retval;
    if (objects_.find(id, loc)) {
        retval = writeRemote(serial_ptr, loc, mem);
    } else {
        // we could merge this into a single message (?)
        cirrus::AllocationRecord allocRec;
        {
            TimerFunction tf("FullBladeObjectStoreTempl::put allocate", true);
            allocRec = client.allocate(this->serialized_size);
        }
        insertObjectLocation(id, this->serialized_size, allocRec);
        LOG<INFO>("FullBladeObjectStoreTempl::writeRemote after alloc");
        retval = writeRemote(serial_ptr,
                          BladeLocation(this->serialized_size, allocRec), mem);
    }
    free(serial_ptr);
    return retval;

}

template<class T>
std::function<bool(bool)>
FullBladeObjectStoreTempl<T>::put_async(Object obj, uint64_t size, ObjectID id) {
    BladeLocation loc;

    if (!objects_.find(id, loc)) {
        cirrus::AllocationRecord allocRec = client.allocate(size);
        insertObjectLocation(id, size, allocRec);
        loc = BladeLocation(size, allocRec);
    }

    auto future = writeRemoteAsync(obj, loc);

    //TimerFunction tf("create lambda", true);
    auto fun = [future](bool try_wait) -> bool {
        if (try_wait) {
            return future->try_wait();
        } else {
            future->wait();
            return true;
        }
    };
    return fun;
}

template<class T>
bool FullBladeObjectStoreTempl<T>::remove(ObjectID id) {
    BladeLocation loc;
    if (objects_.find(id, loc)) {
        client.deallocate(loc.allocRec);
    } else {
        throw std::runtime_error("Error. Trying to do inexistent object");
    }
}

template<class T>
bool FullBladeObjectStoreTempl<T>::readToLocal(BladeLocation loc, void* ptr) const {
    RDMAMem mem(ptr, loc.size);
    client.read_sync(loc.allocRec, 0, loc.size, ptr, &mem);
    return true;
}

template<class T>
std::shared_ptr<FutureBladeOp> FullBladeObjectStoreTempl<T>::readToLocalAsync(
        BladeLocation loc, void* ptr) const {
    auto future = client.read_async(loc.allocRec, 0, loc.size, ptr);
    return future;
}

template<class T>
bool FullBladeObjectStoreTempl<T>::writeRemote(Object obj,
        BladeLocation loc, RDMAMem* mem) {

//    LOG<INFO>("FullBladeObjectStoreTempl::writeRemote");
    RDMAMem mm(obj, loc.size);

    {
        //TimerFunction tf("writeRemote", true);
        //LOG<INFO>("FullBladeObjectStoreTempl:: writing sync");
        client.write_sync(loc.allocRec, 0, loc.size, obj,
                nullptr == mem ? &mm : mem);
    }
    return true;
}

template<class T>
std::shared_ptr<FutureBladeOp> FullBladeObjectStoreTempl<T>::writeRemoteAsync(
        Object obj, BladeLocation loc) {
    auto future = client.write_async(loc.allocRec, 0, loc.size, obj);
    return future;
}

template<class T>
bool FullBladeObjectStoreTempl<T>::insertObjectLocation(ObjectID id,
        uint64_t size, const AllocationRecord& allocRec) {
    objects_[id] = BladeLocation(size, allocRec);

    return true;
}

template<class T>
void FullBladeObjectStoreTempl<T>::printStats() const noexcept {
}

}
}

#endif // _FULLBLADE_OBJECT_STORE_H_
