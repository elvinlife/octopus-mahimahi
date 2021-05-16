#include "packet_header.hh"
#include "drop_sojourn_queue.hh"
#include "timestamp.hh"
#include <map>
#include <chrono>

using namespace std::chrono;

void DropSemanticSojournQueue::enqueue( QueuedPacket && p ) {
    if ( good_with( size_bytes() + p.contents.size(),
                size_packets() + 1 ) ) {
        uint64_t ts = timestamp();
        PacketHeader header ( p.contents );
        if ( log_fd_ ) {
            if ( header.is_udp() ) {
                fprintf( log_fd_, "enqueue, UDP ts: %ld pkt_size: %ld queue_size: %u seq: %d msg_field: %x wildcard: %x bandwidth: %u\n",
                        ts, p.contents.size(),
                        size_bytes(),
                        header.seq(),
                        header.msg_field(),
                        header.wildcard(),
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
        accept( std::move( p ) );
        if ( header.pkt_pos() == LAST || header.pkt_pos() == SOLO ) {
            if ( header.is_preempt() )
                drop_stale_pkts_svc( header.msg_no(), header.priority() );
        }
    }
    assert( good() );
}

QueuedPacket DropSemanticSojournQueue::dequeue( void ) {
    assert( not internal_queue_.empty() );

    uint64_t ts = timestamp();

    if ( last_adjust_ts_ + interval_ <= ts && !sojourn_trace_.empty() ) {
        int sojourn_time = interval_;
        for (auto it = sojourn_trace_.begin(); it != sojourn_trace_.end(); ++it) {
            if ( sojourn_time > it->second ) {
                sojourn_time = it->second;
            }
        }
        int orig_thresh = priority_thresh_;
        if ( sojourn_time <= target_sojourn_ && priority_thresh_ < 3 ) {
            priority_thresh_ += 1;
        }
        else if ( sojourn_time > target_sojourn_ && priority_thresh_ > 1 ) {
            priority_thresh_ -= 1;
        }
        if ( log_fd_ )
            fprintf( log_fd_, "sojourn_time: %dms thresh: %d->%d\n",
                    sojourn_time, orig_thresh, priority_thresh_ ); 
        last_adjust_ts_ = ts;
    }

    QueuedPacket pkt = std::move( internal_queue_.front() );
    internal_queue_.pop_front();
    queue_size_in_bytes_ -= pkt.contents.size();
    queue_size_in_packets_--;

    // exclude tcp packets
    PacketHeader header( pkt.contents );

    if ( !header.is_udp() ) {
        fprintf( log_fd_, "dequeue, TCP ts: %ld pkt_size: %ld queue_size: %u queued_time: %ld\n",
                ts, pkt.contents.size(),
                size_bytes(),
                ts - pkt.arrival_time );
        assert( good() );
        return pkt;
    }
    else {
        if ( header.priority() <= priority_thresh_ || 
                internal_queue_.empty() ) {
            while( sojourn_trace_.front().first < (ts - interval_) &&
                    !sojourn_trace_.empty() ) {
                sojourn_trace_.pop_front();
            }
            sojourn_trace_.push_back( std::pair<uint64_t, int> (ts, ts - pkt.arrival_time) );
            if ( log_fd_ )
                fprintf( log_fd_, "dequeue, UDP ts: %ld pkt_size: %ld queue_size: %u queued_time: %ld seq: %d\n",
                        ts, pkt.contents.size(),
                        size_bytes(),
                        ts - pkt.arrival_time,
                        header.seq() );
            assert( good() );
            return pkt;
        }
        else {
            if ( log_fd_ )
                fprintf( log_fd_, "drop, ts: %lu seq: %d msg_no: %d priority: %u thresh: %u\n",
                        ts,
                        header.seq(),
                        header.msg_no(),
                        header.priority(),
                        priority_thresh_ );
            return dequeue();
        }
    }

    /*
    while ( !internal_queue_.empty() ) {
        PacketHeader header( pkt.contents );
        // deliver if the priority is higher or it's the only pkt in queue
        if ( header.priority() <= priority_thresh_ ) {
            break;
        }
        if ( log_fd_ )
            fprintf( log_fd_, "drop, ts: %lu seq: %d msg_field: %x priority: %u thresh: %u\n",
                    ts,
                    header.seq(),
                    header.msg_field(),
                    header.priority(),
                    priority_thresh_ );

        pkt = std::move( internal_queue_.front() );
        internal_queue_.pop_front();
        queue_size_in_bytes_ -= pkt.contents.size();
        queue_size_in_packets_--;
    }
    sojourn_trace_.push_back( std::pair<uint64_t, int> (ts, ts - pkt.arrival_time) );
    while( sojourn_trace_.front().second < (int)(ts - interval_) &&
            sojourn_trace_.size() >= 3 ) {
        sojourn_trace_.pop_front();
    }
    if ( log_fd_ ) {
        PacketHeader header (pkt.contents );
        fprintf( log_fd_, "dequeue, seq: %d ts: %ld pkt_size: %ld queue_size: %u queued_time: %ld\n",
                header.seq(),
                ts, pkt.contents.size(), size_bytes(),
                ts - pkt.arrival_time
                );
    }
    assert( good() );
    return pkt;
    */
}

void DropSemanticSojournQueue::drop_stale_pkts_svc( uint32_t msg_no, uint32_t priority ) {
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
