#ifndef PACKET_HEADER_HH
#define PACKET_HEADER_HH
#include <cstring>
#include <string>
#include <arpa/inet.h>
using std::string;

class PacketHeader {
    static const int app_header_offset = 32; // 4(?) + 20(ip) + 8(udp)
private:
    bool    valid_;
    uint32_t seq_;
    uint32_t msg_no_;
    uint32_t wildcard_;
    uint16_t fragment_no_;
    uint16_t fragments_num_; 

public:
    uint32_t ip_field_;

public:
    PacketHeader( const string & payload ) {
        memcpy( &ip_field_, payload.c_str()+12, sizeof( ip_field_ ) );
        ip_field_ = ntohl( ip_field_ );

        const char* bytes_buffer = payload.c_str() + app_header_offset;
        memcpy( &seq_, bytes_buffer, sizeof(seq_) );
        seq_ = le32toh( seq_ );
        memcpy( &msg_no_, bytes_buffer + 4, sizeof( msg_no_ ) );
        msg_no_ = le32toh( msg_no_ );
        memcpy( &wildcard_, bytes_buffer + 8, sizeof( wildcard_ ) );
        wildcard_ = le32toh( wildcard_ );
        memcpy( &fragment_no_, bytes_buffer + 12, sizeof( fragment_no_ ) );
        fragment_no_ = le16toh( fragment_no_ );
        memcpy( &fragments_num_, bytes_buffer + 14, sizeof( fragments_num_ ) );
        fragments_num_ = le16toh( fragments_num_ );
    }

    uint32_t seq() const { return seq_; }
    uint32_t msg_no() const { return msg_no_; }
    uint32_t wildcard() const { return wildcard_; }
    uint16_t fragment_no() const { return fragment_no_; }
    uint16_t fragments_num() const { return fragments_num_; }

    uint32_t priority() const { return (wildcard_ & 0xe0000000) >> 29; }
    uint32_t gop_id() const { return wildcard_ & 0x0fffffff; }
    uint32_t bitrate() const { return wildcard_ & 0x0fffffff; }
    bool is_udp() const { return ((ip_field_ >> 16) & 0x000000ff) == 17; }
    bool is_preempt() const { return (wildcard_ & 0x10000000); }
};

#endif
