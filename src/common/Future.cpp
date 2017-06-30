#include <string>
#include <memory>
#include "common/Synchronization.h"
#include "common/Future.h"
#include "common/Exception.h"

namespace cirrus {

using ObjectID = uint64_t;

/**
  * Constructor for Future.
  */
Future::Future(std::shared_ptr<bool> result,
               std::shared_ptr<bool> result_available,
               std::shared_ptr<cirrus::PosixSemaphore> sem,
               std::shared_ptr<cirrus::ErrorCodes> error_code):
    result(result), result_available(result_available),
    sem(sem), error_code(error_code) {}

/**
  * Waits until the result the future is monitoring is available.
  */
void Future::wait() {
    // Wait on the semaphore as long as the result is not available
    while (!*result_available) {
        sem->wait();
    }
}

/**
  * Checks the status of the result.
  * @return Returns true if the result is available, false otherwise.
  */
bool Future::try_wait() {
    if (!*result_available) {
        bool sem_success = sem->trywait();
        return sem_success;
    } else {
        return true;
    }
}

/**
  * Returns the result of the asynchronous operation. If result is not
  * yet available, waits until it is ready.
  * @return Returns the result given by the asynchronous operation.
  */
bool Future::get() {
    // Wait until result is available
    wait();

    // Check the error code enum. Throw exception if one happened on server.
    switch (*error_code) {
      case cirrus::ErrorCodes::kOk: {
        break;
      }
      case cirrus::ErrorCodes::kException: {
        throw cirrus::Exception("Server threw generic exception.");
        break;
      }
      case cirrus::ErrorCodes::kServerMemoryErrorException: {
        throw cirrus::ServerMemoryErrorException("Server memory exhausted "
                                                 "during call to put.");
        break;
      }
      case cirrus::ErrorCodes::kNoSuchIDException: {
        throw cirrus::NoSuchIDException("Call to put was made for id that "
                                        "did not exist on server.");
        break;
      }
      default: {
        throw cirrus::Exception("Unrecognized error code during get().");
        break;
      }
    }
    return *result;
}

}  // namespace cirrus
