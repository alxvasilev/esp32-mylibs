#ifndef STREAMRINGQUEUE_HPP
#define STREAMRINGQUEUE_HPP

#include "ringQueue.hpp"
#include "waitable.hpp"

class StreamItem {
    enum Type {
        kStreamStart = 1,
        kStreamData
    };
};
template<int N>
class StreamRingQueue: public RingQueue, public Waitable {
public:

};
#endif // STREAMRINGQUEUE_HPP
