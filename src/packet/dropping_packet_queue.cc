/* -*-mode:c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include <iostream>

#include "dropping_packet_queue.hh"
#include "exception.hh"
#include "ezio.hh"
#include <chrono>
using namespace std::chrono;

using namespace std;

DroppingPacketQueue::DroppingPacketQueue( const string & args )
    : packet_limit_( 0 ),
      byte_limit_( 0 ),
      log_fd_( NULL )
{
    string argv = "";
    argv = get_arg( args, "packets" );
    if ( argv != "" )
        packet_limit_ = myatoi( argv );
    argv = get_arg( args, "bytes" );
    if ( argv != "" )
        byte_limit_ = myatoi( argv );

    if ( packet_limit_ == 0 and byte_limit_ == 0 ) {
        throw runtime_error( "Dropping queue must have a byte or packet limit." );
    }
    argv = get_arg( args, "log_file" );
    if ( argv.size() > 0 )
        log_fd_ = fopen( argv.c_str(), "w" ); 
}

QueuedPacket DroppingPacketQueue::dequeue( void )
{
    assert( not internal_queue_.empty() );

    QueuedPacket ret = std::move( internal_queue_.front() );
    //internal_queue_.pop();
    internal_queue_.pop_front();

    uint32_t ts = duration_cast< milliseconds >( system_clock::now().time_since_epoch() ).count();
    if ( log_fd_ )
        fprintf( log_fd_, "dequeue, ts: %u pkt_size: %ld queue_size: %u\n",
                ts, ret.contents.size(), size_bytes() );

    queue_size_in_bytes_ -= ret.contents.size();
    queue_size_in_packets_--;

    assert( good() );

    return ret;
}

bool DroppingPacketQueue::empty( void ) const
{
    return internal_queue_.empty();
}

bool DroppingPacketQueue::good_with( const unsigned int size_in_bytes,
                                     const unsigned int size_in_packets ) const
{
    bool ret = true;

    if ( byte_limit_ ) {
        ret &= ( size_in_bytes <= byte_limit_ );
    }

    if ( packet_limit_ ) {
        ret &= ( size_in_packets <= packet_limit_ );
    }

    return ret;
}

bool DroppingPacketQueue::good( void ) const
{
    return good_with( size_bytes(), size_packets() );
}

unsigned int DroppingPacketQueue::size_bytes( void ) const
{
    assert( queue_size_in_bytes_ >= 0 );
    return unsigned( queue_size_in_bytes_ );
}

unsigned int DroppingPacketQueue::size_packets( void ) const
{
    assert( queue_size_in_packets_ >= 0 );
    return unsigned( queue_size_in_packets_ );
}

/* put a packet on the back of the queue */
void DroppingPacketQueue::accept( QueuedPacket && p )
{
    queue_size_in_bytes_ += p.contents.size();
    queue_size_in_packets_++;
    internal_queue_.emplace_back( std::move( p ) );
}

string DroppingPacketQueue::to_string( void ) const
{
    string ret = type() + " [";

    if ( byte_limit_ ) {
        ret += string( "bytes=" ) + ::to_string( byte_limit_ );
    }

    if ( packet_limit_ ) {
        if ( byte_limit_ ) {
            ret += ", ";
        }

        ret += string( "packets=" ) + ::to_string( packet_limit_ );
    }

    ret += "]";

    return ret;
}

string DroppingPacketQueue::get_arg( const string & args, const string & name )
{
    auto offset = args.find( name );
    if ( offset == string::npos ) {
        return ""; /* default value */
    } else {
        /* extract the value */

        /* advance by length of name */
        offset += name.size();

        /* make sure next char is "=" */
        if ( args.substr( offset, 1 ) != "=" ) {
            throw runtime_error( "could not parse queue arguments: " + args );
        }

        /* advance by length of "=" */
        offset++;

        /* find the first non-digit character */
        //auto offset2 = args.substr( offset ).find_first_not_of( "0123456789" );
        auto offset2 = args.substr( offset ).find_first_of( "," );

        auto digit_string = args.substr( offset ).substr( 0, offset2 );

        if ( digit_string.empty() ) {
            throw runtime_error( "could not parse queue arguments: " + args );
        }

        return digit_string;
        //return myatoi( digit_string );
    }
}
