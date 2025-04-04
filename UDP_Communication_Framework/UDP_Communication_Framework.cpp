#pragma comment(lib, "ws2_32.lib")
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdio.h>
#include <stdint.h>
#include <string>

#define BUFFERS_LEN 1024

// ODKOMENTUJ POUZE JEDNU Z NÁSLEDUJÍCÍCH ŘÁDKŮ:
//#define SENDER
#define RECEIVER

#ifdef SENDER
#define TARGET_IP   "147.32.221.16"   // Změň na IP přijímače
#define TARGET_PORT 5555
#define LOCAL_PORT  8888
#define FILENAME    "C:\\Users\\vejvo\\Downloads\\UDP_Communication_Framework\\file.txt"
#endif

#ifdef RECEIVER
#define TARGET_PORT 8888
#define LOCAL_PORT  5555
#endif

void InitWinsock() {
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
}

int main() {
    SOCKET socketS;
    InitWinsock();

    struct sockaddr_in local;
    local.sin_family = AF_INET;
    local.sin_port = htons(LOCAL_PORT);
    local.sin_addr.s_addr = INADDR_ANY;

    socketS = socket(AF_INET, SOCK_DGRAM, 0);
    if (bind(socketS, (sockaddr*)&local, sizeof(local)) != 0) {
        printf("Binding error!\n");
        getchar();
        return 1;
    }

    char buffer_tx[BUFFERS_LEN];
    char buffer_rx[BUFFERS_LEN];

#ifdef SENDER

    struct sockaddr_in addrDest;
    addrDest.sin_family = AF_INET;
    addrDest.sin_port = htons(TARGET_PORT);
    addrDest.sin_addr.s_addr = inet_addr(TARGET_IP);

    FILE* file = fopen(FILENAME, "rb");
    if (file == NULL) {
        printf("File error! %s not found.\n", FILENAME);
        getchar();
        return 1;
    }

    fseek(file, 0, SEEK_END);
    int file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    // send file name
    snprintf(buffer_tx, BUFFERS_LEN, "NAME=%s", FILENAME);
    printf("Sending file name.\n");
    sendto(socketS, buffer_tx, strlen(buffer_tx), 0, (sockaddr*)&addrDest, sizeof(addrDest));

    // send file size
    snprintf(buffer_tx, BUFFERS_LEN, "SIZE=%d", file_size);
    printf("Sending file size.\n");
    sendto(socketS, buffer_tx, strlen(buffer_tx), 0, (sockaddr*)&addrDest, sizeof(addrDest));

    // send START
    strncpy(buffer_tx, "START", BUFFERS_LEN);
    printf("Sending start.\n");
    sendto(socketS, buffer_tx, strlen(buffer_tx), 0, (sockaddr*)&addrDest, sizeof(addrDest));

    // send file content
    while (!feof(file)) {
        int data_len = fread(buffer_tx + 4, 1, BUFFERS_LEN - 4, file);
        if (data_len == 0) break;

        uint32_t offset = htonl(ftell(file));
        memcpy(buffer_tx, &offset, 4);

        printf("Sending %d bytes of data.\n", data_len);
        sendto(socketS, buffer_tx, data_len + 4, 0, (sockaddr*)&addrDest, sizeof(addrDest));
    }

    // send STOP
    strncpy(buffer_tx, "STOP", BUFFERS_LEN);
    printf("Sending stop.\n");
    sendto(socketS, buffer_tx, strlen(buffer_tx), 0, (sockaddr*)&addrDest, sizeof(addrDest));

    fclose(file);
    closesocket(socketS);
    getchar();
    return 0;

#endif // SENDER

#ifdef RECEIVER

    struct sockaddr_in from;
    int fromlen = sizeof(from);
    std::string filename;
    int file_size = 0;
    int file_offset = 0;
    FILE* file = NULL;

    while (true) {
        printf("Waiting for data...\n");

        memset(buffer_rx, 0, BUFFERS_LEN);

        int recv_len = recvfrom(socketS, buffer_rx, sizeof(buffer_rx), 0, (sockaddr*)&from, &fromlen);
        if (recv_len == SOCKET_ERROR) {
            printf("Error receiving data!\n");
            getchar();
            return 1;
        }

        // Rozpoznání textových zpráv
        if (strncmp(buffer_rx, "NAME=", 5) == 0 ||
            strncmp(buffer_rx, "SIZE=", 5) == 0 ||
            strncmp(buffer_rx, "START", 5) == 0 ||
            strncmp(buffer_rx, "STOP", 4) == 0) {

            char temp[BUFFERS_LEN + 1];
            memcpy(temp, buffer_rx, recv_len);
            temp[recv_len] = '\0';
            printf("Data: %s\n", temp);

            if (strncmp(temp, "NAME=", 5) == 0) {
                filename = temp + 5;
                size_t lastSlash = filename.find_last_of("\\/");
                if (lastSlash != std::string::npos) {
                    filename = filename.substr(lastSlash + 1);
                }

                file = fopen(filename.c_str(), "wb");
                if (file == NULL) {
                    printf("File error! Cannot create %s\n", filename.c_str());
                    getchar();
                    return 1;
                } else {
                    printf("Receiving file: %s\n", filename.c_str());
                }
            }
            else if (strncmp(temp, "SIZE=", 5) == 0) {
                file_size = atoi(temp + 5);
                printf("File size: %d bytes\n", file_size);
            }
            else if (strncmp(temp, "START", 5) == 0) {
                file_offset = 0;
                printf("Start receiving data...\n");
            }
            else if (strncmp(temp, "STOP", 4) == 0) {
                fclose(file);
                printf("Transfer complete.\n");
                break;
            }

        } else {
            if (file == NULL) continue;

            uint32_t offset;
            memcpy(&offset, buffer_rx, 4);
            offset = ntohl(offset);

            if (offset == file_offset) {
                int data_len = recv_len - 4;
                fwrite(buffer_rx + 4, 1, data_len, file);
                file_offset += data_len;

                printf("Written %d bytes at offset %d\n", data_len, offset);
            } else {
                printf("Offset mismatch! Expected %d, got %d\n", file_offset, offset);
            }
        }
    }

    closesocket(socketS);
    getchar();
    return 0;

#endif // RECEIVER
}
