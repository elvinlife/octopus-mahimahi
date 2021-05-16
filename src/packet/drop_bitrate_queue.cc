#include "packet_header.hh"
#include "drop_bitrate_queue.hh"
#include "timestamp.hh"
#include <map>
#include <chrono>

using namespace std::chrono;

void DropBitrateQueue::enqueue( QueuedPacket && p ) {
    if ( good_with( size_bytes() + p.contents.size(),
                size_packets() + 1 ) ) {
        uint64_t ts = timestamp();
        PacketHeader header ( p.contents );
        if ( log_fd_ ) {
            if ( header.is_udp() ) {
                fprintf( log_fd_, "enqueue, UDP ts: %lu pkt_size: %ld queue_size: %u seq: %u msg_no: %u bitrate: %u bw: %u\n",
                        ts, p.contents.size(),
                        size_bytes(),
                        header.seq(),
                        header.msg_no(),
                        header.bitrate(),
                        bandwidth_
                        );
            }
            else {
                fprintf( log_fd_, "enqueue, TCP ts: %ld pkt_size: %ld queue_size: %u\n",
                        ts, p.contents.size(),
                        size_bytes() );
            }
        }
        if ( !header.is_udp() ) {
            accept( std::move( p ) );
            assert( good() );
            return;
        }
        if ((uint32_t)bandwidth_ >= header.bitrate()) {
            accept( std::move( p ) );
        }
        else {
            if ( log_fd_ ) {
                if ( header.pkt_pos() == FIRST || header.pkt_pos() == SOLO ) {
                    fprintf( log_fd_, "drop_msg msg_no: %d\n", header.msg_no() );
                }
                fprintf( log_fd_, "sema-drop, ts: %lu seq: %u msg_no: %u bitrate: %u bw: %u\n", 
                        ts,
                        header.seq(),
                        header.msg_no(),
                        header.bitrate(),
                        bandwidth_
                        );
            }
        }
        if ( header.pkt_pos() == LAST || header.pkt_pos() == SOLO ) {
            if ( header.is_preempt() )
                drop_stale_pkts_svc( header.msg_no(), header.priority() );
        }
    }
    assert( good() );
}

void DropBitrateQueue::drop_stale_pkts_svc( uint32_t msg_no, uint32_t priority ) {
    std::map< uint32_t, int > frame_counter;
    for ( auto it = internal_queue_.cbegin(); it != internal_queue_.cend(); ++it ) {
        PacketHeader header( it->contents );
        if( header.pkt_pos() == FIRST || header.pkt_pos() == LAST ) {
            if( frame_counter.find( header.msg_no() ) == frame_counter.end() ) 
                frame_counter[ header.msg_no() ] = 1;
            else
                frame_counter[ header.msg_no() ] += 1;
        }
        else if ( header.pkt_pos() == SOLO ) {
            frame_counter[ header.msg_no() ] = 2;
        }
    }
    for ( auto it = frame_counter.cbegin(); it != frame_counter.cend(); ) {
        if( it->second < 2 )
            it = frame_counter.erase( it );
        else
            it++;
    }
    for ( auto it = frame_counter.cbegin(); it != frame_counter.cend(); it++ ) {
        if ( log_fd_ ) {
            fprintf( log_fd_, "msgs-in-queue msg_id: %d\n", it->first );
        }
    }
    frame_counter.erase( msg_no );
    for ( auto it = internal_queue_.cbegin(); it != internal_queue_.cend(); ) {
        PacketHeader header( it->contents );
        if( frame_counter.find( header.msg_no() ) != frame_counter.end() &&
                header.priority() >= priority &&
                header.msg_no() < msg_no &&
                header.is_udp() ) {
            queue_size_in_packets_ --;
            queue_size_in_bytes_ -= it->contents.size();
            it = internal_queue_.erase( it );
            if ( log_fd_ ) {
                fprintf( log_fd_, "sema-drop pkt, seq: %d, msg_id: %d, priority: %d\n",
                        header.seq(), header.msg_no(), header.priority() );
            }
        }
        else
            it++;
    }
}
