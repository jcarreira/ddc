#include <examples/ml/Tasks.h>

#include "Configuration.h"
#include "Serializers.h"
#include "Redis.h"
#include "Utils.h"
#include "S3Iterator.h"
#include "async.h"
//#include "adapters/libevent.h"

void check_redis(auto r) {
  if (r == NULL || r -> err) {
    std::cout << "[WORKER] "
      << "Error connecting to REDIS"
      << " IP: " << REDIS_IP
      << std::endl;
    throw std::runtime_error(
        "Error connecting to redis server. IP: " + std::string(REDIS_IP));
  }
}

/**
  * Works as a cache for remote model
  */
//redisContext* connect_redis() {
//  std::cout << "[WORKER] "
//    << "Worker task connecting to REDIS. "
//    << "IP: " << REDIS_IP << std::endl;
//  auto r = redis_connect(REDIS_IP, REDIS_PORT);
//  check_redis(r);
//  return r;
//

std::unique_ptr<LRModel> lr_model;

void PerformanceLambdaTask::unpack_minibatch(
    const double* minibatch,
    auto& samples, auto& labels) {
  uint64_t num_samples_per_batch = batch_size / features_per_sample;

  samples = std::shared_ptr<double>(
      new double[batch_size], std::default_delete<double[]>());
  labels = std::shared_ptr<double>(
      new double[num_samples_per_batch], std::default_delete<double[]>());

  for (uint64_t j = 0; j < num_samples_per_batch; ++j) {
    const double* data = minibatch + j * (features_per_sample + 1);
    labels.get()[j] = *data;

    if (!FLOAT_EQ(*data, 1.0) && !FLOAT_EQ(*data, 0.0))
      throw std::runtime_error(
          "Wrong label in unpack_minibatch " + std::to_string(*data));

    data++;
    std::copy(data,
        data + features_per_sample,
        samples.get() + j * features_per_sample);
  }
}

void PerformanceLambdaTask::run(const Configuration& config) {
  uint64_t num_s3_batches = config.get_limit_samples() / config.get_s3_size();

  // we use redis
  std::cout << "Connecting to redis.." << std::endl;
  //redisContext* r = connect_redis();

  // Create iterator that goes from 0 to num_s3_batches
  S3Iterator s3_iter(0, num_s3_batches, config,
      config.get_s3_size(), features_per_sample,
      config.get_minibatch_size());
  
  std::cout << "[WORKER] starting loop" << std::endl;

  lr_model.reset(new LRModel(13));
  lr_model->randomize();
  
  uint64_t count = 0;
  auto start = get_time_ns();
  while (1) {
    // maybe we can wait a few iterations to get the model
    //std::shared_ptr<double> samples;
    //std::shared_ptr<double> labels;
    
    const double* minibatch = s3_iter.get_next_fast();

    //std::cout << "building dataset" << std::endl;
    Dataset dataset(minibatch, samples_per_batch, features_per_sample);
    //std::cout << "built dataset" << std::endl;
    
    //unpack_minibatch(minibatch, samples, labels);
    //Dataset dataset(
    //    samples.get(), labels.get(),
    //    samples_per_batch, features_per_sample);
    
    // compute gradient
    //std::cout << "computing gradient" << std::endl;
    std::unique_ptr<ModelGradient> gradient;
    gradient = lr_model->minibatch_grad(dataset.samples_,
        const_cast<double*>(dataset.labels_.get()), samples_per_batch, config.get_epsilon());
        //labels.get(), samples_per_batch, config.get_epsilon());
    
    count++;
    if (count % 10000 == 0) {
      auto now = get_time_ns();
      double time_in_sec = (now - start) / 1000.0 / 1000.0 / 1000.0;
      std::cout << "Iters/sec (v4): " << count / time_in_sec << std::endl;
    }
  }
}
