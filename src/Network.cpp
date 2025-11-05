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
}

bool Network::init(int listen_port) {
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        std::cerr << "Could not create socket." << std::endl;
        return false;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(listen_port);

    if (bind(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        std::cerr << "Could not bind socket." << std::endl;
        close(sock);
        sock = -1;
        return false;
    }

    return true;
}

bool Network::wait_for_client() {
    if (client_connected) {
        return true;
    }

    std::cout << "Waiting for client connection..." << std::endl;
    char buffer[10];
    socklen_t client_len = sizeof(client_addr);
    int n = recvfrom(sock, buffer, sizeof(buffer), 0, (struct sockaddr *)&client_addr, &client_len);

    if (n > 0) {
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
        std::cout << "Client connected from " << client_ip << ":" << ntohs(client_addr.sin_port) << std::endl;
        client_connected = true;
        return true;
    }

    return false;
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
    if (!client_connected) return;

    if (nal_size <= MTU_SIZE - 12) {
        // Single NAL unit packet
        std::vector<uint8_t> packet(12 + nal_size);
        create_rtp_header(packet.data(), sequence_number++, timestamp, true);
        memcpy(packet.data() + 12, nal_data, nal_size);
        sendto(sock, packet.data(), packet.size(), 0, (struct sockaddr*)&client_addr, sizeof(client_addr));
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
            sendto(sock, packet.data(), packet.size(), 0, (struct sockaddr*)&client_addr, sizeof(client_addr));

            nal_data += chunk_size;
            nal_size -= chunk_size;
        }
    }
}
