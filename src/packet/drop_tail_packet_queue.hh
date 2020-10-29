/* -*-mode:c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#ifndef DROP_TAIL_PACKET_QUEUE_HH
#define DROP_TAIL_PACKET_QUEUE_HH

#include "dropping_packet_queue.hh"
#include "packet_header.hh"
#include <chrono>

using namespace std::chrono;

class DropTailPacketQueue : public DroppingPacketQueue
{
private:
    FILE* log_fd;
    virtual const std::string & type( void ) const override
    {
        static const std::string type_ { "droptail" };
        return type_;
    }

public:
    DropTailPacketQueue( const std::string & args )
        : DroppingPacketQueue( args ) 
    {
        log_fd = fopen( "/home/alvin/Research/octopus/link.log", "w" );
    }

    ~DropTailPacketQueue() {
        fclose( log_fd );
    }

    void enqueue( QueuedPacket && p ) override
    {
        if ( good_with( size_bytes() + p.contents.size(),
                    size_packets() + 1 ) ) {
            uint32_t ts = duration_cast< milliseconds >( system_clock::now().time_since_epoch() ).count();
            PacketHeader header (p.contents );
            fprintf( log_fd, "enqueue, ts: %u seq: %u frame_no: %u queue_size: %u\n",
                    ts,
                    header.seq(),
                    header.frame_no(),
                    size_packets());
            accept( std::move( p ) );
        }

        assert( good() );
    }
};

#endif /* DROP_TAIL_PACKET_QUEUE_HH */ 
