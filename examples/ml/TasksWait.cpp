#include <examples/ml/Tasks.h>
#include "Redis.h"
#include <iostream>
#include "config.h"


#ifdef USE_CIRRUS
void MLTask::wait_for_start(
    int index, cirrus::TCPClient& client, int nworkers) {
  std::cout << "wait_for_start index: " << index
    << std::endl;

  c_array_serializer<bool> start_counter_ser(1);
  c_array_deserializer<bool> start_counter_deser(1);
  auto t = std::make_shared<bool>(true);

  std::cout << "Connecting to CIRRUS store : " << std::endl;
  cirrus::ostore::FullBladeObjectStoreTempl<std::shared_ptr<bool>>
    start_store(IP, PORT, &client,
        start_counter_ser, start_counter_deser);

  std::cout << "Updating start index: " << index
    << std::endl;
  start_store.put(START_BASE + index, t);
  std::cout << "Updated start index: " << index << std::endl;

  int num_waiting_tasks = WORKER_TASK_RANK + nworkers;
  while (1) {
    int i = 0;
    for (i = 1; i < num_waiting_tasks; ++i) {
      std::cout << "Getting status" << std::endl;

      try {
        auto is_done = start_store.get(START_BASE + i);
        std::cout << "Checking " << i
          << " " << *is_done.get()
          << std::endl;
        if (!*is_done.get())
          break;
      } catch(const cirrus::NoSuchIDException& e) {
        break;
      }
    }
    if (i == num_waiting_tasks)
      break;
  }
  std::cout << "Done waiting: " << index
    << std::endl;
}
#elif defined(USE_REDIS)
void MLTask::wait_for_start(int index, redisContext* r, int nworkers) {
  std::cout
    << "Waiting for all workers to start. index: " << index
    << std::endl;

  char data = 1;  // bit used to indicate start
  redis_put_binary_numid(r, START_BASE + index, &data, 1);

  int num_waiting_tasks = WORKER_TASK_RANK + nworkers;
  while (1) {
    int i = 1;
    for (; i < num_waiting_tasks; ++i) {
#ifdef DEBUG
      std::cout << "Getting status i: " << i << std::endl;
#endif
      int len;  // length of object received
      char* data = redis_get_numid(r, START_BASE + i, &len);
      if (data == nullptr) {
        std::cout << "wait_for_start breaking" << std::endl;
        break;
      }
      auto is_done = bool(data[0]);
      free(data);
      if (!is_done)
        break;
    }
    if (i == num_waiting_tasks) {
      break;
    } else {
      std::cout << "Worker " << i << " not done" << std::endl;
    }
  }
  std::cout << "Worker " << index << " done waiting: " << std::endl;
}
#endif
