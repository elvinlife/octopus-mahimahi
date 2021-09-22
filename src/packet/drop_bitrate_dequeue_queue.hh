#ifndef DROP_BITRATE_DEQUEUE_QUEUE_HH
#define DROP_BITRATE_DEQUEUE_QUEUE_HH
#include "dropping_packet_queue.hh"
#include <cstdio>
#include <queue>
#include <map>
#include <utility>

using Trace = std::pair<uint64_t, int>;

class DropBitrateDequeueQueue: public DroppingPacketQueue
{
private:
    const static unsigned int PACKET_SIZE = 1504; /* default max TUN payload size */
    FILE* log_fd;
    virtual const std::string & type( void ) const override
    {
        static const std::string type_ { "dropbitrate_dequeue" };
        return type_;
    }

    std::map<uint16_t, std::map< int32_t, int32_t >> frame_counter_;
    std::map<uint16_t, int32_t>             msg_in_drop_;
    std::map<uint16_t, int32_t [8]>         prio_to_droppers_;
    std::map<uint16_t, std::deque<Trace>>   enqueue_trace_;
    std::map<uint16_t, int>                 enqueue_rate_;
    //int32_t msg_in_drop_;
    //int32_t prio_to_droppers_[8];

public:
    //using DroppingPacketQueue::DroppingPacketQueue;
    DropBitrateDequeueQueue( const std::string & args )
        : DroppingPacketQueue( args )
    {
    }

    ~DropBitrateDequeueQueue()
    {
    }

    void enqueue( QueuedPacket && p ) override;
    QueuedPacket dequeuefront( void );
    QueuedPacket dequeue( void ) override;
};

#endif
