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
        uint16_t dstport = header.dstport();
        accept( std::move( p ) );
        if ( !header.is_octopus() ) {
            if ( log_fd_ )
                fprintf( log_fd_, "enqueue, Other ts: %ld pkt_size: %ld queue_size: %u\n",
                        ts, pkt_size,
                        size_bytes() );
            assert( good() );
            return;
        }

        enqueue_trace_[ dstport ].push_back( Trace( ts, pkt_size ) ); 
        if ( enqueue_trace_[dstport].front().first + 100 < ts ) {
            int bytes_sent = 0;
            for ( auto it = enqueue_trace_[dstport].begin(); 
                    it != enqueue_trace_[dstport].end(); ++it ) {
                bytes_sent += it->second;
            }
            bytes_sent =- enqueue_trace_[dstport].front().second;
            enqueue_rate_[dstport] = bytes_sent * 8 / 
                (enqueue_trace_[dstport].back().first - enqueue_trace_[dstport].front().first);
        }

        if ( log_fd_ ) {
            fprintf( log_fd_, "enqueue, Octopus ts: %ld pkt_size: %ld"
                    " queue_size: %u seq: %u msg_no: %d dstport: %u\n",
                    ts, pkt_size,
                    size_bytes(),
                    header.seq(),
                    header.msg_no(),
                    dstport );
        }
        if( header.pkt_pos() == FIRST || header.pkt_pos() == LAST ) {
            if( frame_counter_[dstport].find( header.msg_no() ) == frame_counter_[dstport].end() ) 
                frame_counter_[dstport][ header.msg_no() ] = 1;
            else
                frame_counter_[dstport][ header.msg_no() ] += 1;
        }
        else if ( header.pkt_pos() == SOLO ) {
            assert( frame_counter_[dstport].find( header.msg_no() ) == frame_counter_[dstport].end() );
            frame_counter_[dstport][ header.msg_no() ] = 2;
        }
        if ( header.pkt_pos() == LAST || header.pkt_pos() == SOLO ) {
            if ( header.is_preempt() ) {
                int32_t dropper = header.msg_no();
                uint32_t prio_thresh = header.priority_threshold();
                if ( prio_to_droppers_.find( dstport ) == prio_to_droppers_.end() ) {
                    for ( int i = 0; i < 8; ++i )
                        prio_to_droppers_[dstport][i] = -1;
                }
                if ( dropper > prio_to_droppers_[dstport][prio_thresh] ) {
                    prio_to_droppers_[dstport][prio_thresh] = dropper;
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
    uint16_t dstport = header.dstport();

    if ( !header.is_octopus() )
        return pkt;

    uint32_t fair_share = 0;
    uint32_t peer_rate = 0;
    uint32_t own_rate = enqueue_rate_[dstport];
    for( auto it = enqueue_rate_.begin(); it != enqueue_rate_.end(); ++it ) {
        peer_rate += it->second;
    }
    peer_rate -= own_rate;
    if ( peer_rate == 0)
        fair_share = bandwidth_;
    else if ( peer_rate >= bandwidth_/2 && own_rate >= bandwidth_/2 )
        fair_share = bandwidth_ / 2;
    else if ( peer_rate <= bandwidth_/2 && own_rate <= bandwidth_/2 )
        fair_share = bandwidth_/ 2;
    else if ( peer_rate <= bandwidth_/2 && own_rate >= bandwidth_/2 )
        fair_share = bandwidth_ - peer_rate;
    else if ( peer_rate >= bandwidth_/2 && own_rate <= bandwidth_/2 )
        fair_share = own_rate + 500;


    if ( frame_counter_[dstport].find( header.msg_no() ) != frame_counter_[dstport].end()
            && frame_counter_[dstport][header.msg_no()] == 2 ) {
        int64_t sojourn_time = ts - pkt.arrival_time;
        int32_t latest_dropper = -1;
        for ( unsigned int i = 0; i <= header.priority(); ++i ) {
            if ( prio_to_droppers_[dstport][i] > latest_dropper )
                latest_dropper = prio_to_droppers_[dstport][i];
        }
        if ( header.bitrate() > fair_share && sojourn_time >= header.slack_time() ) {
            pkt.is_drop = true;
            msg_in_drop_[dstport] = header.msg_no();
            if (log_fd_)
                fprintf( log_fd_, "sema-drop, ts: %lu seq: %u msg_no: %d bitrate: %u fairshare: %u slack_time: %u\n",
                        ts,
                        header.seq(),
                        header.msg_no(),
                        header.bitrate(),
                        fair_share,
                        header.slack_time()
                       );
        }
        else if ( header.msg_no() < latest_dropper
                && sojourn_time >= header.slack_time() ) {
            pkt.is_drop = true;
            msg_in_drop_[dstport] = header.msg_no();
            if (log_fd_)
                fprintf( log_fd_, "sema-drop, ts: %lu seq: %u msg_no: %d priority: %u dropper: %d slack_time: %u\n",
                        ts,
                        header.seq(),
                        header.msg_no(),
                        header.priority(),
                        latest_dropper,
                        header.slack_time()
                       );
        }
    }
    else if ( header.msg_no() == msg_in_drop_[dstport] ) {
        pkt.is_drop = true;
    }

    if ( header.pkt_pos() == FIRST || header.pkt_pos() == LAST ) {
        frame_counter_[dstport].at( header.msg_no() ) -= 1;
        if ( frame_counter_[dstport][ header.msg_no() ] == 0 ) {
            frame_counter_[dstport].erase( header.msg_no() );
            /*
            if ( log_fd_ )
                fprintf( log_fd_, "remove_msg msg_no: %u\n",
                        header.msg_no() );
                        */
        }
    }
    else if ( header.pkt_pos() == SOLO ) {
        assert ( frame_counter_[dstport][ header.msg_no() ] == 2 );
        frame_counter_[dstport].erase( header.msg_no() );
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
        if ( !header.is_octopus() )
            break;
        if ( pkt.is_drop ) {
            pkt = dequeuefront();
            continue;
        }
        break;
    }
    PacketHeader header( pkt.contents );
    if ( log_fd_ && header.is_octopus() ) {
        fprintf( log_fd_, "dequeue, Octopus ts: %lu pkt_size: %ld queue_size: %u queued_time: %ld seq: %u msg_no: %u bitrate: %u bw: %d wildcard: %x\n",
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

