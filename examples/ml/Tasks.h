#ifndef EXAMPLES_ML_TASKS_H_
#define EXAMPLES_ML_TASKS_H_

#include <Configuration.h>

#include <client/TCPClient.h>
#include "config.h"
#include "Redis.h"
#include "LRModel.h"

#include <string>
#include <vector>

class MLTask {
  public:
    MLTask(const std::string& redis_ip,
        uint64_t redis_port,
        uint64_t MODEL_GRAD_SIZE, uint64_t MODEL_BASE,
        uint64_t LABEL_BASE, uint64_t GRADIENT_BASE,
        uint64_t SAMPLE_BASE, uint64_t START_BASE,
        uint64_t batch_size, uint64_t samples_per_batch,
        uint64_t features_per_sample, uint64_t nworkers,
        uint64_t worker_id) :
      redis_ip(redis_ip), redis_port(redis_port),
      MODEL_GRAD_SIZE(MODEL_GRAD_SIZE),
      MODEL_BASE(MODEL_BASE), LABEL_BASE(LABEL_BASE),
      GRADIENT_BASE(GRADIENT_BASE), SAMPLE_BASE(SAMPLE_BASE),
      START_BASE(START_BASE),
      batch_size(batch_size), samples_per_batch(samples_per_batch),
      features_per_sample(features_per_sample),
      nworkers(nworkers), worker_id(worker_id)
  {}

    /**
     * Worker here is a value 0..nworkers - 1
     */
    void run(const Configuration& config, int worker);

#ifdef USE_CIRRUS
    void wait_for_start(int index, cirrus::TCPClient& client, int nworkers);
#elif defined(USE_REDIS)
    void wait_for_start(int index, redisContext* r, int nworkers);
#endif

  protected:
    std::string redis_ip;
    uint64_t redis_port;
    uint64_t MODEL_GRAD_SIZE;
    uint64_t MODEL_BASE;
    uint64_t LABEL_BASE;
    uint64_t GRADIENT_BASE;
    uint64_t SAMPLE_BASE;
    uint64_t START_BASE;
    uint64_t batch_size;
    uint64_t samples_per_batch;
    uint64_t features_per_sample;
    uint64_t nworkers;
    uint64_t worker_id;
};

class LogisticTask : public MLTask {
  public:
    LogisticTask(const std::string& redis_ip, uint64_t redis_port,
        uint64_t MODEL_GRAD_SIZE, uint64_t MODEL_BASE,
        uint64_t LABEL_BASE, uint64_t GRADIENT_BASE,
        uint64_t SAMPLE_BASE, uint64_t START_BASE,
        uint64_t batch_size, uint64_t samples_per_batch,
        uint64_t features_per_sample, uint64_t nworkers,
        uint64_t worker_id) :
      MLTask(redis_ip, redis_port, MODEL_GRAD_SIZE, MODEL_BASE,
          LABEL_BASE, GRADIENT_BASE, SAMPLE_BASE, START_BASE,
          batch_size, samples_per_batch, features_per_sample,
          nworkers, worker_id)
  {}

    /**
     * Worker here is a value 0..nworkers - 1
     */
    void run(const Configuration& config, int worker);

  private:
};

class LogisticTaskS3 : public MLTask {
  public:
    LogisticTaskS3(const std::string& redis_ip, uint64_t redis_port,
        uint64_t MODEL_GRAD_SIZE, uint64_t MODEL_BASE,
        uint64_t LABEL_BASE, uint64_t GRADIENT_BASE,
        uint64_t SAMPLE_BASE, uint64_t START_BASE,
        uint64_t batch_size, uint64_t samples_per_batch,
        uint64_t features_per_sample, uint64_t nworkers,
        uint64_t worker_id) :
      MLTask(redis_ip, redis_port, MODEL_GRAD_SIZE, MODEL_BASE,
          LABEL_BASE, GRADIENT_BASE, SAMPLE_BASE, START_BASE,
          batch_size, samples_per_batch, features_per_sample,
          nworkers, worker_id)
  {}

    /**
     * Worker here is a value 0..nworkers - 1
     */
    void run(const Configuration& config, int worker);

  private:
    bool run_phase1(auto& samples, auto& labels,
        auto& model, auto r, auto& s3_iter,
        uint64_t features_per_sample);
    auto get_model(auto r, auto lmd);
    void push_gradient(auto r, int, LRGradient*);
    void unpack_minibatch(std::shared_ptr<double> /*minibatch*/,
        auto& samples, auto& labels);
};

class LogisticTaskPreloaded : public MLTask {
  public:
    LogisticTaskPreloaded(const std::string& redis_ip, uint64_t redis_port,
        uint64_t MODEL_GRAD_SIZE, uint64_t MODEL_BASE,
        uint64_t LABEL_BASE, uint64_t GRADIENT_BASE,
        uint64_t SAMPLE_BASE, uint64_t START_BASE,
        uint64_t batch_size, uint64_t samples_per_batch,
        uint64_t features_per_sample, uint64_t nworkers,
        uint64_t worker_id) :
      MLTask(redis_ip, redis_port, MODEL_GRAD_SIZE, MODEL_BASE,
          LABEL_BASE, GRADIENT_BASE, SAMPLE_BASE, START_BASE,
          batch_size, samples_per_batch, features_per_sample,
          nworkers, worker_id)
  {}
    void get_data_samples(auto r,
        uint64_t left_id, uint64_t right_id,
        auto& samples, auto& labels);

    /**
     * Worker here is a value 0..nworkers - 1
     */
    void run(const Configuration& config, int worker);

  private:
};

class PSTask : public MLTask {
  public:
    PSTask(const std::string& redis_ip, uint64_t redis_port,
        uint64_t MODEL_GRAD_SIZE, uint64_t MODEL_BASE,
        uint64_t LABEL_BASE, uint64_t GRADIENT_BASE,
        uint64_t SAMPLE_BASE, uint64_t START_BASE,
        uint64_t batch_size, uint64_t samples_per_batch,
        uint64_t features_per_sample, uint64_t nworkers,
        uint64_t worker_id);

    void run(const Configuration& config);

  private:
    void update_gradient_version(
        auto& gradient, int worker, LRModel& model, Configuration config);
    auto connect_redis();
    void put_model(LRModel model);
    void get_gradient(auto r, auto& gradient, auto gradient_id);

    bool first_time = true;
#if defined(USE_REDIS)
    redisContext* r;
    std::vector<unsigned int> gradientVersions;
#endif
};

class ErrorTask : public MLTask {
  public:
    ErrorTask(const std::string& redis_ip, uint64_t redis_port,
        uint64_t MODEL_GRAD_SIZE, uint64_t MODEL_BASE,
        uint64_t LABEL_BASE, uint64_t GRADIENT_BASE,
        uint64_t SAMPLE_BASE, uint64_t START_BASE,
        uint64_t batch_size, uint64_t samples_per_batch,
        uint64_t features_per_sample, uint64_t nworkers,
        uint64_t worker_id) :
      MLTask(redis_ip, redis_port, MODEL_GRAD_SIZE, MODEL_BASE,
          LABEL_BASE, GRADIENT_BASE, SAMPLE_BASE, START_BASE,
          batch_size, samples_per_batch, features_per_sample,
          nworkers, worker_id)
  {}
    void run(const Configuration& config);

  private:
    LRModel get_model_redis(auto r, auto lmd);
    void get_samples_labels_redis(
        auto r, auto i, auto& samples, auto& labels,
        auto cad_samples, auto cad_labels);
};

class LoadingTask : public MLTask {
  public:
    LoadingTask(const std::string& redis_ip, uint64_t redis_port,
        uint64_t MODEL_GRAD_SIZE, uint64_t MODEL_BASE,
        uint64_t LABEL_BASE, uint64_t GRADIENT_BASE,
        uint64_t SAMPLE_BASE, uint64_t START_BASE,
        uint64_t batch_size, uint64_t samples_per_batch,
        uint64_t features_per_sample, uint64_t nworkers,
        uint64_t worker_id) :
      MLTask(redis_ip, redis_port, MODEL_GRAD_SIZE, MODEL_BASE,
          LABEL_BASE, GRADIENT_BASE, SAMPLE_BASE, START_BASE,
          batch_size, samples_per_batch, features_per_sample,
          nworkers, worker_id)
  {}
    void run(const Configuration& config);

  private:
};

class LoadingTaskS3 : public MLTask {
  public:
    LoadingTaskS3(const std::string& redis_ip, uint64_t redis_port,
        uint64_t MODEL_GRAD_SIZE, uint64_t MODEL_BASE,
        uint64_t LABEL_BASE, uint64_t GRADIENT_BASE,
        uint64_t SAMPLE_BASE, uint64_t START_BASE,
        uint64_t batch_size, uint64_t samples_per_batch,
        uint64_t features_per_sample, uint64_t nworkers,
        uint64_t worker_id) :
      MLTask(redis_ip, redis_port, MODEL_GRAD_SIZE, MODEL_BASE,
          LABEL_BASE, GRADIENT_BASE, SAMPLE_BASE, START_BASE,
          batch_size, samples_per_batch, features_per_sample,
          nworkers, worker_id)
  {}
    void run(const Configuration& config);
    Dataset read_dataset(const Configuration& config);
    auto connect_redis();
    void check_loading(auto& s3_client, uint64_t s3_obj_entries);

  private:
};

#endif  // EXAMPLES_ML_TASKS_H_
