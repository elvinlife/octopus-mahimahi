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

    void enqueue( QueuedPacket && p ) override
    {
        uint64_t ts = timestamp();
        PacketHeader header ( p.contents );
        if ( log_fd_ ) {
            if ( header.is_udp() ) {
                fprintf( log_fd_, "enqueue, UDP ts: %ld pkt_size: %ld queue_size: %u seq: %d\n",
                        ts, p.contents.size(),
                        size_bytes(),
                        header.seq() );
            }
            else {
                fprintf( log_fd_, "enqueue, TCP ts: %ld pkt_size: %ld queue_size: %u\n",
                        ts, p.contents.size(),
                        size_bytes() );
            }
        }
        if ( good_with( size_bytes() + p.contents.size(),
                    size_packets() + 1 ) ) {
            accept( std::move( p ) );
        }
        else {
            if ( log_fd_ )
                fprintf( log_fd_, "drop, ts: %lu pkt_size: %ld queue_size: %u type: %s\n",
                        ts,
                        p.contents.size(),
                        size_bytes(),
                        header.is_udp() ? "UDP" : "TCP"
                        );
        }

        assert( good() );
    }
};

#endif /* DROP_TAIL_PACKET_QUEUE_HH */ 
