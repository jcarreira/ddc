#include "S3SparseIterator.h"
#include "Utils.h"
#include <unistd.h>
#include <vector>
#include <iostream>

#include <pthread.h>
#include <semaphore.h>

// s3_cad_size nmber of samples times features per sample
S3SparseIterator::S3SparseIterator(
        uint64_t left_id, uint64_t right_id, // right id is exclusive
        const Configuration& c,
        uint64_t s3_rows,
        uint64_t minibatch_rows) :
    left_id(left_id), right_id(right_id),
    conf(c), s3_rows(s3_rows),
    minibatch_rows(minibatch_rows),
    pm(REDIS_IP, REDIS_PORT),
    minibatches_list(100000)
{
      
  std::cout << "Creating S3SparseIterator"
    << " left_id: " << left_id
    << " right_id: " << right_id
    << std::endl;

  // initialize s3
  s3_initialize_aws();
  s3_client.reset(s3_create_client_ptr());

  for (uint64_t i = 0; i < read_ahead; ++i) {
    pref_sem.signal();
  }

  sem_init(&semaphore, 0, 0);

  thread = new std::thread(std::bind(&S3SparseIterator::thread_function, this, c));
}

const void* S3SparseIterator::get_next_fast() {
  // we need to delete entry
  if (to_delete != -1) {
#ifdef DEBUG
    std::cout << "get_next_fast::Deleting entry: " << to_delete
      << std::endl;
#endif
    list_strings.erase(to_delete);
#ifdef DEBUG
    std::cout << "get_next_fast::Deleted entry: " << to_delete
      << std::endl;
#endif
  }
  
  sem_wait(&semaphore);
  ring_lock.lock();

  auto ret = minibatches_list.pop();
  
  uint64_t ring_size = minibatches_list.size();
  ring_lock.unlock();

#ifdef DEBUG
  if (ret.second != -1) {
    std::cout << "get_next_fast::ret.second: " << ret.second << std::endl;
  }
#endif

  to_delete = ret.second;

  if (ring_size < 200 && pref_sem.getvalue() < (int)read_ahead) {
#ifdef DEBUG
    std::cout << "get_next_fast::pref_sem.signal!!!" << std::endl;
#endif
    pref_sem.signal();
  }

  return ret.first;
}

void S3SparseIterator::push_samples(std::ostringstream* oss) {
  uint64_t n_minibatches = s3_rows / minibatch_rows;

#ifdef DEBUG
  std::cout << "push_samples n_minibatches: " << n_minibatches << std::endl;
  auto start = get_time_us();
#endif
  // save s3 object into list of string
  list_strings[str_version] = oss->str();
  delete oss;
#ifdef DEBUG
  uint64_t elapsed_us = (get_time_us() - start);
  std::cout << "oss->str() time (us): " << elapsed_ns << std::endl;
#endif

  auto str_iter = list_strings.find(str_version);

  ring_lock.lock();
  // create a pointer to each minibatch within s3 object and push it

  const void* s3_data = reinterpret_cast<const void*>(str_iter->second.c_str());
  int s3_obj_size = load_value<int>(s3_data);
  assert(s3_obj_size > 0 && s3_obj_size < 100 * 1024 * 1024);
  int num_samples = load_value<int>(s3_data);
  assert(num_samples > 0 && num_samples < 1000000);
  //std::cout << "push_samples s3_obj_size: " << s3_obj_size << " num_samples: " << num_samples << std::endl;
  for (uint64_t i = 0; i < n_minibatches; ++i) {
    // if it's the last minibatch in object we mark it so it can be deleted
    int is_last = ((i + 1) == n_minibatches) ? str_version : -1;

    minibatches_list.add(std::make_pair(s3_data, is_last));
    sem_post(&semaphore);
  
    // advance ptr sample by sample
    for (uint64_t j = 0; j < minibatch_rows; ++j) {
      FEATURE_TYPE label = load_value<FEATURE_TYPE>(s3_data); // read label
      int num_values = load_value<int>(s3_data); 
      assert(label == 0.0 || label == 1.0);
      assert(num_values > 0 && num_values < 1000000);
    
      // advance until the next minibatch
      // every sample has index and value
      advance_ptr(s3_data, num_values * (sizeof(int) + sizeof(FEATURE_TYPE)));
    }
  }
  ring_lock.unlock();
  str_version++;
}

uint64_t get_random_obj_id(uint64_t left, uint64_t right) {
  std::random_device rd;
  std::default_random_engine re(rd());
  std::uniform_int_distribution<int> sampler(left, right - 1);
  return sampler(re);
}

void S3SparseIterator::thread_function(const Configuration& config) {
  std::cout << "Building S3 deser. with size: "
    << std::endl;

  uint64_t count = 0;
  while (1) {
    // if we can go it means there is a slot
    // in the ring
    std::cout << "Waiting for pref_sem" << std::endl;
    pref_sem.wait();

    uint64_t obj_id = get_random_obj_id(left_id, right_id);

    std::cout << "Getting object. "
      << "count: " << count++
      << " random obj id: " << obj_id
      << std::endl;

    std::ostringstream* s3_obj;
try_start:
    try {
      //std::cout << "S3SparseIterator: getting object" << std::endl;
      //auto start = get_time_us();
      s3_obj = s3_get_object_fast(obj_id, *s3_client, config.get_s3_bucket());
      //auto elapsed_us = (get_time_us() - start);
      pm.increment_batches(); // increment number of batches we have processed

      //double MBps = (1.0 * (32812.5*1024.0) / elapsed_us) / 1024 / 1024 * 1000 * 1000;
      //std::cout << "Get s3 obj took (us): " << (elapsed_us)
      //  << " size (KB): " << 32812.5
      //  << " bandwidth (MB/s): " << MBps
      //  << std::endl;
    } catch(...) {
      std::cout << "S3SparseIterator: error in s3_get_object" << std::endl;
      goto try_start;
      exit(-1);
    }
    
    uint64_t num_passes = (count / (right_id - left_id));
    if (LIMIT_NUMBER_PASSES > 0 && num_passes == LIMIT_NUMBER_PASSES) {
      exit(0);
    }

    //auto start = get_time_us();
    push_samples(s3_obj);
    //auto elapsed_us = (get_time_us() - start);
    //std::cout << "pushing took (us): " << elapsed_us << " at (us) " << get_time_us() << std::endl;
  }
}
