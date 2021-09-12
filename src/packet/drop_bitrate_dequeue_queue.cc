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
            fprintf( log_fd_, "enqueue, UDP ts: %ld pkt_size: %ld queue_size: %u seq: %u msg_no: %d\n",
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
                int32_t dropper = header.msg_no();
                uint32_t prio_thresh = header.priority_threshold();
                if ( dropper > prio_to_droppers_[prio_thresh] ) {
                    prio_to_droppers_[prio_thresh] = dropper;
                }
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
        int32_t latest_dropper = -1;
        for ( unsigned int i = 0; i <= header.priority(); ++i ) {
            if ( prio_to_droppers_[i] > latest_dropper )
                latest_dropper = prio_to_droppers_[i];
        }
        if ( header.bitrate() > bandwidth_ && sojourn_time >= header.slack_time() ) {
            pkt.is_drop = true;
            msg_in_drop_ = header.msg_no();
            fprintf( log_fd_, "sema-drop, ts: %lu seq: %u msg_no: %d bitrate: %u bw: %u slack-time: %u\n",
                    ts,
                    header.seq(),
                    header.msg_no(),
                    header.bitrate(),
                    bandwidth_,
                    header.slack_time()
                    );
        }
        else if ( header.msg_no() < latest_dropper
                && sojourn_time >= header.slack_time() ) {
            pkt.is_drop = true;
            msg_in_drop_ = header.msg_no();
            fprintf( log_fd_, "sema-drop, ts: %lu seq: %u msg_no: %d priority: %u dropper: %d slack-time: %u\n",
                    ts,
                    header.seq(),
                    header.msg_no(),
                    header.priority(),
                    latest_dropper,
                    header.slack_time()
                    );
        }
    }
    else if ( header.msg_no() == msg_in_drop_ ) {
        pkt.is_drop = true;
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

