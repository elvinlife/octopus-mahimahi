#include "packet_header.hh"
#include "drop_semantic_packet_queue.hh"
#include <map>
#include <chrono>

using namespace std::chrono;

void DropSemanticPacketQueue::enqueue( QueuedPacket && p) {
    drop_stale_pkts();
    if ( good_with( size_bytes() + p.contents.size(),
                size_packets() + 1 ) ) {
        uint32_t ts = duration_cast< milliseconds >( system_clock::now().time_since_epoch() ).count();
        /*
           PacketHeader header (p.contents );
           fprintf( log_fd_, "enqueue, ts: %u seq: %u frame_no: %u queue_size: %u\n",
           ts,
           header.seq(),
           header.frame_no(),
           size_packets());
           */
        if ( log_fd_ )
            fprintf( log_fd_, "enqueue, ts: %u pkt_size: %ld queue_size: %u\n",
                    ts, p.contents.size(), size_bytes() );
        accept( std::move( p ) );
    }

    assert( good() );
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

    // collect frame_no in the queue
    for ( auto it = internal_queue_.cbegin(); it != internal_queue_.cend(); ++it ) {
        PacketHeader header( it->contents );
        if( header.fragment_no() == 0 ||
                header.fragment_no() == ( header.fragments_num() - 1 ) ) {
            if( frame_counter.find( header.frame_no() ) == frame_counter.end() ) 
                frame_counter[ header.frame_no() ] = 1;
            else
                frame_counter[ header.frame_no() ] += 1;
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
        fprintf( log_fd_, "drop frame, frame_no: %u\n", it->first );
    }
    fflush( log_fd_ );

    for ( auto it = internal_queue_.cbegin(); it != internal_queue_.cend(); ) {
        PacketHeader header( it->contents );
        if( frame_counter.find( header.frame_no() ) != frame_counter.end() &&
                header.frame_no() != latest_msg ) {
            queue_size_in_packets_ --;
            queue_size_in_bytes_ -= it->contents.size();
            it = internal_queue_.erase( it );
        }
        else
            it++;
    }
}
