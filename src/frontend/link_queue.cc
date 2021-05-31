/* -*-mode:c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include <limits>
#include <cassert>

#include "link_queue.hh"
#include "timestamp.hh"
#include "util.hh"
#include "ezio.hh"
#include "abstract_packet_queue.hh"
#include "packet_header.hh"

using namespace std;

LinkQueue::LinkQueue( const string & link_name, const string & filename, const string & logfile,
                      const bool repeat, const bool graph_throughput, const bool graph_delay,
                      unique_ptr<AbstractPacketQueue> && packet_queue,
                      const string & command_line )
    : next_delivery_( 0 ),
      schedule_(),
      //base_timestamp_( timestamp() ),
      base_timestamp_( 0 ),
      packet_queue_( move( packet_queue ) ),
      packet_in_transit_( "", 0 ),
      packet_in_transit_bytes_left_( 0 ),
      output_queue_(),
      log_(),
      throughput_graph_( nullptr ),
      delay_graph_( nullptr ),
      repeat_( repeat ),
      finished_( false ),
      empty_times_( 0 ),
      dequeue_rate_(0xffffffff)
{
    assert_not_root();

    /* open filename and load schedule */
    ifstream trace_file( filename );

    if ( not trace_file.good() ) {
        throw runtime_error( filename + ": error opening for reading" );
    }

    string line;

    while ( trace_file.good() and getline( trace_file, line ) ) {
        if ( line.empty() ) {
            throw runtime_error( filename + ": invalid empty line" );
        }

        const uint64_t ms = myatoi( line );

        if ( not schedule_.empty() ) {
            if ( ms < schedule_.back() ) {
                throw runtime_error( filename + ": timestamps must be monotonically nondecreasing" );
            }
        }

        schedule_.emplace_back( ms );
    }

    /*
    uint64_t interval = 50; // 50ms
    int anchor = 0;
    for (int i = 0; i < (int)schedule_.size(); i++) {
        if (schedule_[i] < interval) {
            bitrate_.push_back(0);
            anchor = i;
        }
        else {
            int j = i;
            int num_pkts = 0;
            while ( j >= 0 && (schedule_[j] > schedule_[i] - interval) ) {
                num_pkts += 1;
                j--;
            }
            bitrate_.push_back( PACKET_SIZE * num_pkts * 8 / interval);
        }
    }

    for (int i = 0; i <= anchor; ++i) {
        bitrate_[i] = bitrate_[anchor + 1];
    }
    */

    int interval = 100;
    // trace in small timescale
    if ((int)schedule_.back() < interval) {
        for (int i = 0; i < (int)schedule_.size(); i++) {
            bitrate_.push_back( PACKET_SIZE * schedule_.size() * 8 / schedule_.back() ); 
        }
    }
    // trace in large timescale
    else {
        for (int i = 0; i < (int)schedule_.size(); i++) {
            int cycle = schedule_.back();
            //int left = i, right = i, offset;
            int left = i, offset;
            offset = 0;
            while (schedule_[left] + offset > schedule_[i] - interval/2) {
                left -= 1;
                if (left < 0) {
                    offset = - cycle;
                    left += schedule_.size(); 
                }
            }
            int num_pkts = (i - left) > 0 ? ( i - left ) : ( i - left + schedule_.size() );
            int interval = (i - left) > 0 ? ( schedule_[i] - schedule_[left] ) : (schedule_[i] - schedule_[left] + cycle );
            bitrate_.push_back( PACKET_SIZE * num_pkts * 8 / interval );
            /*
            offset = 0;
            while (schedule_[right] + offset < schedule_[i] + interval/2) {
                right += 1;
                if ( right >= (int)schedule_.size() ) {
                    offset = cycle;
                    right -= schedule_.size();
                }
            }
            int num_pkts = (right - left) > 0 ? (right - left) : (right - left + schedule_.size());
            int interval = (right - left) > 0 ? (schedule_[right] - schedule_[left]) : (schedule_[right] - schedule_[left] + cycle );
            bitrate_.push_back( PACKET_SIZE * num_pkts * 8 / interval );
            */
        }

    }
    assert( bitrate_.size() == schedule_.size() );

    if ( schedule_.empty() ) {
        throw runtime_error( filename + ": no valid timestamps found" );
    }

    if ( schedule_.back() == 0 ) {
        throw runtime_error( filename + ": trace must last for a nonzero amount of time" );
    }

    /* open logfile if called for */
    if ( not logfile.empty() ) {
        log_.reset( new ofstream( logfile ) );
        if ( not log_->good() ) {
            throw runtime_error( logfile + ": error opening for writing" );
        }

        *log_ << "# mahimahi mm-link (" << link_name << ") [" << filename << "] > " << logfile << endl;
        *log_ << "# command line: " << command_line << endl;
        *log_ << "# queue: " << packet_queue_->to_string() << endl;
        *log_ << "# init timestamp: " << initial_timestamp() << endl;
        *log_ << "# base timestamp: " << base_timestamp_ << endl;
        const char * prefix = getenv( "MAHIMAHI_SHELL_PREFIX" );
        if ( prefix ) {
            *log_ << "# mahimahi config: " << prefix << endl;
        }
    }

    /* create graphs if called for */
    if ( graph_throughput ) {
        throughput_graph_.reset( new BinnedLiveGraph( link_name + " [" + filename + "]",
                                                      { make_tuple( 1.0, 0.0, 0.0, 0.25, true ),
                                                        make_tuple( 0.0, 0.0, 0.4, 1.0, false ),
                                                        make_tuple( 1.0, 0.0, 0.0, 0.5, false ) },
                                                      "throughput (Mbps)",
                                                      8.0 / 1000000.0,
                                                      true,
                                                      500,
                                                      [] ( int, int & x ) { x = 0; } ) );
    }

    if ( graph_delay ) {
        delay_graph_.reset( new BinnedLiveGraph( link_name + " delay [" + filename + "]",
                                                 { make_tuple( 0.0, 0.25, 0.0, 1.0, false ) },
                                                 "queueing delay (ms)",
                                                 1, false, 250,
                                                 [] ( int, int & x ) { x = -1; } ) );
    }
}

void LinkQueue::record_arrival( const uint64_t arrival_time, const size_t pkt_size )
{
    /* log it */
    if ( log_ ) {
        *log_ << arrival_time << " + " << pkt_size << endl;
    }

    /* meter it */
    if ( throughput_graph_ ) {
        throughput_graph_->add_value_now( 1, pkt_size );
    }
}

void LinkQueue::record_drop( const uint64_t time, const size_t pkts_dropped, const size_t bytes_dropped)
{
    /* log it */
    if ( log_ ) {
        *log_ << time << " d " << pkts_dropped << " " << bytes_dropped << endl;
    }
}

void LinkQueue::record_departure_opportunity( void )
{
    /* log the delivery opportunity */
    if ( log_ ) {
        *log_ << next_delivery_time() << " # " << PACKET_SIZE << endl;
    }

    /* meter the delivery opportunity */
    if ( throughput_graph_ ) {
        throughput_graph_->add_value_now( 0, PACKET_SIZE );
    }    
}

void LinkQueue::record_departure( const uint64_t departure_time, const QueuedPacket & packet )
{
    /* log the delivery */
    if ( log_ ) {
        *log_ << departure_time << " - " << packet.contents.size()
              << " delay: " << departure_time - packet.arrival_time 
              << " queue_size: " << packet_queue_->size_packets() << endl;
    }

    /* meter the delivery */
    if ( throughput_graph_ ) {
        throughput_graph_->add_value_now( 2, packet.contents.size() );
    }

    if ( delay_graph_ ) {
        delay_graph_->set_max_value_now( 0, departure_time - packet.arrival_time );
    }    
}

void LinkQueue::read_packet( const string & contents )
{
    const uint64_t now = timestamp();

    if ( contents.size() > PACKET_SIZE ) {
        throw runtime_error( "packet size is greater than maximum" );
    }

    rationalize( now );

    record_arrival( now, contents.size() );

    unsigned int bytes_before = packet_queue_->size_bytes();
    unsigned int packets_before = packet_queue_->size_packets();

    //packet_queue_->enqueue( QueuedPacket( contents, now ), bitrate_.at( next_delivery_ ) );
    //packet_queue_->set_bandwidth( dequeue_rate_ );
    //packet_queue_->set_bandwidth( bitrate_.at( next_delivery_ ) );
    packet_queue_->enqueue( QueuedPacket( contents, now ) );

    assert( packet_queue_->size_packets() <= packets_before + 1 );
    assert( packet_queue_->size_bytes() <= bytes_before + contents.size() );
    
    unsigned int missing_packets = packets_before + 1 - packet_queue_->size_packets();
    unsigned int missing_bytes = bytes_before + contents.size() - packet_queue_->size_bytes();
    if ( missing_packets > 0 || missing_bytes > 0 ) {
        record_drop( now, missing_packets, missing_bytes );
    }
}

uint64_t LinkQueue::next_delivery_time( void ) const
{
    if ( finished_ ) {
        return -1;
    } else {
        return schedule_.at( next_delivery_ ) + base_timestamp_;
    }
}

void LinkQueue::use_a_delivery_opportunity( void )
{
    record_departure_opportunity();

    next_delivery_ = (next_delivery_ + 1) % schedule_.size();
    //packet_queue_->set_bandwidth( bitrate_.at( next_delivery_ ) );
    packet_queue_->set_bandwidth( dequeue_rate_ );

    /* wraparound */
    if ( next_delivery_ == 0 ) {
        if ( repeat_ ) {
            base_timestamp_ += schedule_.back();
        } else {
            finished_ = true;
        }
    }
}

/* emulate the link up to the given timestamp */
/* this function should be called before enqueueing any packets and before
   calculating the wait_time until the next event */
void LinkQueue::rationalize( const uint64_t now )
{
    while ( next_delivery_time() <= now ) {
        const uint64_t this_delivery_time = next_delivery_time();

        /* burn a delivery opportunity */
        unsigned int bytes_left_in_this_delivery = PACKET_SIZE;
        use_a_delivery_opportunity();

        while ( bytes_left_in_this_delivery > 0 ) {
            if ( not packet_in_transit_bytes_left_ ) {
                if ( packet_queue_->empty() ) {
                    empty_times_ ++;
                    dequeue_trace_.clear();
                    dequeue_rate_ = 0xffffffff;
                    break;
                }
                packet_in_transit_ = packet_queue_->dequeue();
                packet_in_transit_bytes_left_ = packet_in_transit_.contents.size();
            }

            assert( packet_in_transit_.arrival_time <= this_delivery_time );
            assert( packet_in_transit_bytes_left_ <= PACKET_SIZE );
            assert( packet_in_transit_bytes_left_ > 0 );
            assert( packet_in_transit_bytes_left_ <= packet_in_transit_.contents.size() );

            /* how many bytes of the delivery opportunity can we use? */
            const unsigned int amount_to_send = min( bytes_left_in_this_delivery,
                                                     packet_in_transit_bytes_left_ );

            /* send that many bytes */
            packet_in_transit_bytes_left_ -= amount_to_send;
            bytes_left_in_this_delivery -= amount_to_send;

            /* has the packet been fully sent? */
            if ( packet_in_transit_bytes_left_ == 0 ) {
                int interval = 50;
                int packet_size = packet_in_transit_.contents.size();
                record_departure( this_delivery_time, packet_in_transit_ );

                /* this packet is ready to go */
                output_queue_.push( move( packet_in_transit_.contents ) );

                // measure the dequeue rate
                empty_times_ = 0;
                if (dequeue_trace_.size() >= 5) {
                    int total_byte = 0;
                    for (auto it = dequeue_trace_.begin(); it != dequeue_trace_.end(); ++it) {
                        total_byte += it->second;
                    }
                    total_byte = total_byte - dequeue_trace_.front().second + packet_size;
                    if ( this_delivery_time == dequeue_trace_.front().first ) {
                        dequeue_rate_ = 0xffffffff;
                    }
                    else {
                        dequeue_rate_ = total_byte * 8.0 / ( this_delivery_time - dequeue_trace_.front().first );
                    }
                    /*
                    if ( log_ ) {
                        *log_ << "dequeue_rate: " << dequeue_rate_ << \
                            " begin_ts: " << dequeue_trace_.front().first << \
                            " end_ts: " << this_delivery_time << \
                            " total_bytes: " << total_byte << endl;
                    }
                    */
                }
                else {
                    dequeue_rate_ = 0xffffffff;
                }
                dequeue_trace_.push_back( std::pair<uint64_t, int>(this_delivery_time, packet_size ) );
                // consider both the time interval (for high bandwidth) and trace number (for low bandwidth)
                while ( dequeue_trace_.size() >= 5 && 
                        dequeue_trace_.front().first < (this_delivery_time - interval) ) {
                    dequeue_trace_.pop_front();
                }
            }
        }
    }
}

void LinkQueue::write_packets( FileDescriptor & fd )
{
    while ( not output_queue_.empty() ) {
        fd.write( output_queue_.front() );
        output_queue_.pop();
    }
}

unsigned int LinkQueue::wait_time( void )
{
    const auto now = timestamp();

    rationalize( now );

    if ( next_delivery_time() <= now ) {
        return 0;
    } else {
        return next_delivery_time() - now;
    }
}

bool LinkQueue::pending_output( void ) const
{
    return not output_queue_.empty();
}
