#include <sys/socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "str_replace.h"
#include <syslog.h>

#define RECEIVEBUFFER_SIZE 1E3
#define SSDP_PORT 1900
#define SSDP_GROUP "239.255.255.250"
#define REPLY_TIMEOUT_USEC 5E5
#define SONOS_LOCATION_TEMPLATE "\r\nLOCATION: http://%s:1400/xml/device_description.xml\r\n"

inline char* sonos_location_string(char *ip) {
    char* temp;
    asprintf(&temp, SONOS_LOCATION_TEMPLATE, ip);
    return temp;
}

void fill_socketaddr_in(struct sockaddr_in* sock_address, uint16_t port, const char* ip_address) {
    sock_address->sin_family = AF_INET;
    sock_address->sin_port = htons(port);

    if (inet_pton(AF_INET, ip_address, &sock_address->sin_addr) != 1) {
        fprintf(stderr, "Not a valid address: %s\n", ip_address);
        exit(1);
   }
}

int create_bounded_socket(struct sockaddr_in sock_address) {
    int sock;
    
    if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("cannot create socket");
        exit(1);
    }

    if (bind(sock, (struct sockaddr *)&sock_address, sizeof(sock_address)) < 0) {
        perror("bind failed");
        exit(1);
    }

    return sock;
}

int main(int argc, char** argv) {
    openlog("sonos-f", LOG_PID|LOG_CONS, LOG_DAEMON);

    int ssdp_socket, forward_reply_socket;
    struct sockaddr_in ssdp_socket_address = {0}, forward_reply_socket_address = {0};
    struct sockaddr_in remote_address;
    socklen_t remote_address_length = sizeof(remote_address);
    char* receive_buffer=malloc(RECEIVEBUFFER_SIZE * sizeof(char));
    struct ip_mreq mreq;
    struct timeval timeout = {0, REPLY_TIMEOUT_USEC};


    if (argc != 4) {
        fprintf(stderr, "Call with 3 arguments:\n%s external-interface-ip-address internal-interface-ip-address sonos-box-ip-address", argv[0]);
    }
    char* outside_nat_ip_address = argv[1];
    char* inside_nat_ip_address = argv[2];
    char* sonos_box_ip_address = argv[3];

    char* to_replace = sonos_location_string(sonos_box_ip_address);
    char* replace_with = sonos_location_string(outside_nat_ip_address);

    fill_socketaddr_in(&ssdp_socket_address, SSDP_PORT, SSDP_GROUP);
    fill_socketaddr_in(&forward_reply_socket_address, SSDP_PORT, outside_nat_ip_address);

    ssdp_socket = create_bounded_socket(ssdp_socket_address);
    forward_reply_socket = create_bounded_socket(forward_reply_socket_address);
    inet_pton(AF_INET, argv[1], &mreq.imr_interface);
    mreq.imr_multiaddr.s_addr = inet_addr(SSDP_GROUP);

    if (setsockopt(ssdp_socket, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
        perror("setsockopt mreq");
        exit(1);
    }         

    while (1) {
        int nr_bytes_received;
        nr_bytes_received = recvfrom(ssdp_socket, receive_buffer, RECEIVEBUFFER_SIZE, 0, (struct sockaddr*)&remote_address, &remote_address_length);
        if(strstr(receive_buffer, "ZonePlayer") != NULL) {
            struct sockaddr_in passthrough_socket_address = {0};
            fill_socketaddr_in(&passthrough_socket_address, 0, inside_nat_ip_address);
            int passthrough_socket = create_bounded_socket(passthrough_socket_address);
            sendto(passthrough_socket, receive_buffer, nr_bytes_received, 0, (struct sockaddr*)&ssdp_socket_address, sizeof(ssdp_socket_address));
            if (setsockopt(passthrough_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
                perror("Error");
                exit(1);
            }
            nr_bytes_received = recv(passthrough_socket, receive_buffer, RECEIVEBUFFER_SIZE, 0);
            if (nr_bytes_received <= 0) {
                syslog(LOG_DEBUG, "Message %s --> internal", inet_ntoa(remote_address.sin_addr));
            } else {
                int send_buffer_size = nr_bytes_received - strlen(to_replace) + strlen(replace_with) + 1;
                char* send_buffer = str_replace(receive_buffer, to_replace, replace_with, STR_REPLACE_REPLACE_ALL);
                sendto(forward_reply_socket, send_buffer, send_buffer_size, 0, (struct sockaddr*) &remote_address, remote_address_length); 
                free(send_buffer);
                syslog(LOG_DEBUG, "Message %s <-> internal", inet_ntoa(remote_address.sin_addr));
            }
        } else {
            syslog(LOG_DEBUG, "Message %s not routed", inet_ntoa(remote_address.sin_addr));
        }
    }
}
