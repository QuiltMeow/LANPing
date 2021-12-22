#pragma once

struct IPRange {
    unsigned long startIP, endIP;
};

struct IPRange getIPRangeBySubnetMask(const char* ipAddressString, const char* subnetMaskString) {
    struct in_addr ipAddress, subnetMask;
    inet_pton(AF_INET, ipAddressString, &ipAddress);
    inet_pton(AF_INET, subnetMaskString, &subnetMask);

    struct IPRange ret;
    ret.startIP = ntohl(ipAddress.s_addr & subnetMask.s_addr);
    ret.endIP = ntohl(ipAddress.s_addr | ~subnetMask.s_addr);
    return ret;
}
