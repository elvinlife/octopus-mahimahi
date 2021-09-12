#ifndef DROP_ACTIVENET_QUEUE_HH
#define DROP_ACTIVENET_QUEUE_HH
#include "dropping_packet_queue.hh"
#include <cstdio>
#include <queue>
#include <map>

class DropActiveNetQueue: public DroppingPacketQueue
{
private:
    const static unsigned int PACKET_SIZE = 1504; /* default max TUN payload size */
    FILE* log_fd;
    virtual const std::string & type( void ) const override
    {
        static const std::string type_ { "dropactivenet" };
        return type_;
    }

    int32_t msg_in_drop_;

public:
    //using DroppingPacketQueue::DroppingPacketQueue;
    DropActiveNetQueue( const std::string & args )
        : DroppingPacketQueue( args ), msg_in_drop_( -1 )
    {
    }

    ~DropActiveNetQueue()
    {
    }

    void enqueue( QueuedPacket && p ) override;
    QueuedPacket dequeue( void ) override;
    int dropStaleFrames( uint32_t, int );
};

#endif
