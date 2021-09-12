#include "packet_header.hh"
#include "drop_activenet_queue.hh"
#include "timestamp.hh"
#include <chrono>
#include <vector>
#include <exception>

using namespace std::chrono;

void DropActiveNetQueue::enqueue( QueuedPacket && p ) {
    PacketHeader header ( p.contents );
    if ( !header.is_udp() ) {
        accept( std::move( p ) );
        return;
    }
    if ( !good_with( size_bytes() + p.contents.size(),
                size_packets() + 1 ) )  {
        if ( header.is_preempt() && header.priority() == 1) {
            int pkts_drop = dropStaleFrames( 3, header.msg_no() );
            if ( pkts_drop == 0 )
                pkts_drop = dropStaleFrames( 2, header.msg_no() );
        }
    }
    uint64_t ts = timestamp();
    size_t pkt_size = p.contents.size();
    if ( good_with( size_bytes() + p.contents.size(),
                size_packets() + 1 ) ) {
        accept( std::move( p ) );
        if ( log_fd_ ) {
            fprintf( log_fd_, "enqueue, UDP ts: %ld pkt_size: %ld queue_size: %u seq: %u msg_no: %d\n",
                    ts, pkt_size,
                    size_packets(),
                    header.seq(),
                    header.msg_no()
                   );
        }
    }
    else {
        if ( log_fd_ ) {
            fprintf( log_fd_, "drop_tail, UDP ts: %ld pkt_size: %ld queue_size: %u seq: %u msg_no: %d\n",
                    ts, pkt_size,
                    size_packets(),
                    header.seq(),
                    header.msg_no()
                   );
        }
    }
}

QueuedPacket DropActiveNetQueue::dequeue( void ) {
    assert( not internal_queue_.empty() );

    QueuedPacket ret = std::move( internal_queue_.front() );
    PacketHeader header ( ret.contents );
    internal_queue_.pop_front();

    uint64_t ts = timestamp();
    if ( log_fd_ && header.is_udp() ) {
        fprintf( log_fd_, "dequeue, UDP ts: %lu pkt_size: %ld queue_size: %u queued_time: %ld seq: %u msg_no: %u wildcard: %x\n",
                ts, ret.contents.size(),
                size_packets(),
                ts - ret.arrival_time,
                header.seq(),
                header.msg_no(),
                header.wildcard()
               );
    }

    queue_size_in_bytes_ -= ret.contents.size();
    queue_size_in_packets_--;

    assert( good() );
    return ret;
}

int DropActiveNetQueue::dropStaleFrames(uint32_t to_drop, int dropper_msg) {
    uint64_t ts = timestamp();
    int pkts_dropped = 0;
    for ( auto it = internal_queue_.begin(); it != internal_queue_.end(); ) {
        PacketHeader header( it->contents );
        if ( header.priority() == to_drop && 
                header.msg_no() < dropper_msg &&
                ( header.pkt_pos() == FIRST || header.pkt_pos() == SOLO ) ) {
            msg_in_drop_ = header.msg_no();
            it = internal_queue_.erase( it );
            pkts_dropped += 1;
            fprintf( log_fd_, "sema-drop: ts: %lu seq: %u msg_no: %d priority: %u\n",
                    ts, header.seq(), header.msg_no(), to_drop );
        }
        else if ( msg_in_drop_ == header.msg_no() ) {
            it = internal_queue_.erase( it );
            pkts_dropped += 1;
        }
        else {
            ++it;
        }
    }
    return pkts_dropped;
}
