#ifndef DROP_BITRATE_QUEUE_HH
#define DROP_BITRATE_QUEUE_HH
#include "dropping_packet_queue.hh"
#include <cstdio>
#include <queue>

class DropBitrateQueue: public DroppingPacketQueue
{
private:
    const static unsigned int PACKET_SIZE = 1504; /* default max TUN payload size */
    FILE* log_fd;
    virtual const std::string & type( void ) const override
    {
        static const std::string type_ { "dropbitrate" };
        return type_;
    }

    void drop_stale_pkts_svc( uint32_t, uint32_t );

public:
    //using DroppingPacketQueue::DroppingPacketQueue;
    DropBitrateQueue( const std::string & args )
        : DroppingPacketQueue( args )
    {
    }

    ~DropBitrateQueue()
    {
    }

    void enqueue( QueuedPacket && p ) override;
};

#endif
