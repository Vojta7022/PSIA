#pragma comment(lib, "ws2_32.lib")
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdio.h>
#include <stdint.h>
#include <string>
#include <windows.h>
#include <zlib.h>
#include <openssl/sha.h>
#include <openssl/evp.h>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <vector>
#include <unordered_map>
#include <chrono>

#define BUFFERS_LEN 1024
#define SENDER
// #define RECEIVER

// C:\\Users\\ponrt\\Documents\\_Coding\\PSIA\\PSIA - Sender\\UDP_Communication_Framework\\lena.bmp

#ifdef SENDER
#define TARGET_IP   "147.32.219.112"
#define TARGET_PORT 5555
#define LOCAL_PORT  8888
#define FILENAME    "C:\\Users\\ponrt\\Documents\\_Coding\\PSIA\\PSIA - Sender\\UDP_Communication_Framework\\3.bmp"
#endif

#ifdef RECEIVER
#define TARGET_PORT 8888
#define LOCAL_PORT  5555
#endif

// #ifdef SENDER
// #define TARGET_IP   "127.0.0.1"
// #define TARGET_PORT 14000
// #define LOCAL_PORT  15001
// #define FILENAME    "C:\\Users\\ponrt\\Documents\\_Coding\\PSIA\\PSIA - Sender\\UDP_Communication_Framework\\lena.bmp"
// #endif

// #ifdef RECEIVER
// #define TARGET_PORT 8888
// #define LOCAL_PORT  5555
// #endif

#define WINDOW_SIZE 40
#define MAX_RETRIES 10
#define TIMEOUT_MS 500

struct Packet {
    uint32_t seq;
    int data_len;
    char data[BUFFERS_LEN];
    bool acked;
    int retries;
};

uint32_t calculate_crc32(const char* data, size_t length) {
    return crc32(0L, (const Bytef*)data, length);
}

std::string calculate_sha256(const std::string& filename) {
    std::ifstream file(filename, std::ios::binary);
    if (!file) return "";
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(ctx, EVP_sha256(), NULL);
    char buf[4096];
    while (file.good()) {
        file.read(buf, sizeof(buf));
        EVP_DigestUpdate(ctx, buf, file.gcount());
    }
    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int hash_len = 0;
    EVP_DigestFinal_ex(ctx, hash, &hash_len);
    EVP_MD_CTX_free(ctx);
    std::stringstream ss;
    for (unsigned int i = 0; i < hash_len; ++i)
        ss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
    return ss.str();
}

void InitWinsock() {
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
}

int main() {
    
    auto start_time = std::chrono::steady_clock::now();

    InitWinsock();
    SOCKET sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("socket");
        return 1;
    }

    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_port = htons(LOCAL_PORT);
    local.sin_addr.s_addr = INADDR_ANY;
    bind(sock, (sockaddr*)&local, sizeof(local));

#ifdef SENDER
    printf("Starting sending data...\n");
    sockaddr_in dest{};
    memset(&dest, 0, sizeof(dest));
    dest.sin_family = AF_INET;
    dest.sin_port = htons(TARGET_PORT);
    inet_pton(AF_INET, TARGET_IP, &dest.sin_addr);

    FILE* file = fopen(FILENAME, "rb");
    if (!file) {
        printf("Failed to open file: %s\n", FILENAME);
        return 1;
    }

    fseek(file, 0, SEEK_END);
    int file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    char ctrl_buf[BUFFERS_LEN];
    snprintf(ctrl_buf, BUFFERS_LEN, "NAME=%s", FILENAME);
    sendto(sock, ctrl_buf, strlen(ctrl_buf), 0, (sockaddr*)&dest, sizeof(dest));
    printf("Sent NAME: %s\n", FILENAME);

    snprintf(ctrl_buf, BUFFERS_LEN, "SIZE=%d", file_size);
    sendto(sock, ctrl_buf, strlen(ctrl_buf), 0, (sockaddr*)&dest, sizeof(dest));
    printf("Sent SIZE: %d\n", file_size);

    strncpy(ctrl_buf, "START", BUFFERS_LEN);
    sendto(sock, ctrl_buf, strlen(ctrl_buf), 0, (sockaddr*)&dest, sizeof(dest));
    printf("Sent START command\n");

    std::vector<Packet> packets;
    int seq = 1;
    while (!feof(file)) {
        Packet pkt;
        pkt.seq = seq++;
        pkt.data_len = fread(pkt.data + 8, 1, BUFFERS_LEN - 8, file);
        uint32_t crc = htonl(calculate_crc32(pkt.data + 8, pkt.data_len));
        memcpy(pkt.data, &pkt.seq, 4);
        memcpy(pkt.data + 4, &crc, 4);
        pkt.acked = false;
        pkt.retries = 0;
        packets.push_back(pkt);
    }

    int base = 0;
    int next = 0;
    fd_set readfds;
    timeval tv{};

    while (base < packets.size()) {
        while (next < base + WINDOW_SIZE && next < packets.size()) {
            sendto(sock, packets[next].data, packets[next].data_len + 8, 0, (sockaddr*)&dest, sizeof(dest));
            printf("Sending packet #%d\n", packets[next].seq);
            next++;
        }

        FD_ZERO(&readfds);
        FD_SET(sock, &readfds);
        tv.tv_sec = 0;
        tv.tv_usec = TIMEOUT_MS * 1000;
        int activity = select(0, &readfds, NULL, NULL, &tv);

        if (activity > 0 && FD_ISSET(sock, &readfds)) {
            char ack_buf[16];
            int len = recvfrom(sock, ack_buf, sizeof(ack_buf), 0, NULL, NULL);
            if (len == 5 && strncmp(ack_buf, "ACK", 3) == 0) {
                unsigned char low = (unsigned char)ack_buf[3];
                unsigned char high = (unsigned char)ack_buf[4];
                uint16_t ack_seq = (high << 8) | low;
                printf("Received ACK for #%u\n", ack_seq);
                for (auto& pkt : packets) {
                    if (pkt.seq == ack_seq) pkt.acked = true;
                }
                while (base < packets.size() && packets[base].acked) base++;
            } else if (len == 5 && strncmp(ack_buf, "NAK", 3) == 0) {
                unsigned char low = (unsigned char)ack_buf[3];
                unsigned char high = (unsigned char)ack_buf[4];
                uint16_t nak_seq = (high << 8) | low;
                printf("Received NAK for #%u - sending packet again\n", nak_seq);
                printf("---------------ERROR OCCURRED: Packet #%u not received-------------------\n", nak_seq);
                usleep(3000000);
                for (auto& pkt : packets) {
                    if (pkt.seq == nak_seq) {
                        sendto(sock, pkt.data, pkt.data_len + 8, 0, (sockaddr*)&dest, sizeof(dest));
                        pkt.retries++;
                        break;
                    }
                }
            } else {
                printf("Received unexpected data\n");
            }
        } else {
            for (int i = base; i < next; i++) {
                if (!packets[i].acked && packets[i].retries < MAX_RETRIES) {
                    sendto(sock, packets[i].data, packets[i].data_len + 8, 0, (sockaddr*)&dest, sizeof(dest));
                    packets[i].retries++;
                    printf("Retrying packet #%d\n", packets[i].seq);
                }
            }
        }
    }

    strncpy(ctrl_buf, "STOP", BUFFERS_LEN);
    sendto(sock, ctrl_buf, strlen(ctrl_buf), 0, (sockaddr*)&dest, sizeof(dest));
    printf("Sent STOP command\n");

    std::string hash = calculate_sha256(FILENAME);
    snprintf(ctrl_buf, BUFFERS_LEN, "HASH=%s", hash.c_str());
    sendto(sock, ctrl_buf, strlen(ctrl_buf), 0, (sockaddr*)&dest, sizeof(dest));
    printf("Sent SHA256: %s\n", hash.c_str());

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
    printf("Total time taken: %lld ms\n", duration);

    fclose(file);
    closesocket(sock);
    return 0;
#endif

#ifdef RECEIVER
    printf("[RECEIVER] Receiver mode activated.\n");
    sockaddr_in from;
    int fromlen = sizeof(from);
    char buffer_rx[BUFFERS_LEN];

    std::string filename;
    int file_size = 0;
    FILE* file = NULL;
    int expected_base = 1;
    std::unordered_map<int, DataPacket> buffer;

    while (true) {
        memset(buffer_rx, 0, BUFFERS_LEN);
        int recv_len = recvfrom(sock, buffer_rx, BUFFERS_LEN, 0, (sockaddr*)&from, &fromlen);
        if (recv_len < 0) continue;

        if (strncmp(buffer_rx, "NAME=", 5) == 0) {
            filename = buffer_rx + 5;
            size_t lastSlash = filename.find_last_of("\\/");
            if (lastSlash != std::string::npos)
                filename = filename.substr(lastSlash + 1);
            file = fopen(filename.c_str(), "wb");
            if (!file) return 1;
            printf("[RECEIVER] Receiving file: %s\n", filename.c_str());
        } else if (strncmp(buffer_rx, "SIZE=", 5) == 0) {
            file_size = atoi(buffer_rx + 5);
            printf("[RECEIVER] File size: %d bytes\n", file_size);
        } else if (strncmp(buffer_rx, "START", 5) == 0) {
            printf("[RECEIVER] Receiving started...\n");
        } else if (strncmp(buffer_rx, "STOP", 4) == 0) {
            fclose(file);
            printf("[RECEIVER] File closed.\n");
        } else if (strncmp(buffer_rx, "HASH=", 5) == 0) {
            std::string received_hash = buffer_rx + 5;
            std::string local_hash = calculate_sha256(filename);
            printf("[RECEIVER] Received SHA256: %s\n", received_hash.c_str());
            printf("[RECEIVER] Computed SHA256: %s\n", local_hash.c_str());
            break;
        } else if (recv_len >= 8 && file != NULL) {
            uint32_t seq;
            memcpy(&seq, buffer_rx, 4);
            uint32_t received_crc;
            memcpy(&received_crc, buffer_rx + 4, 4);
            received_crc = ntohl(received_crc);
            int data_len = recv_len - 8;
            uint32_t computed_crc = calculate_crc32(buffer_rx + 8, data_len);

            if (received_crc != computed_crc) {
                printf("[RECEIVER] CRC mismatch on packet #%u\n", seq);
                continue;
            }
            if (seq < expected_base || seq >= expected_base + WINDOW_SIZE) {
                char ack_msg[5] = { 'A', 'C', 'K', (unsigned char)seq && 0xFF, (unsigned char)seq >> 8 };
                sendto(sock, ack_msg, 4, 0, (sockaddr*)&from, fromlen);
                printf("[RECEIVER] Packet #%u out of window, ignored.\n", seq);
                continue;
            }

            if (buffer.find(seq) == buffer.end()) {
                DataPacket pkt;
                pkt.seq = seq;
                pkt.data_len = data_len;
                memcpy(pkt.data, buffer_rx + 8, data_len);
                buffer[seq] = pkt;
                char ack_msg[5] = { 'A', 'C', 'K', (unsigned char)seq && 0xFF, (unsigned char)seq >> 8 };
                sendto(sock, ack_msg, 4, 0, (sockaddr*)&from, fromlen);
                printf("[RECEIVER] Packet #%d received, CRC OK, ACK sent\n", seq);
            }

            while (buffer.find(expected_base) != buffer.end()) {
                fwrite(buffer[expected_base].data, 1, buffer[expected_base].data_len, file);
                buffer.erase(expected_base);
                expected_base++;
            }
        }
    }

    closesocket(sock);
    return 0;
#endif
}
