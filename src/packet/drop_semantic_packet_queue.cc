#include "packet_header.hh"
#include "drop_semantic_packet_queue.hh"
#include "timestamp.hh"
#include <map>
#include <chrono>

using namespace std::chrono;

void DropSemanticPacketQueue::enqueue( QueuedPacket && p, int bandwidth) {
    if ( good_with( size_bytes() + p.contents.size(),
                size_packets() + 1 ) ) {
        uint64_t ts = timestamp();
        PacketHeader header ( p.contents );
        if ( !header.is_udp() ) {
            accept( std::move( p ) );
            assert( good() );
            return;
        }
        if ( log_fd_ )
            fprintf( log_fd_, "enqueue, ts: %lu seq: %d msg_id: %d wildcard: %x pkt_size: %ld queue_size: %u bandwidth: %d dequeue_rate: %u\n",
                    ts, 
                    header.seq(),
                    header.msg_no(),
                    header.wildcard(),
                    p.contents.size(), 
                    size_bytes(),
                    bandwidth,
                    dequeue_rate );
        /*
        if (dequeue_rate > header.bitrate()) {
            accept( std::move( p ) );
        }
        else {
            fprintf( log_fd_, "sema-drop pkt, seq: %d, msg_id: %d, wildcard: %x\n",
                    header.seq(), header.msg_no(), header.wildcard() );
        }
        */
        accept( std::move( p ) );
        drop_on_dequeue_rate();
    }
    assert( good() );
}

void DropSemanticPacketQueue::drop_on_dequeue_rate( void )
{
    for ( auto it = internal_queue_.cbegin(); it != internal_queue_.cend(); ) {
        PacketHeader header( it->contents );
        if( dequeue_rate < header.bitrate() ) {
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

/*
void DropSemanticPacketQueue::enqueue( QueuedPacket && p, int bandwidth) {
    if ( good_with( size_bytes() + p.contents.size(),
                size_packets() + 1 ) ) {
        uint64_t ts = timestamp();
        PacketHeader header ( p.contents );
        if ( !header.is_udp() ) {
            accept( std::move( p ) );
            assert( good() );
            return;
        }
        if ( log_fd_ )
            fprintf( log_fd_, "enqueue, ts: %lu seq: %d msg_id: %d wildcard: %x pkt_size: %ld queue_size: %u bandwidth: %d\n",
                    ts, 
                    header.seq(),
                    header.msg_no(),
                    header.wildcard(),
                    p.contents.size(), 
                    size_bytes(),
                    bandwidth);
        accept( std::move( p ) );
        if ( header.fragment_no() == ( header.fragments_num() - 1) ) {
            //if (size_bytes() > 100000)
            if ( header.is_preempt() )
                drop_stale_pkts_svc( header.msg_no(), header.priority() );
        }
    }
    assert( good() );
}
*/

QueuedPacket DropSemanticPacketQueue::dequeue( void )
{
    assert( not internal_queue_.empty() );

    QueuedPacket ret = std::move( internal_queue_.front() );
    //internal_queue_.pop();
    internal_queue_.pop_front();

    uint64_t ts = timestamp();
    if ( log_fd_ ) {
        PacketHeader header (ret.contents );
        fprintf( log_fd_, "dequeue, seq: %d ts: %ld pkt_size: %ld queue_size: %u queued_time: %ld\n",
                header.seq(),
                ts, ret.contents.size(), size_bytes(),
                ts - ret.arrival_time
                );
    }

    queue_size_in_bytes_ -= ret.contents.size();
    queue_size_in_packets_--;

    uint64_t ts_micro = microtimestamp();
    dequeue_trace.push( ts_micro );
    if (internal_queue_.empty()) {
        std::queue<uint64_t>().swap(dequeue_trace);
        dequeue_rate = 0xffffffff;
    }
    else {
        fprintf( log_fd_, "dequeue_trace: %lu %lu\n", dequeue_trace.front(), dequeue_trace.back() );
        if ( dequeue_trace.front() > (ts_micro - interval/2) ) {
            dequeue_rate = 0xffffffff;
        }
        else {
            while (dequeue_trace.front() < (ts_micro - interval) ) {
                dequeue_trace.pop();
            }
            dequeue_rate = (uint32_t)PACKET_SIZE * (dequeue_trace.size() - 1) * 8.0 / 
                ( ( ts_micro - dequeue_trace.front() ) / 1000.0);
        }
    }

    assert( good() );

    return ret;
}

void DropSemanticPacketQueue::drop_stale_pkts_svc( uint32_t msg_no, uint32_t priority ) {
    std::map< uint32_t, int > frame_counter;
    for ( auto it = internal_queue_.cbegin(); it != internal_queue_.cend(); ++it ) {
        PacketHeader header( it->contents );
        if( header.fragment_no() == 0 ||
                header.fragment_no() == ( header.fragments_num() - 1 ) ) {
            if( frame_counter.find( header.msg_no() ) == frame_counter.end() ) 
                frame_counter[ header.msg_no() ] = 1;
            else
                frame_counter[ header.msg_no() ] += 1;
        }
    }
    for ( auto it = frame_counter.cbegin(); it != frame_counter.cend(); ) {
        if( it->second < 2 )
            it = frame_counter.erase( it );
        else
            it++;
    }
    frame_counter.erase( msg_no );
    for ( auto it = internal_queue_.cbegin(); it != internal_queue_.cend(); ) {
        PacketHeader header( it->contents );
        if( frame_counter.find( header.msg_no() ) != frame_counter.end() &&
                header.priority() > priority &&
                header.msg_no() < msg_no ) {
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

void DropSemanticPacketQueue::drop_stale_pkts ( void ) {
    /*
    for ( const auto & pkt : internal_queue_ ) {
        PacketHeader header ( pkt.contents );
    }
    */
    uint32_t latest_msg = -1;
    int gop_size = 3;
    std::map< uint32_t, int > frame_counter;

    // collect msg_no in the queue
    for ( auto it = internal_queue_.cbegin(); it != internal_queue_.cend(); ++it ) {
        PacketHeader header( it->contents );
        if( header.fragment_no() == 0 ||
                header.fragment_no() == ( header.fragments_num() - 1 ) ) {
            if( frame_counter.find( header.msg_no() ) == frame_counter.end() ) 
                frame_counter[ header.msg_no() ] = 1;
            else
                frame_counter[ header.msg_no() ] += 1;
        }
    }

    // remove incomplete frames
    for ( auto it = frame_counter.cbegin(); it != frame_counter.cend(); ) {
        if( it->second < 2 )
            it = frame_counter.erase( it );
        else
            it++;
    }

    // keep the latest frame
    for ( auto it = frame_counter.cbegin(); it != frame_counter.cend(); ++it ) {
        if ( ( it->first % gop_size == 0 && latest_msg % gop_size == 0 )
                || ( it->first % gop_size != 0 && latest_msg % gop_size != 0 ) ) {
            if( it->first > latest_msg )
                latest_msg = it->first;
        }
        else if ( it->first % gop_size == 0 && latest_msg % gop_size != 0 )
            latest_msg = it->first;
    }

    for ( auto it = frame_counter.cbegin(); it != frame_counter.cend(); ++it ) {
        fprintf( log_fd_, "drop frame, msg_no: %u\n", it->first );
    }
    fflush( log_fd_ );

    for ( auto it = internal_queue_.cbegin(); it != internal_queue_.cend(); ) {
        PacketHeader header( it->contents );
        if( frame_counter.find( header.msg_no() ) != frame_counter.end() &&
                header.msg_no() != latest_msg ) {
            queue_size_in_packets_ --;
            queue_size_in_bytes_ -= it->contents.size();
            it = internal_queue_.erase( it );
        }
        else
            it++;
    }
}
