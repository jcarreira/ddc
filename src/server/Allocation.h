#ifndef _ALLOCATION_H_
#define _ALLOCATION_H_



namespace cirrus {
/**
  * This base class describes a resource
  * reserved by one app.
  */
class Allocation {
 public:
    Allocation() {}
    virtual ~Allocation() {}
 private:
};

}  // namespace cirrus

#endif  // _ALLOCATION_H_
