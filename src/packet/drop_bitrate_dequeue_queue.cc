#include "packet_header.hh"
#include "drop_bitrate_dequeue_queue.hh"
#include "timestamp.hh"
#include <map>
#include <chrono>

using namespace std::chrono;

void DropBitrateDequeueQueue::enqueue( QueuedPacket && p ) {
    if ( good_with( size_bytes() + p.contents.size(),
                size_packets() + 1 ) ) {
        uint64_t ts = timestamp();
        PacketHeader header ( p.contents );
        if ( log_fd_ ) {
            if ( header.is_udp() ) {
                fprintf( log_fd_, "enqueue, UDP ts: %ld pkt_size: %ld queue_size: %u seq: %u msg_no : %u\n",
                        ts, p.contents.size(),
                        size_bytes(),
                        header.seq(),
                        header.msg_no()
                        );
            }
            else {
                fprintf( log_fd_, "enqueue, TCP ts: %ld pkt_size: %ld queue_size: %u\n",
                        ts, p.contents.size(),
                        size_bytes() );
            }
        }
        accept( std::move( p ) );
        if ( header.pkt_pos() == LAST || header.pkt_pos() == SOLO ) {
            if ( header.is_preempt() )
                drop_stale_pkts_svc( header.msg_no(), header.priority() );
        }
    }
    assert( good() );
}

QueuedPacket DropBitrateDequeueQueue::dequeue( void ) {
    assert( not internal_queue_.empty() );
    uint64_t ts = timestamp();

    QueuedPacket pkt = std::move( internal_queue_.front() );
    internal_queue_.pop_front();
    queue_size_in_bytes_ -= pkt.contents.size();
    queue_size_in_packets_--;

    PacketHeader header( pkt.contents );
    
    if ( header.bitrate() <= bandwidth_ ||
            internal_queue_.empty() 
            //( ( ts - pkt.arrival_time) < 10 ) 
            ) {
        if ( log_fd_ )
            fprintf( log_fd_, "dequeue, UDP ts: %lu pkt_size: %ld queue_size: %u queued_time: %ld seq: %u bitrate: %u bw: %d\n",
                    ts, pkt.contents.size(),
                    size_bytes(),
                    ts - pkt.arrival_time,
                    header.seq(),
                    header.bitrate(),
                    bandwidth_
                    );
        assert( good() );
        return pkt;
    }
    else {
        if ( log_fd_ )
            fprintf( log_fd_, "sema-drop, ts: %lu seq: %u msg_no: %u bitrate: %u bw: %u\n",
                    ts,
                    header.seq(),
                    header.msg_no(),
                    header.bitrate(),
                    bandwidth_ );
        return dequeue();
    }
}

void DropBitrateDequeueQueue::drop_stale_pkts_svc( uint32_t msg_no, uint32_t priority ) {
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
