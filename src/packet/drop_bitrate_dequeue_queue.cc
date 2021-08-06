#include "packet_header.hh"
#include "drop_bitrate_dequeue_queue.hh"
#include "timestamp.hh"
#include <chrono>
#include <vector>
#include <exception>

using namespace std::chrono;

void DropBitrateDequeueQueue::enqueue( QueuedPacket && p ) {
    if ( good_with( size_bytes() + p.contents.size(),
                size_packets() + 1 ) ) {
        uint64_t ts = timestamp();
        size_t pkt_size = p.contents.size();
        PacketHeader header ( p.contents );
        accept( std::move( p ) );
        if ( !header.is_udp() ) {
            if ( log_fd_ )
                fprintf( log_fd_, "enqueue, TCP ts: %ld pkt_size: %ld queue_size: %u\n",
                        ts, pkt_size,
                        size_bytes() );
            assert( good() );
            return;
        }
        if ( log_fd_ ) {
            fprintf( log_fd_, "enqueue, UDP ts: %ld pkt_size: %ld queue_size: %u seq: %u msg_no: %u\n",
                    ts, pkt_size,
                    size_bytes(),
                    header.seq(),
                    header.msg_no()
                   );
        }
        if( header.pkt_pos() == FIRST || header.pkt_pos() == LAST ) {
            if( frame_counter_.find( header.msg_no() ) == frame_counter_.end() ) 
                frame_counter_[ header.msg_no() ] = 1;
            else
                frame_counter_[ header.msg_no() ] += 1;
        }
        else if ( header.pkt_pos() == SOLO ) {
            assert( frame_counter_.find( header.msg_no() ) == frame_counter_.end() );
            frame_counter_[ header.msg_no() ] = 2;
        }
        if ( header.pkt_pos() == LAST || header.pkt_pos() == SOLO ) {
            if ( header.is_preempt() ) {
                //drop_stale_pkts_svc( header.msg_no(), header.priority_threshold() );
                dropper_msg_ = header.msg_no();
                priority_threshold_ = header.priority_threshold();
            }
        }
    }
}

QueuedPacket DropBitrateDequeueQueue::dequeuefront( void ) {
    // iterate to tag drop_flag of one message
    uint64_t ts = timestamp();
    QueuedPacket pkt = std::move( internal_queue_.front() );
    internal_queue_.pop_front();
    queue_size_in_bytes_ -= pkt.contents.size();
    queue_size_in_packets_--;
    PacketHeader header( pkt.contents );
    if ( !header.is_udp() )
        return pkt;
    if ( frame_counter_.find( header.msg_no() ) != frame_counter_.end()
            && frame_counter_[header.msg_no()] == 2 ) {
        int64_t sojourn_time = ts - pkt.arrival_time;
        if ( header.bitrate() > bandwidth_ && sojourn_time >= header.slack_time() ) {
            pkt.is_drop = true;
            msg_in_drop_ = header.msg_no();
            fprintf( log_fd_, "sema-drop, ts: %lu seq: %u msg_no: %u bitrate: %u bw: %u slack-time: %u\n",
                    ts,
                    header.seq(),
                    header.msg_no(),
                    header.bitrate(),
                    bandwidth_,
                    header.slack_time()
                    );
        }
        else if ( header.priority() >= priority_threshold_
                && header.msg_no() < dropper_msg_ 
                && sojourn_time >= header.slack_time() ) {
            pkt.is_drop = true;
            msg_in_drop_ = header.msg_no();
            fprintf( log_fd_, "sema-drop, ts: %lu seq: %u msg_no: %u priority: %u prio_thresh: %u slack-time: %u\n",
                    ts,
                    header.seq(),
                    header.msg_no(),
                    header.priority(),
                    priority_threshold_,
                    header.slack_time()
                    );
        }
    }
    else if ( header.msg_no() == msg_in_drop_ ) {
        pkt.is_drop = true;
        /*
        if ( log_fd_ )
            fprintf( log_fd_, "sema-drop, ts: %lu seq: %u msg_no: %u bitrate: %u bw: %u\n",
                    ts,
                    header.seq(),
                    header.msg_no(),
                    header.bitrate(),
                    bandwidth_ );
                    */
    }

    if ( header.pkt_pos() == FIRST || header.pkt_pos() == LAST ) {
        assert ( frame_counter_.find( header.msg_no() ) != frame_counter_.end() );
        frame_counter_[ header.msg_no() ] -= 1;
        if ( frame_counter_[ header.msg_no() ] == 0 ) {
            frame_counter_.erase( header.msg_no() );
            /*
            if ( log_fd_ )
                fprintf( log_fd_, "remove_msg msg_no: %u\n",
                        header.msg_no() );
                        */
        }
    }
    else if ( header.pkt_pos() == SOLO ) {
        assert ( frame_counter_[ header.msg_no() ] == 2 );
        frame_counter_.erase( header.msg_no() );
        /*
        if ( log_fd_ )
            fprintf( log_fd_, "remove_msg msg_no: %u\n",
                    header.msg_no() );
                    */
    }
    return pkt; // use std::move later
}

QueuedPacket DropBitrateDequeueQueue::dequeue( void ) {
    assert( not internal_queue_.empty() );
    uint64_t ts = timestamp();
    QueuedPacket pkt = dequeuefront();
    while ( !internal_queue_.empty() ) {
        PacketHeader header( pkt.contents );
        if ( !header.is_udp() )
            break;
        if ( pkt.is_drop ) {
            pkt = dequeuefront();
            continue;
        }
        break;
    }
    PacketHeader header( pkt.contents );
    if ( log_fd_ && header.is_udp() ) {
        fprintf( log_fd_, "dequeue, UDP ts: %lu pkt_size: %ld queue_size: %u queued_time: %ld seq: %u msg_no: %u bitrate: %u bw: %d wildcard: %x\n",
                ts, pkt.contents.size(),
                size_bytes(),
                ts - pkt.arrival_time,
                header.seq(),
                header.msg_no(),
                header.bitrate(),
                bandwidth_,
                header.wildcard()
               );
    }
    return pkt;
}

void DropBitrateDequeueQueue::drop_stale_pkts_svc( uint32_t msg_no, uint32_t priority ) {
    uint64_t ts = timestamp();
    for ( auto it = frame_counter_.cbegin(); it != frame_counter_.cend(); it++ ) {
        if ( log_fd_ ) {
            fprintf( log_fd_, "msgs-in-queue msg_id: %d value: %d\n",
                    it->first, it->second );
            if ( it->second == 0 ) {
                fprintf( log_fd_, "error: value shouldn't be 0\n" );
                //throw std::runtime_error("value shouldn't be 0\n");
            }
        }
    }
    for ( auto it = internal_queue_.begin(); it != internal_queue_.end(); it++) {
        PacketHeader header( it->contents );
        if ( header.is_udp() &&
                header.msg_no() < msg_no &&
                header.priority() >= priority &&
                frame_counter_[ header.msg_no() ] == 2 ) {
            it->is_drop = true;
            if ( log_fd_ ) {
                fprintf( log_fd_, "sema-drop, ts: %lu seq: %d, msg_no: %u, priority: %u\n",
                        ts,
                        header.seq(),
                        header.msg_no(),
                        header.priority() );
            }
        }
    }
}
