#include <cassert>
#include "PSSparseServerInterface.h"
#include "MultiplePSInterface.h"

#undef DEBUG

#define MAX_MSG_SIZE (1024*1024)

MultiplePSInterface::MultiplePSInterface(const Configuration& config) {
  this->num_servers = config.get_num_ps();
  for (int i = 0; i < this->num_servers; i++) { 
    psint.push_back(new PSSparseServerInterface(config.get_ip(i), config.get_port(i)));
  }
}

void MultiplePSInterface::send_gradient(const LRSparseGradient* gradient) {
  // need to generalize to arbitrary num of servers
  std::vector<LRSparseGradient*> split_model = gradient->gradient_shard(num_servers);
  for (int i = 0; i < num_servers; i++)
    psint[i]->send_lr_gradient(*split_model[i]);
}


SparseLRModel MultiplePSInterface::get_lr_sparse_model(const SparseDataset& ds, const Configuration& config) {
  // Initialize variables
  SparseLRModel model(0);
  //std::unique_ptr<CirrusModel> model = std::make_unique<SparseLRModel>(0);
  // we don't know the number of weights to start with
  char** msg_lst = new char*[this->num_servers];
  char** msg_begin_lst = new char*[this->num_servers];
  uint32_t* num_weights_lst = new uint32_t[this->num_servers];
  for (int i = 0; i < this->num_servers; i++) {
    msg_lst[i] = new char[MAX_MSG_SIZE];
    msg_begin_lst[i] = msg_lst[i];
    num_weights_lst[i] = 0;
    store_value<uint32_t>(msg_lst[i], num_weights_lst[i]); // just make space for the number of weights
  }


  // Split the dataset based on which server data belongs to.
  // XXX consider optimizing this
  for (const auto& sample : ds.data_) {
    for (const auto& w : sample) {
      int server_index = w.first % this->num_servers;
      int data_index = (w.first - server_index) / num_servers;
      //std::cout << "[converted] " << w.first << " to " << data_index << std::endl;
      store_value<uint32_t>(msg_lst[server_index], data_index);
      num_weights_lst[server_index]++;
    }
  }

  // we get the model subset with just the right amount of weights
  for (int i = 0; i < config.get_num_ps(); i++) {
    psint[i]->get_lr_sparse_model_inplace_sharded(model, config, msg_begin_lst[i], num_weights_lst[i], i);
  }

  for (int i = 0; i < this->num_servers; i++) {
    delete[] msg_begin_lst[i];
  }


  delete[] msg_begin_lst;
  delete[] msg_lst;
  delete[] num_weights_lst;
  //return psint[0]->get_lr_sparse_model(ds, config);
  return model;
}


std::unique_ptr<CirrusModel> MultiplePSInterface::get_full_model() {
  //SparseLRModel model(0);
  std::unique_ptr<CirrusModel> model = std::make_unique<SparseLRModel>(0);
  // placeholder for now NOT CORRECT
  for (int i = 0; i < num_servers; i++) {
    model = psint[i]->get_full_model(false, i, num_servers, std::move(model));

  }
  return model;
}