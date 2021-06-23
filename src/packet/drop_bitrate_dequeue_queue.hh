#ifndef DROP_BITRATE_DEQUEUE_QUEUE_HH
#define DROP_BITRATE_DEQUEUE_QUEUE_HH
#include "dropping_packet_queue.hh"
#include <cstdio>
#include <queue>
#include <map>

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

    void drop_stale_pkts_svc( uint32_t, uint32_t );
    std::map< uint32_t, int > frame_counter_;
    uint32_t msg_in_drop_;

public:
    //using DroppingPacketQueue::DroppingPacketQueue;
    DropBitrateDequeueQueue( const std::string & args )
        : DroppingPacketQueue( args ), msg_in_drop_( 0xffffffff )
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
