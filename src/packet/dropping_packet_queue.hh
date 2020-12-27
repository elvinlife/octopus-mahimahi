/* -*-mode:c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#ifndef DROPPING_PACKET_QUEUE_HH
#define DROPPING_PACKET_QUEUE_HH

#include <deque>
#include <cassert>

#include "abstract_packet_queue.hh"
#include "exception.hh"

class DroppingPacketQueue : public AbstractPacketQueue
{
private:
    virtual const std::string & type( void ) const = 0;

protected:
    int queue_size_in_bytes_ = 0, queue_size_in_packets_ = 0;
    std::deque<QueuedPacket> internal_queue_ {};
    unsigned int packet_limit_;
    unsigned int byte_limit_;
    FILE* log_fd_ = NULL;

    /* put a packet on the back of the queue */
    void accept( QueuedPacket && p );

    /* are the limits currently met? */
    bool good( void ) const;
    bool good_with( const unsigned int size_in_bytes,
                    const unsigned int size_in_packets ) const;

public:
    DroppingPacketQueue( const std::string & args );
    ~DroppingPacketQueue() {
        if( log_fd_ )
            fclose(log_fd_);
    }

    virtual void enqueue( QueuedPacket && p, uint32_t ) = 0;

    QueuedPacket dequeue( void ) override;

    bool empty( void ) const override;

    std::string to_string( void ) const override;

    //static unsigned int get_arg( const std::string & args, const std::string & name );
    static std::string get_arg( const std::string & args, const std::string & name );

    unsigned int size_bytes( void ) const override;
    unsigned int size_packets( void ) const override;
};

#endif /* DROPPING_PACKET_QUEUE_HH */ 
