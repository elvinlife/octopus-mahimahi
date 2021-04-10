/* -*-mode:c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#ifndef DROP_TAIL_PACKET_QUEUE_HH
#define DROP_TAIL_PACKET_QUEUE_HH

#include "dropping_packet_queue.hh"
#include "packet_header.hh"
#include "timestamp.hh"
#include <chrono>

using namespace std::chrono;

class DropTailPacketQueue : public DroppingPacketQueue
{
private:
    virtual const std::string & type( void ) const override
    {
        static const std::string type_ { "droptail" };
        return type_;
    }

public:
    DropTailPacketQueue( const std::string & args )
        : DroppingPacketQueue( args ) 
    {
    }

    ~DropTailPacketQueue()
    {
    }

    void enqueue( QueuedPacket && p, uint32_t ) override
    {
        uint64_t ts = timestamp();
        if ( good_with( size_bytes() + p.contents.size(),
                    size_packets() + 1 ) ) {
            if ( log_fd_ ) {
                uint32_t seq = 0;
                //memcpy( &seq, p.contents.c_str() + 32, sizeof(seq) ); 
                fprintf( log_fd_, "enqueue, ts: %lu pkt_size: %ld queue_size: %u seq: %u raw_ts: %lu\n",
                        ts,
                        p.contents.size(), 
                        size_bytes(),
                        seq,
                        ts + initial_timestamp() );
            }
            accept( std::move( p ) );
        }
        else {
            if ( log_fd_ )
                fprintf( log_fd_, "drop, ts: %ld pkt_size: %ld queue_size: %u\n",
                        ts, p.contents.size(), size_bytes() );
        }

        assert( good() );
    }
};

#endif /* DROP_TAIL_PACKET_QUEUE_HH */ 
