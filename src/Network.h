#ifndef NETWORK_H
#define NETWORK_H

#include <cstdint>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
}

#define MTU_SIZE 1400

class Network {
public:
    Network();
    ~Network();

    bool init(const char* dest_ip, int dest_port);
    void send_rtp_packet(const uint8_t* nal_data, int nal_size, uint32_t timestamp, bool is_sps_pps);

private:
    void create_rtp_header(uint8_t* buffer, uint16_t seq_num, uint32_t timestamp, bool marker);

    int sock = -1;
    struct sockaddr_in* dest_addr = nullptr;
    uint16_t sequence_number = 0;
    uint32_t ssrc = 0;
};

#endif // NETWORK_H
