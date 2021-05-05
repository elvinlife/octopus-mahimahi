#ifndef DROP_SEMANTIC_SOJOURN_QUEUE_HH
#define DROP_SEMANTIC_SOJOURN_QUEUE_HH
#include "dropping_packet_queue.hh"
#include <cstdio>
#include <queue>

class DropSemanticSojournQueue : public DroppingPacketQueue
{
private:
    const static unsigned int PACKET_SIZE = 1504; /* default max TUN payload size */
    FILE* log_fd;
    virtual const std::string & type( void ) const override
    {
        static const std::string type_ { "dropsojourn" };
        return type_;
    }

    const int   target_sojourn_ = 1;    // in ms 
    const int   interval_       = 100;
    uint32_t    priority_thresh_    = 2;   // less or equal prioritized pkts are delivered
    uint64_t    last_adjust_ts_     = 0;
    std::deque<std::pair<uint64_t, int>> sojourn_trace_;    // (dequeue_ts, queued_time)
    void    drop_stale_pkts_svc( uint32_t, uint32_t );

public:
    //using DroppingPacketQueue::DroppingPacketQueue;
    DropSemanticSojournQueue( const std::string & args )
        : DroppingPacketQueue( args )
    {
    }

    ~DropSemanticSojournQueue()
    {
    }

    void enqueue( QueuedPacket && p, uint32_t bandwidth) override;
    QueuedPacket dequeue( void ) override;
};

#endif
