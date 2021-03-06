#include <Tasks_softmax.h>
#include <unistd.h>
#include <iostream>
#include <memory>

#include <ModelGradient.h>
#include <Dataset.h>
#include <client/TCPClient.h>
#include "object_store/FullBladeObjectStore.h"
#include "cache_manager/CacheManager.h"
#include "cache_manager/LRAddedEvictionPolicy.h"
#include "iterator/CirrusIterable.h"
#include "Serializers.h"
#include "Checksum.h"
#include "Input.h"
#include "Utils.h"

#define READ_AHEAD (3)

void LogisticTask::run(const Configuration& config, int worker) {
    std::cout << "[WORKER] "
        << "Worker task connecting to store" << std::endl;

    cirrus::TCPClient client;
    sm_gradient_serializer sgs(nclasses, MODEL_GRAD_SIZE);
    sm_gradient_deserializer sgd(nclasses, MODEL_GRAD_SIZE);
    sm_model_serializer sms(nclasses, MODEL_GRAD_SIZE);
    sm_model_deserializer smd(nclasses, MODEL_GRAD_SIZE);
    c_array_serializer<double> cas_samples(batch_size);
    c_array_deserializer<double> cad_samples(batch_size,
            "worker samples_store");
    c_array_serializer<double> cas_labels(samples_per_batch);
    c_array_deserializer<double> cad_labels(samples_per_batch,
            "worker labels_store", false);

    // used to publish the gradient
    cirrus::ostore::FullBladeObjectStoreTempl<SoftmaxGradient>
        gradient_store(IP, PORT, &client, sgs, sgd);

    // this is used to access the most up to date model
    cirrus::ostore::FullBladeObjectStoreTempl<SoftmaxModel>
        model_store(IP, PORT, &client, sms, smd);

    uint64_t num_batches = config.get_num_samples() /
        config.get_minibatch_size();
    std::cout << "[WORKER] "
        << "num_batches: " << num_batches
        << "batch_size: " << batch_size
        << std::endl;

    // this is used to access the training data sample
    cirrus::ostore::FullBladeObjectStoreTempl<std::shared_ptr<double>>
        samples_store(IP, PORT, &client, cas_samples, cad_samples);
    //cirrus::LRAddedEvictionPolicy samples_policy(100);
    //cirrus::CacheManager<std::shared_ptr<double>> samples_cm(
    //        &samples_store, &samples_policy, 100);
    //cirrus::CirrusIterable<std::shared_ptr<double>> s_iter(
    //      &samples_cm, READ_AHEAD, SAMPLE_BASE, SAMPLE_BASE + num_batches - 1);
    //auto samples_iter = s_iter.begin();

    // this is used to access the training labels
    // we configure this store to return shared_ptr that do not free memory
    // because these objects will be owned by the Dataset
    cirrus::ostore::FullBladeObjectStoreTempl<std::shared_ptr<double>>
        labels_store(IP, PORT, &client, cas_labels, cad_labels);
    //cirrus::LRAddedEvictionPolicy labels_policy(100);
    //cirrus::CacheManager<std::shared_ptr<double>> labels_cm(
    //        &labels_store, &labels_policy, 100);
    //cirrus::CirrusIterable<std::shared_ptr<double>> l_iter(
    //        &labels_cm, READ_AHEAD, LABEL_BASE,
    //        LABEL_BASE + num_batches - 1);
    //auto labels_iter = l_iter.begin();

    bool first_time = true;

    int samples_id = 0;
    int labels_id  = 0;
    int gradient_id = GRADIENT_BASE + worker;
    uint64_t version = 0;
    while (1) {
        // maybe we can wait a few iterations to get the model
        std::shared_ptr<double> samples;
        std::shared_ptr<double> labels;
        SoftmaxModel model(nclasses, MODEL_GRAD_SIZE);
        try {
            //std::cout << "[WORKER] "
            //    << "Worker task getting the model at id: "
            //    << MODEL_BASE
            //    << "\n";
            model = model_store.get(MODEL_BASE);

            //std::cout << "[WORKER] "
            //    << "Worker task received model csum: " << model.checksum()
            //    << std::endl
            //    << "Worker task getting the training data with id: "
            //    << (SAMPLE_BASE + samples_id)
            //    << "\n";

            samples = samples_store.get(SAMPLE_BASE + samples_id);
            //auto now = get_time_ns();
            //samples = *samples_iter;
            //std::cout << "Samples Elapsed (ns): " << get_time_ns() - now << "\n";
            //std::cout << "[WORKER] "
            //    << "Worker task received training data with id: "
            //    << (SAMPLE_BASE + samples_id)
            //    << " and checksum: " << checksum(samples, batch_size)
            //    << "\n";

            labels = labels_store.get(LABEL_BASE + labels_id);
            //labels = *labels_iter;

            //std::cout << "[WORKER] "
            //    << "Worker task received label data with id: "
            //    << samples_id
            //    << " and checksum: " << checksum(labels, samples_per_batch)
            //    << "\n";
        } catch(const cirrus::NoSuchIDException& e) {
            if (!first_time) {
                exit(0);

                // XXX fix this. We should be able to distribute
                // how many samples are there

                // wrap around
                samples_id = 0;
                labels_id = 0;
                continue;
            }
            // this happens because the ps task
            // has not uploaded the model yet
            std::cout << "[WORKER] "
                << "Model could not be found at id: "
                << MODEL_BASE
                << "\n";
            continue;
        }

        first_time = false;

        // Big hack. Shame on me
        //auto now = get_time_us();
        std::shared_ptr<double> l(new double[samples_per_batch],
                ml_array_nodelete<double>);
        std::copy(labels.get(), labels.get() + samples_per_batch, l.get());
        //std::cout << "Elapsed: " << get_time_us() - now << "\n";

        Dataset dataset(samples.get(), l.get(),
                samples_per_batch, features_per_sample);
#ifdef DEBUG
        dataset.print();
#endif

        dataset.check_values();

        // compute mini batch gradient
        std::unique_ptr<ModelGradient> gradient;
        try {
            gradient = model.minibatch_grad(0, dataset.samples_,
                    labels.get(), samples_per_batch, config.get_epsilon());
        } catch(...) {
            std::cout << "There was an error here" << std::endl;
            exit(-1);
        }
        gradient->setVersion(version++);

#ifdef DEBUG
        gradient->check_values();
        gradient->print();
#endif

        try {
            auto smg = *dynamic_cast<SoftmaxGradient*>(gradient.get());
            gradient_store.put(
                    GRADIENT_BASE + gradient_id, smg);
        } catch(...) {
            std::cout << "[WORKER] "
                << "Worker task error doing put of gradient"
                << "\n";
            exit(-1);
        }

        // move to next batch of samples
        samples_id++;
        labels_id++;

        // Wrap around
        if (samples_id == static_cast<int>(num_batches)) {
            samples_id = labels_id = 0;
        }
    }
}

/**
  * This is the task that runs the parameter server
  * This task is responsible for
  * 1) sending the model to the workers
  * 2) receiving the gradient updates from the workers
  *
  */
void PSTask::run(const Configuration& config) {
    std::cout << "[PS] "
        << "PS connecting to store" << std::endl;
    cirrus::TCPClient client;

    sm_model_serializer sms(nclasses, MODEL_GRAD_SIZE);
    sm_model_deserializer smd(nclasses, MODEL_GRAD_SIZE);
    sm_gradient_serializer sgs(nclasses, MODEL_GRAD_SIZE);
    sm_gradient_deserializer sgd(nclasses, MODEL_GRAD_SIZE);

    cirrus::ostore::FullBladeObjectStoreTempl<SoftmaxModel>
        model_store(IP, PORT, &client,
            sms, smd);
    cirrus::ostore::FullBladeObjectStoreTempl<SoftmaxGradient>
        gradient_store(IP, PORT, &client,
                sgs, sgd);

    std::cout << "[PS] "
        << "PS task initializing model" << std::endl;
    // initialize model

    SoftmaxModel model(nclasses, MODEL_GRAD_SIZE);
    model.randomize();
    //model.print();
    std::cout << "[PS] "
        << "PS publishing model at id: "
        << MODEL_BASE
        << " csum: " << model.checksum()
        << std::endl;
    // publish the model back to the store so workers can use it
    model_store.put(MODEL_BASE, model);

    std::cout << "[PS] "
        << "PS getting model"
        << " with id: " << MODEL_BASE
        << std::endl;
    auto m = model_store.get(MODEL_BASE);
    std::cout << "[PS] "
        << "PS model is here"
        << " with csum: " << m.checksum()
        << std::endl;

    // we keep a version number for the gradient produced by each worker
    std::vector<unsigned int> gradientCounts;
    gradientCounts.resize(10);

    bool first_time = true;

    while (1) {
        // for every worker, check for a new gradient computed
        // if there is a new gradient, get it and update the model
        // once model is updated publish it
        for (int worker = 0; worker < static_cast<int>(nworkers); ++worker) {
            int gradient_id = GRADIENT_BASE + worker;

            // get gradient from store
            SoftmaxGradient gradient(nclasses, MODEL_GRAD_SIZE);
            try {
                gradient = gradient_store.get(GRADIENT_BASE + gradient_id);
            } catch(const cirrus::NoSuchIDException& e) {
                if (!first_time) {
                    std::cout
                        << "PS task not able to get gradient: "
                        << std::to_string(GRADIENT_BASE + gradient_id)
                        << std::endl;
                    //throw std::runtime_error(
                    //        "PS task not able to get gradient: "
                    //        + std::to_string(GRADIENT_BASE + gradient_id));
                }
                // this happens because the worker task
                // has not uploaded the gradient yet
                worker--;
                continue;
            }

            first_time = false;

            // check if this is a gradient we haven't used before
            if (gradient.getVersion() > gradientCounts[worker]) {
                gradientCounts[worker] = gradient.getVersion();

                model.sgd_update(config.get_learning_rate(), &gradient);

                model_store.put(MODEL_BASE, model);
            }
        }
    }
}

void ErrorTask::run(const Configuration& /* config */) {
    std::cout << "Compute error task connecting to store" << std::endl;

    cirrus::TCPClient client;

    sm_model_serializer sms(nclasses, MODEL_GRAD_SIZE);
    sm_model_deserializer smd(nclasses, MODEL_GRAD_SIZE);
    c_array_serializer<double> cas_samples(batch_size);
    c_array_deserializer<double> cad_samples(batch_size,
            "error samples_store");
    c_array_serializer<double> cas_labels(samples_per_batch);
    c_array_deserializer<double> cad_labels(samples_per_batch,
            "error labels_store", false);

    // this is used to access the most up to date model
    cirrus::ostore::FullBladeObjectStoreTempl<SoftmaxModel>
        model_store(IP, PORT, &client, sms, smd);

    // this is used to access the training data sample
    cirrus::ostore::FullBladeObjectStoreTempl<std::shared_ptr<double>>
        samples_store(IP, PORT, &client, cas_samples, cad_samples);

    // this is used to access the training labels
    cirrus::ostore::FullBladeObjectStoreTempl<std::shared_ptr<double>>
        labels_store(IP, PORT, &client, cas_labels, cad_labels);

    bool first_time = true;
    while (1) {
        sleep(2);

        double loss = 0;
        try {
            // first we get the model
            //std::cout << "[ERROR_TASK] getting the model at id: "
            //    << MODEL_BASE
            //    << "\n";
            SoftmaxModel model(nclasses, MODEL_GRAD_SIZE);
            model = model_store.get(MODEL_BASE);

            //std::cout << "[ERROR_TASK] received the model with id: "
            //    << MODEL_BASE
            //    << " csum: " << model.checksum()
            //    << "\n";

            // then we compute the error
            loss = 0;
            uint64_t count = 0;

            // we keep reading until a get() fails
            for (int i = 0; 1; ++i) {
                std::shared_ptr<double> samples;
                std::shared_ptr<double> labels;

                try {
                    samples = samples_store.get(SAMPLE_BASE + i);
                    labels = labels_store.get(LABEL_BASE + i);
                } catch(const cirrus::NoSuchIDException& e) {
                    if (i == 0)
                        goto continue_point;  // no loss to be computed
                    else
                        goto compute_loss;  // we looked at all minibatches
                }

                //std::cout << "[ERROR_TASK] received data and labels with id: "
                //    << i << " with csmus: "
                //    << checksum(samples, batch_size) << " "
                //    << checksum(labels, samples_per_batch)
                //    << "\n";

                Dataset dataset(samples.get(), labels.get(),
                        samples_per_batch, features_per_sample);

                //std::cout << "[ERROR_TASK] checking the dataset"
                //        << "\n";

                dataset.check_values();

                //std::cout << "[ERROR_TASK] computing loss"
                //        << "\n";

                //std::cout << "First 10 samples labels: " << std::endl;
                //for (int k = 0; k < 10; ++k) {
                //    std::cout << "label " << k << ": " << labels.get()[k] << std::endl;
                //}
                loss += model.calc_loss(dataset);
                count++;
            }

compute_loss:
            loss = loss / count;  // compute average loss over all minibatches
        } catch(const cirrus::NoSuchIDException& e) {
            std::cout << "run_compute_error_task unknown id" << std::endl;
        }

        std::cout << "Learning loss: " << loss << std::endl;

continue_point:
        first_time = first_time;
    }
}

/**
  * Load the object store with the training dataset
  * It reads from the criteo dataset files and writes to the object store
  * It signals when work is done by changing a bit in the object store
  */
void LoadingTask::run(const Configuration& config) {
    std::cout << "[LOADER] "
        << "Read criteo input..."
        << std::endl;

    Input input;

    auto dataset = input.read_input_csv(
            config.get_input_path(),
            " ", 1,
            config.get_limit_cols(), true);  // data is already normalized

    std::cout << "First 10 labels" << std::endl;
    for (int i = 0; i < 10; ++i) {
        std::cout << "label: " << *dataset.label(i) << std::endl;
    }

    dataset.check_values();
#ifdef DEBUG
    dataset.print();
#endif
    
    c_array_serializer<double> cas_samples(batch_size);
    c_array_deserializer<double> cad_samples(batch_size,
            "loader samples_store");
    c_array_serializer<double> cas_labels(samples_per_batch);
    c_array_deserializer<double> cad_labels(samples_per_batch,
            "loader labels_store", false);

    cirrus::TCPClient client;
    cirrus::ostore::FullBladeObjectStoreTempl<std::shared_ptr<double>>
        samples_store(IP, PORT, &client, cas_samples, cad_samples);
    cirrus::ostore::FullBladeObjectStoreTempl<std::shared_ptr<double>>
        labels_store(IP, PORT, &client, cas_labels, cad_labels);

    //std::cout << "[LOADER] "
    //    << "Adding "
    //    << dataset.num_samples()
    //    << " samples in batches of size (samples*features): "
    //    << batch_size
    //    << std::endl;

    // We put in batches of N samples
    for (unsigned int i = 0; i < dataset.num_samples() / samples_per_batch; ++i) {
        //std::cout << "[LOADER] "
        //    << "Building samples batch" << std::endl;
        /** Build sample object
          */
        auto sample = std::shared_ptr<double>(
                new double[batch_size],
                std::default_delete<double[]>());
        // this memcpy can be avoided with some trickery
        std::memcpy(sample.get(), dataset.sample(i * samples_per_batch),
                sizeof(double) * batch_size);

        //std::cout << "[LOADER] "
        //    << "Building labels batch" << std::endl;
        /**
          * Build label object
          */
        auto label = std::shared_ptr<double>(
                new double[samples_per_batch],
                std::default_delete<double[]>());
        // this memcpy can be avoided with some trickery
        std::memcpy(label.get(), dataset.label(i * samples_per_batch),
                sizeof(double) * samples_per_batch);

        try {
            //std::cout << "[LOADER] "
            //    << "Adding sample batch id: "
            //    << (SAMPLE_BASE + i)
            //    << " samples with size (bytes): " << sizeof(double) * batch_size
            //    << std::endl;

            if (i == 0) {
                std::cout << "[LOADER] "
                    << "Storing sample with id: " << 0
                    << " checksum: " << checksum(sample, batch_size) << std::endl
                    << std::endl;
            }

            samples_store.put(SAMPLE_BASE + i, sample);
            // std::cout << "[LOADER] "
            //    << "Putting label" << std::endl;
            labels_store.put(LABEL_BASE + i, label);
        } catch(...) {
            std::cout << "[LOADER] "
                << "Caught exception" << std::endl;
            exit(-1);
        }
    }

    std::cout << "[LOADER] "
        << "Trying to get sample with id: " << 0
        << std::endl;

    auto sample = samples_store.get(0);

    std::cout << "[LOADER] "
        << "Got sample with id: " << 0
        << " checksum: " << checksum(sample, batch_size) << std::endl
        << "Added all samples"
        << std::endl;

    std::cout << "[LOADER] "
        << "Print sample 0"
        << std::endl;

#ifdef DEBUG
    for (int i = 0; i < batch_size; ++i) {
        double val = sample.get()[i];
        std::cout << val << " ";
    }
#endif
    std::cout << std::endl;
}

