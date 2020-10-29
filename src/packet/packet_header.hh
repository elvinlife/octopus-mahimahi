#ifndef PACKET_HEADER_HH
#define PACKET_HEADER_HH
#include <cstring>
#include <string>
using std::string;

class PacketHeader {
    static const int app_header_offset = 32; // 20(ip) + 8(udp) + 4(?)
private:
    bool    valid_;
    uint32_t seq_;
    uint32_t frame_no_;
    uint32_t ref_frame_no_;
    uint16_t fragment_no_;
    uint16_t fragments_num_; 
    uint32_t ts_send_;

public:

    PacketHeader( const string & payload ) {
        const char* bytes_buffer = payload.c_str() + app_header_offset;
        memcpy( &seq_, bytes_buffer, sizeof(seq_) );
        seq_ = le32toh( seq_ );
        memcpy( &frame_no_, bytes_buffer + 4, sizeof( frame_no_ ) );
        frame_no_ = le32toh( frame_no_ );
        memcpy( &ref_frame_no_, bytes_buffer + 8, sizeof( ref_frame_no_ ) );
        ref_frame_no_ = le32toh( ref_frame_no_ );
        memcpy( &fragment_no_, bytes_buffer + 12, sizeof( fragment_no_ ) );
        fragment_no_ = le16toh( fragment_no_ );
        memcpy( &fragments_num_, bytes_buffer + 14, sizeof( fragments_num_ ) );
        fragments_num_ = le16toh( fragments_num_ );
    }

    uint32_t seq() const { return seq_; }
    uint32_t frame_no() const { return frame_no_; }
    uint32_t ref_frame_no() const { return ref_frame_no_; }
    uint16_t fragment_no() const { return fragment_no_; }
    uint16_t fragments_num() const { return fragments_num_; }
    uint32_t ts_send() const { return ts_send_; }
};

#endif
