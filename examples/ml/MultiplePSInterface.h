#ifndef PS_SPARSE_SERVER_INTERFACE_WRAPPER_H_
#define PS_SPARSE_SERVER_INTERFACE_WRAPPER_H_

#include "ModelGradient.h"
#include "SparseLRModel.h"
#include "PSSparseServerInterface.h"

class MultiplePSInterface {
 public:
  MultiplePSInterface(const Configuration& config);

  void send_gradient(const LRSparseGradient*);

  //void get_model(SparseLRModel& model);
  std::unique_ptr<CirrusModel> get_full_model(bool isCollaborative);
  SparseLRModel get_lr_sparse_model(const SparseDataset& ds, const Configuration& config);


 private:
  std::vector<std::shared_ptr<PSSparseServerInterface>> psint;
  int num_servers;
  int nusers;
  int nitems;
};

#endif //  PS_SPARSE_SERVER_INTERFACE_WRAPPER_H_
