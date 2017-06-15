#ifndef _CIRRUS_ITERABLE_H_
#define _CIRRUS_ITERABLE_H_

#include "src/cache_manager/CacheManager.h"

namespace cirrus {
using ObjectID = uint64_t;

/**
  * A class that interfaces with the cache manager. Returns cirrus::Iterator
  * objects for iteration.
  */
template<class T>
class CirrusIterable {
    template<class C> class Iterator; 
 public:
    CirrusIterable<T>::Iterator<T> begin();
    CirrusIterable<T>::Iterator<T> end();

    /**
      * Constructor for the CirrusIterable class. Assumes that all objects
      * are stored sequentially between first and last.
      * @param cm a pointer to a CacheManager with that contains the same
      * object type as this Iterable.
      * @param first the first sequential objectID. Should always be <= than
      * last.
      * @param the last sequential id under which an object is stored. Should
      * always be >= first.
      * @param readAhead how many items ahead items should be prefetched.
      * Should always be <= last - first. Additionally, should be less than
      * the cache capacity that was specified in the creation of the
      * CacheManager.
      */
    CirrusIterable<T>(cirrus::CacheManager<T>* cm,
                                 unsigned int readAhead,
                                 ObjectID first,
                                 ObjectID last):
                                 cm(cm), readAhead(readAhead), first(first),
                                 last(last) {}

 private:
    /**
      * A class that interfaces with the cache manager. Making an access will
      * prefetch a user defined distance ahead
      */
    template<class C>
    class Iterator {
     public:
       /**
         * Constructor for the Iterator class. Assumes that all objects
         * are stored sequentially.
         * @param cm a pointer to a CacheManager with that contains the same
         * object type as this Iterable.
         * @param readAhead how many items ahead items should be prefetched.
         * @param first the first sequential objectID. Should always be <= than
         * last.
         * @param the last sequential id under which an object is stored. Should
         * always be >= first.
         * @param current_id the id that will be fetched when the iterator is
         * dereferenced.
         */
        Iterator(cirrus::CacheManager<C>* cm,
                                    unsigned int readAhead, ObjectID first,
                                    ObjectID last, ObjectID current_id):
                                    cm(cm), readAhead(readAhead), first(first),
                                    last(last), current_id(current_id) {}

        C operator*();
        Iterator& operator++();
        Iterator& operator++(int i);
        bool operator!=(const Iterator& it) const;
        bool operator==(const Iterator& it) const;
        ObjectID get_curr_id() const;

     private:
        cirrus::CacheManager<C> *cm;
        unsigned int readAhead;
        ObjectID first;
        ObjectID last;
        ObjectID current_id;
    };

    cirrus::CacheManager<T> *cm;
    unsigned int readAhead;
    ObjectID first;
    ObjectID last;
};

/**
  * Function that returns a cirrus::Iterator at the start of the given range.
  */
template<class T>
CirrusIterable<T>::Iterator<T> CirrusIterable<T>::begin() {
    return CirrusIterable<T>::Iterator<T>(cm, readAhead, first, last, first);
}

/**
  * Function that returns a cirrus::Iterator one past the end of the given
  * range.
  */
template<class T>
CirrusIterable<T>::Iterator<T> CirrusIterable<T>::end() {
  return CirrusIterable<T>::Iterator<T>(cm, readAhead, first, last, last + 1);
}


template<class C>
C CirrusIterable<C>::Iterator<C>::operator*() {
  // Attempts to get the next readAhead items.
  for (unsigned int i = 1; i <= readAhead; i++) {
    // Math to make sure that prefetching loops back around
    // Formula is val = ((current_id + i) - first) % (last - first)) + first
    ObjectID tenative_fetch = current_id + i;  // calculate what we WOULD fetch
    ObjectID shifted = tenative_fetch - first;  // shift relative to first
    ObjectID modded = shifted % (last - first);  // Mod relative to shifted last
    ObjectID to_fetch = modded + first;  // Add back to first for final result
    cm->prefetch(to_fetch);
  }

  return cm->get(current_id);
}

/**
  * A function that increments the Iterator by increasing the value of
  * current_id. The next time the Iterator is dereferenced, an object stored
  * under the incremented current_id will be retrieved.
  */
template<class C>
CirrusIterable<C>::Iterator<C>& Iterator<C>::operator++() {
  current_id++;
  return *this;
}


/**
  * A function that increments the Iterator by increasing the value of
  * current_id. The next time the Iterator is dereferenced, an object stored
  * under the incremented current_id will be retrieved.
  */
template<class C>
CirrusIterable<C>::Iterator<C>& Iterator<C>::operator++(int /* i */) {
  current_id++;
  return *this;
}

/**
  * A function that compares two Iterators. Will return true if the two
  * iterators have different values of current_id.
  */
template<class C>
bool CirrusIterable<C>::Iterator<C>::operator!=(const CirrusIterable<T>::Iterator<C>& it) const {
  return current_id != it.get_curr_id();
}

/**
  * A function that compares two Iterators. Will return true if the two
  * iterators have identical values of current_id.
  */
template<class C>
bool Iterator<C>::operator==(const Iterator<C>& it) const {
  return current_id == it.get_curr_id();
}

/**
  * A function that returns the current_id of the Iterator that calls it.
  */
template<class C>
ObjectID Iterator<C>::get_curr_id() const {
  return current_id;
}


}  // namespace cirrus

#endif  // _CIRRUS_ITERABLE_H_
