#ifndef SRC_COMMON_FILEALLOCATIONRECORD_H_
#define SRC_COMMON_FILEALLOCATIONRECORD_H_

namespace cirrus {

/**
  * A struct that contains information on where to find an allocated file.
  */
struct FileAllocationRecord {
    /**
      * The remote address where this file is stored.
      */
    uint64_t remote_addr; /** The remote address where this file is stored. */
    /**
      * The peer_rkey for this file.
      */
    uint64_t peer_rkey;

    FileAllocationRecord(uint64_t remote_addr_, uint64_t peer_rkey_) :
        remote_addr(remote_addr_), peer_rkey(peer_rkey_)
    {}
};

}  // namespace cirrus

#endif  // SRC_COMMON_FILEALLOCATIONRECORD_H_
