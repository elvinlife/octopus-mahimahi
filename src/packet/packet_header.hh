#ifndef PACKET_HEADER_HH
#define PACKET_HEADER_HH
#include <cstring>
#include <string>
#include <arpa/inet.h>
using std::string;

enum PktPos { FIRST, LAST, MID, SOLO };

#define UDT4_PKT 1

#ifndef UDT4_PKT
class PacketHeader {
    static const int app_header_offset = 32; // 4(?) + 20(ip) + 8(udp)
private:
    bool    valid_;
    uint32_t seq_;
    uint32_t msg_field_;
    uint32_t wildcard_;
    uint16_t fragment_no_;
    uint16_t fragments_num_; 

    uint32_t ip_field_;

public:
    PacketHeader( const string & payload ) {
        memcpy( &ip_field_, payload.c_str()+12, sizeof( ip_field_ ) );
        ip_field_ = ntohl( ip_field_ );

        const char* bytes_buffer = payload.c_str() + app_header_offset;
        memcpy( &seq_, bytes_buffer, sizeof(seq_) );
        seq_ = le32toh( seq_ );
        memcpy( &msg_field_, bytes_buffer + 4, sizeof( msg_field_ ) );
        msg_field_ = le32toh( msg_field_ );
        memcpy( &wildcard_, bytes_buffer + 8, sizeof( wildcard_ ) );
        wildcard_ = le32toh( wildcard_ );
        memcpy( &fragment_no_, bytes_buffer + 12, sizeof( fragment_no_ ) );
        fragment_no_ = le16toh( fragment_no_ );
        memcpy( &fragments_num_, bytes_buffer + 14, sizeof( fragments_num_ ) );
        fragments_num_ = le16toh( fragments_num_ );
    }

    uint32_t seq() const { return seq_; }
    uint32_t msg_no() const { return msg_field_; }
    uint32_t wildcard() const { return wildcard_; }

    PktPos pkt_pos() const {
        if ( fragment_no_ == (fragments_num_ - 1) ) {
            return LAST;
        }
        else if ( fragment_no_ == 0 ) {
            return FIRST;
        }
        else if ( fragments_num_ == 1 ) {
            return SOLO;
        }
        else {
            return MID;
        }
    }

    uint32_t priority() const { return (wildcard_ & 0xe0000000) >> 29; }
    uint32_t priority_threshold() const { return (wildcard_ & 0x1c000000) >> 26; }
    bool is_preempt() const { return (wildcard_ & 0x2000000); }
    uint32_t slack_time() const { return (wildcard_ & 0x01ff0000) >> 16; }
    uint32_t bitrate() const { return wildcard_ & 0x0000ffff; }
    bool is_udp() const { return ((ip_field_ >> 16) & 0x000000ff) == 17; }
};
#endif

#ifdef UDT4_PKT
class PacketHeader {
    static const int app_header_offset = 32; // 4(?) + 20(ip) + 8(udp)
    private:
    bool    valid_;
    uint32_t seq_;
    uint32_t msg_field_;
    uint32_t wildcard_;
    uint32_t ip_field_;
    uint32_t udp_field_;

    public:
    PacketHeader( const string & payload ) {
        memcpy( &ip_field_, payload.c_str()+12, sizeof( ip_field_ ) );
        ip_field_ = ntohl( ip_field_ );
        memcpy( &udp_field_, payload.c_str()+24, sizeof( udp_field_ ) );
        udp_field_ = ntohl( udp_field_ );
        const char* bytes_buffer = payload.c_str() + app_header_offset;
        memcpy( &seq_, bytes_buffer, sizeof(seq_) );
        seq_ = ntohl( seq_ );
        memcpy( &msg_field_, bytes_buffer + 4, sizeof( msg_field_ ) );
        msg_field_ = ntohl( msg_field_ );
        memcpy( &wildcard_, bytes_buffer + 8, sizeof( wildcard_ ) );
        wildcard_ = ntohl( wildcard_ );
    }

    uint32_t seq() const { return seq_; }
    int32_t msg_no() const { return (int32_t) msg_field_ & 0x1fffffff; }
    uint32_t msg_field() const { return msg_field_; }
    uint32_t wildcard() const { return wildcard_; }

    PktPos pkt_pos() const {
        if ( (msg_field_ & 0xc0000000) == 0x80000000 ) {
            return FIRST;
        }
        else if ( (msg_field_ & 0xc0000000) == 0x40000000 ) {
            return LAST;
        }
        else if ( (msg_field_ & 0xc0000000) == 0x00000000 ) {
            return MID;
        }
        else {
            return SOLO;
        }
    } 

    uint32_t    priority() const { return (wildcard_ & 0xe0000000) >> 29; }
    uint32_t    priority_threshold() const { return (wildcard_ & 0x1c000000) >> 26; }
    bool        is_preempt() const { return (wildcard_ & 0x2000000); }
    uint32_t    slack_time() const { return (wildcard_ & 0x01ff0000) >> 16; }
    uint32_t    bitrate() const { return wildcard_ & 0x0000ffff; }
    bool        is_udp() const { return ((ip_field_ >> 16) & 0x000000ff) == 17; }
    bool        is_octopus() const { return is_udp() && !( seq_ & 0xa0000000 ); }
    uint16_t    dstport() const { return udp_field_ & 0x0000ffff; }
};
#endif

#endif
