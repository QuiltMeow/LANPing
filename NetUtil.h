#pragma once

struct ip_with_icmp_packet {
    struct ip ip;
    struct icmp icmp;
};

// 計算 Check Sum
uint16_t calculateCheckSum(uint16_t* packet, const int length) {
    int available = length;
    uint32_t sum = 0;
    uint16_t* data = packet;
    uint16_t ret = 0;

    while (available > 1) {
        sum += *data++;
        available -= 2;
    }

    if (available == 1) {
        *(unsigned char*)(&ret) = *(unsigned char*)data;
        sum += ret;
    }

    sum = (sum >> 16) + (sum & 0xFFFF);
    sum += (sum >> 16);
    ret = ~sum;
    return ret;
}
