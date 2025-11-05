#include "Network.h"
#include <iostream>
#include <cstring>
#include <arpa/inet.h>
#include <unistd.h>

Network::Network() {
    ssrc = rand();
}

Network::~Network() {
    if (sock >= 0) {
        close(sock);
    }
    if (dest_addr) {
        delete dest_addr;
    }
}

bool Network::init(const char* dest_ip, int dest_port) {
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        std::cerr << "Could not create socket." << std::endl;
        return false;
    }

    dest_addr = new sockaddr_in();
    dest_addr->sin_family = AF_INET;
    dest_addr->sin_port = htons(dest_port);
    if (inet_pton(AF_INET, dest_ip, &dest_addr->sin_addr) <= 0) {
        std::cerr << "Invalid destination address." << std::endl;
        return false;
    }

    return true;
}

void Network::create_rtp_header(uint8_t* buffer, uint16_t seq_num, uint32_t timestamp, bool marker) {
    buffer[0] = 0x80; // Version 2, no padding, no extension, no CSRC
    buffer[1] = 0x60; // Payload type for H.264
    if (marker) {
        buffer[1] |= 0x80;
    }
    *(uint16_t*)&buffer[2] = htons(seq_num);
    *(uint32_t*)&buffer[4] = htonl(timestamp);
    *(uint32_t*)&buffer[8] = htonl(ssrc);
}

void Network::send_rtp_packet(const uint8_t* nal_data, int nal_size, uint32_t timestamp, bool is_sps_pps) {
    if (nal_size <= MTU_SIZE - 12) {
        // Single NAL unit packet
        std::vector<uint8_t> packet(12 + nal_size);
        create_rtp_header(packet.data(), sequence_number++, timestamp, true);
        memcpy(packet.data() + 12, nal_data, nal_size);
        sendto(sock, packet.data(), packet.size(), 0, (struct sockaddr*)dest_addr, sizeof(*dest_addr));
    } else {
        // FU-A fragmentation
        uint8_t nal_header = nal_data[0];
        nal_data++;
        nal_size--;

        bool start_bit = true;
        while (nal_size > 0) {
            int chunk_size = std::min(nal_size, MTU_SIZE - 14);
            std::vector<uint8_t> packet(14 + chunk_size);
            create_rtp_header(packet.data(), sequence_number++, timestamp, false);

            packet[12] = (nal_header & 0xE0) | 28; // FU indicator (FU-A)
            packet[13] = nal_header & 0x1F;

            if (start_bit) {
                packet[13] |= 0x80; // Start bit
                start_bit = false;
            }

            if (chunk_size == nal_size) {
                packet[13] |= 0x40; // End bit
            }

            memcpy(packet.data() + 14, nal_data, chunk_size);
            sendto(sock, packet.data(), packet.size(), 0, (struct sockaddr*)dest_addr, sizeof(*dest_addr));

            nal_data += chunk_size;
            nal_size -= chunk_size;
        }
    }
}
