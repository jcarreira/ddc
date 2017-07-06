#ifndef SRC_COMMON_SYNCHRONIZATION_H_
#define SRC_COMMON_SYNCHRONIZATION_H_

#include <errno.h>
#include <semaphore.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <atomic>
#include <iostream>
#include <string>
#include <stdexcept>
#include <algorithm>
#include <ctime>
#include <random>
#include <thread>
#include "common/Decls.h"
namespace cirrus {

/**
  * A class providing a general outline of a lock. Purely virtual.
  */
class Lock {
 public:
    Lock() = default;
    virtual ~Lock() = default;

    virtual void wait() = 0; /** A pure virtual member. */
    virtual void signal() = 0; /** A pure virtual member. */
    virtual void signal(int count) = 0; /** A pure virtual member. */

    /**
      * A pure virtual member.
      * @return true if lock has succeeded
      */
    virtual bool trywait() = 0;

 private:
    DISALLOW_COPY_AND_ASSIGN(Lock);
};

/**
  * A class that extends the Lock class. Makes use of sem_t and its
  * associated methods to fullfill the functions of the lock class.
  */
class PosixSemaphore : public Lock {
 public:
    explicit PosixSemaphore(int initialCount = 0) : Lock() {
        sem_name = random_string();
        m_sema = sem_open(sem_name.c_str(), O_CREAT, S_IRWXU, initialCount);
        if (m_sema == SEM_FAILED) {
            std::cout << "errno is: " << errno << std::endl;
            throw std::runtime_error("Creation of new semaphore failed");
        }
    }

    virtual ~PosixSemaphore() {
        sem_close(m_sema);
        sem_unlink(sem_name.c_str());
    }

    /**
      * Waits until entered into semaphore.
      */
    void wait() final {
        int rc = sem_wait(m_sema);
        while (rc == -1 && errno == EINTR) {
            rc = sem_wait(m_sema);
        }
    }

    /**
      * Posts to one waiter
      */
    void signal() final {
        sem_post(m_sema);
    }

    /**
      * Posts to a specified number of waiters
      * @param count number of waiters to wake
      */
    void signal(int count) final {
        while (count-- > 0) {
            sem_post(m_sema);
        }
    }

    /**
      * Attempts to lock the semaphore and returns its success.
      * @return True if the semaphore had a positive value and was decremented.
      */
    bool trywait() final {
        int ret = sem_trywait(m_sema);
        if (ret == -1 && errno != EAGAIN) {
            throw std::runtime_error("trywait error");
        }
        return ret != -1;  // true for success
    }

 private:
    /** Underlying semaphore that operations are performed on. */
    sem_t *m_sema;
    /** Name of the underlying semaphore. */
    std::string sem_name;
    /** Length of randomly names for semaphores. */
    const int rand_string_length = 16;

    /**
     * Method to generate random strings for named semaphores.
     */
    std::string random_string() {
        auto randchar = []() -> char {
            const char charset[] =
            "0123456789"
            "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
            "abcdefghijklmnopqrstuvwxyz";
            const size_t max_index = (sizeof(charset) - 1);
            return charset[ rand() % max_index ];
        };
        std::string str(rand_string_length, 0);
        // First character of name must be a slash
        str.front() = '/';
        std::generate_n(str.begin() + 1, rand_string_length, randchar);
        return str;
    }
};

/**
  * A lock that extends the Lock class. Utilizes spin waiting.
  */
class SpinLock : public Lock {
 public:
    SpinLock() :
        Lock()
    { }

    virtual ~SpinLock() = default;

    /**
      * This function busywaits until it obtains the lock.
      */
    void wait() final {
        while (lock.test_and_set(std::memory_order_acquire))
            continue;
    }

    /**
      * This function attempts to obtain the lock once.
      * @return true if the lock was obtained, false otherwise.
      */
    bool trywait() final {
        return lock.test_and_set(std::memory_order_acquire) == 0;
    }

    void signal(__attribute__((unused)) int count) final {
        throw std::runtime_error("Not implemented");
    }

    void signal() final {
        lock.clear(std::memory_order_release);
    }

 private:
    std::atomic_flag lock = ATOMIC_FLAG_INIT;
};

}  // namespace cirrus

#endif  // SRC_COMMON_SYNCHRONIZATION_H_
