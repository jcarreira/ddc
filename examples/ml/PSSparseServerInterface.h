#ifndef PS_SPARSE_SERVER_INTERFACE_H_
#define PS_SPARSE_SERVER_INTERFACE_H_

#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <unistd.h>
#include <stdexcept>
#include <cstring>
#include <iostream>
#include <memory>
#include "ModelGradient.h"
#include "Utils.h"
#include "SparseLRModel.h"
#include "SparseMFModel.h"
#include "Model.h"

class PSSparseServerInterface {
 public:
  PSSparseServerInterface(const std::string& ip, int port);

  void send_lr_gradient(const LRSparseGradient&);
  void send_mf_gradient(const MFSparseGradient&);
  
  SparseLRModel get_lr_sparse_model(const SparseDataset& ds);
  SparseMFModel get_sparse_mf_model(const SparseDataset& ds, uint32_t, uint32_t);

  std::unique_ptr<CirrusModel> get_full_model(bool isCollaborativeFiltering); //XXX use a better argument here

 private:
  std::string ip;
  int port;
  int sock;
};

#endif //  PS_SPARSE_SERVER_INTERFACE_H_
