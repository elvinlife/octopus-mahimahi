/* -*-mode:c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include <ctime>
#include <fstream>

#include "timestamp.hh"
#include "exception.hh"

uint64_t raw_timestamp( void )
{
    timespec ts;
    SystemCall( "clock_gettime", clock_gettime( CLOCK_REALTIME, &ts ) );

    uint64_t millis = ts.tv_nsec / 1000000;
    millis += uint64_t( ts.tv_sec ) * 1000;

    return millis;
}

uint64_t initial_timestamp( void )
{
    static bool is_initiated = false;
    static uint64_t initial_value = raw_timestamp();
    if (!is_initiated) {
        std::ofstream tmp_log ( "/tmp/mahimahi_initts.log" );
        tmp_log << initial_value;
        tmp_log.close();
    }
    is_initiated = true;
    return initial_value;
}

uint64_t timestamp( void )
{
    return raw_timestamp() - initial_timestamp();
}

uint64_t raw_microtimestamp( void )
{
    timespec ts;
    SystemCall( "clock_gettime", clock_gettime( CLOCK_REALTIME, &ts ) );

    uint64_t micro = ts.tv_nsec / 1000;
    micro += uint64_t( ts.tv_sec ) * 1000000;

    return micro;
}

uint64_t initial_microtimestamp( void )
{
    static uint64_t initial_value = raw_microtimestamp();
    return initial_value;
}

uint64_t microtimestamp( void )
{
    return raw_microtimestamp() - initial_microtimestamp();
}
