#ifndef DROP_SEMANTIC_PACKET_QUEUE_HH
#define DROP_SEMANTIC_PACKET_QUEUE_HH
#include "dropping_packet_queue.hh"
#include <cstdio>

class DropSemanticPacketQueue : public DroppingPacketQueue
{
private:
    FILE* log_fd;
    virtual const std::string & type( void ) const override
    {
        static const std::string type_ { "dropsemantic" };
        return type_;
    }

    void drop_stale_pkts( void );

public:
    //using DroppingPacketQueue::DroppingPacketQueue;
    DropSemanticPacketQueue( const std::string & args )
        : DroppingPacketQueue( args )
    {
        log_fd = fopen( "/home/alvin/Research/octopus/link.log", "w" );
    }

    ~DropSemanticPacketQueue() {
        fclose( log_fd );
    }

    void enqueue( QueuedPacket && p) override;
};

#endif
