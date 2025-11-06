#ifndef NETWORK_H
#define NETWORK_H

#include <cstdint>
#include <vector>
#include <netinet/in.h>

extern "C" {
#include <libavcodec/avcodec.h>
}

#define MTU_SIZE 1400

class Network {
public:
    Network();
    ~Network();

    bool init(int listen_port);
    bool wait_for_client();
    void send_rtp_packet(const uint8_t* nal_data, int nal_size, uint32_t timestamp, bool is_sps_pps);
    void send_mouse_position(int x, int y);

private:
    void create_rtp_header(uint8_t* buffer, uint16_t seq_num, uint32_t timestamp, bool marker);

    int sock = -1;
    struct sockaddr_in client_addr;
    bool client_connected = false;
    uint16_t sequence_number = 0;
    uint32_t ssrc = 0;
};

#endif // NETWORK_H
