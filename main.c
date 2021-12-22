#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netinet/in.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <netdb.h>
#include <errno.h>
#include <string.h>
#include <time.h>

#include "Define.h"
#include "IPRange.h"
#include "NetUtil.h"

#define CLOCK_PER_MS 1000
#define ICMP_HEADER_SIZE 8
#define ICMP_DATA "M103140002"
#define ICMP_DATA_SIZE 11
#define SEND_PACKET_SIZE 64

#define IP_ADDRESS_LENGTH 15
#define BUFFER_SIZE 256
char receivePacket[BUFFER_SIZE];

// 使用者輸入
char* adapterName;
int timeout;

int sequence = 0;
int socketFD;
pid_t netPid;

char ipAddressString[IP_ADDRESS_LENGTH], subnetMaskString[IP_ADDRESS_LENGTH];
struct sockaddr_in dstSocketAddress;

// 取得網路卡資訊
void queryLocalAdapter() {
    struct ifreq adapter;
    adapter.ifr_addr.sa_family = AF_INET;
    memcpy(adapter.ifr_name, adapterName, strlen(adapterName));

    if (ioctl(socketFD, SIOCGIFADDR, &adapter) != 0) {
        perror("取得網路卡 IP 位址時失敗 ");
        exit(EXIT_FAILURE);
    }
    struct sockaddr_in* socketAddressPointer = (struct sockaddr_in*)&adapter.ifr_addr;
    struct in_addr ipAddress = socketAddressPointer->sin_addr;

    if (ioctl(socketFD, SIOCGIFNETMASK, &adapter) != 0) {
        perror("取得子網路遮罩時失敗 ");
        exit(EXIT_FAILURE);
    }
    socketAddressPointer = (struct sockaddr_in*)&adapter.ifr_addr;
    struct in_addr subnetMask = socketAddressPointer->sin_addr;

    strcpy(ipAddressString, inet_ntoa(ipAddress));
    strcpy(subnetMaskString, inet_ntoa(subnetMask));
}

void fillSendPacket(struct ip_with_icmp_packet* packet, const char* dstIPAddressString) {
    // IP 包頭
    inet_pton(AF_INET, dstIPAddressString, &packet->ip.ip_dst);
    packet->ip.ip_hl = 5; // IP 包頭長度
    packet->ip.ip_id = 0; // ID = 0
    packet->ip.ip_off = htons(IP_DF); // 不分片
    packet->ip.ip_p = IPPROTO_ICMP; // ICMP 協定
    packet->ip.ip_ttl = 1; // TTL = 1
    packet->ip.ip_v = 4; // IPv4

    // ICMP 協定
    packet->icmp.icmp_type = ICMP_ECHO;
    packet->icmp.icmp_code = 0;
    packet->icmp.icmp_cksum = 0;
    packet->icmp.icmp_hun.ih_idseq.icd_seq = htons(++sequence);
    packet->icmp.icmp_hun.ih_idseq.icd_id = netPid;

    memcpy(packet->icmp.icmp_data, ICMP_DATA, ICMP_DATA_SIZE);
    packet->icmp.icmp_cksum = calculateCheckSum((uint16_t*)&(packet->icmp), ICMP_HEADER_SIZE + ICMP_DATA_SIZE);
}

int main(int argc, char** argv) {
    if (geteuid() != 0) {
        perror("請以 Root 權限執行本程式 ... ");
        return EXIT_FAILURE;
    }
    if (argc < 5 || strcmp(argv[1], "-i") != 0 || strcmp(argv[3], "-t") != 0) {
        printf("使用方法 : %s -i [網路卡名稱] -t [超時等待時間 (毫秒)]\n", argv[0]);
        return EXIT_FAILURE;
    }
    adapterName = argv[2];
    timeout = atoi(argv[4]);
    if (timeout <= 0) {
        perror("超時等待時間輸入錯誤 ");
        return EXIT_FAILURE;
    }

    int pid = getpid();
    printf("超時等待時間 : %d 毫秒\n", timeout);
    printf("進程 ID : %d\n", pid);
    netPid = htons(pid);

    if ((socketFD = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP)) == INVALID_SOCKET) {
        perror("建立 Socket 時發生錯誤 ");
        return EXIT_FAILURE;
    }

    BOOL enable = TRUE;
    if (setsockopt(socketFD, IPPROTO_IP, IP_HDRINCL, &enable, sizeof(enable)) != 0) {
        perror("Socket 開啟 IP_HDRINCL 時發生錯誤 ");
        return EXIT_FAILURE;
    }

    struct timeval timeoutStruct;
    int second = timeout / 1000;
    timeoutStruct.tv_sec = second;
    timeoutStruct.tv_usec = (timeout - second * 1000) * 1000;
    if (setsockopt(socketFD, SOL_SOCKET, SO_RCVTIMEO, &timeoutStruct, sizeof(timeoutStruct)) != 0) {
        perror("設定 Socket 超時等待時間時發生錯誤 ");
        return EXIT_FAILURE;
    }

    queryLocalAdapter();
    printf("本機 IP 位址 : %s\n", ipAddressString);
    printf("子網路遮罩 : %s\n", subnetMaskString);

    struct IPRange range = getIPRangeBySubnetMask(ipAddressString, subnetMaskString);
    clock_t startTime;

    bzero(&dstSocketAddress, sizeof(dstSocketAddress));
    dstSocketAddress.sin_family = AF_INET;
    for (uint32_t i = range.startIP + 1; i <= range.endIP - 1; ++i) {
        dstSocketAddress.sin_addr.s_addr = htonl(i);
        char* dstIPAddressString = inet_ntoa(dstSocketAddress.sin_addr);
        if (strcmp(dstIPAddressString, ipAddressString) == 0) {
            continue;
        }

        struct ip_with_icmp_packet* packet = (struct ip_with_icmp_packet*)malloc(SEND_PACKET_SIZE);
        bzero(packet, SEND_PACKET_SIZE);
        fillSendPacket(packet, dstIPAddressString);

        printf("Ping 請求 %s (序號 : %d)\n", dstIPAddressString, sequence);
        if (sendto(socketFD, packet, SEND_PACKET_SIZE, 0, (struct sockaddr*)&dstSocketAddress, sizeof(dstSocketAddress)) < 0) {
            perror("發送封包時發生錯誤 ");
            exit(EXIT_FAILURE);
        }
        startTime = clock();

        BOOL receiveLoop = TRUE;
        while (receiveLoop) {
            int size = recv(socketFD, &receivePacket, BUFFER_SIZE, 0);
            if (size < 0) {
                if (errno != EAGAIN) {
                    perror("接收封包時發生錯誤 ");
                    exit(EXIT_FAILURE);
                }
                printf("Ping %s 回應等待超時\n", dstIPAddressString);
                break;
            }

            struct ip* ip = (struct ip*)receivePacket;
            struct icmp* icmp = (struct icmp*)(receivePacket + ip->ip_hl * 4);

            if (icmp->icmp_type == ICMP_ECHOREPLY && icmp->icmp_hun.ih_idseq.icd_id == netPid) {
                struct sockaddr_in receiveSocketAddress;
                receiveSocketAddress.sin_addr.s_addr = ip->ip_src.s_addr;
                char* receiveIPAddressString = inet_ntoa(receiveSocketAddress.sin_addr);

                uint16_t receiveSequence = htons(icmp->icmp_hun.ih_idseq.icd_seq);
                uint16_t dstSequence = htons(packet->icmp.icmp_hun.ih_idseq.icd_seq);
                if (strcmp(receiveIPAddressString, dstIPAddressString) != 0 || receiveSequence != dstSequence) {
                    continue;
                }

                clock_t endTime = clock();
                double delay = (endTime - startTime) / (double) CLOCK_PER_MS;
                printf("接收來自 %s Ping 回應 (延遲 : %.3f 毫秒)\n", receiveIPAddressString, delay);
                receiveLoop = FALSE;
            }
        }
    }

    close(socketFD);
    return EXIT_SUCCESS;
}
