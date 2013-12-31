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

#define RECEIVEBUFFER_SIZE 1E3
#define INIT_PORT 6969
#define INIT_GROUP "0.0.0.0"

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

    int init_socket;
    struct sockaddr_in init_socket_address = {0}, forward_reply_socket_address = {0}, destination_socket_address = {0};

    struct sockaddr_in remote_address;
    socklen_t remote_address_length = sizeof(remote_address);
    char* receive_buffer=malloc(RECEIVEBUFFER_SIZE * sizeof(char));


    if (argc != 4) {
        fprintf(stderr, "Call with 3 arguments:\n%s external-interface-ip-address internal-interface-ip-address sonos-box-ip-address", argv[0]);
    }
    char* outside_nat_ip_address = argv[1];
    char* inside_nat_ip_address = argv[2];
    char* sonos_box_ip_address = argv[3];

    fill_socketaddr_in(&init_socket_address, INIT_PORT, INIT_GROUP);

    init_socket = create_bounded_socket(init_socket_address);

    printf("waiting for connection\n");
    while (1) {
        int nr_bytes_received;
        nr_bytes_received = recvfrom(init_socket, receive_buffer, RECEIVEBUFFER_SIZE, 0, (struct sockaddr*)&remote_address, &remote_address_length);
        if (remote_address.sin_addr.s_addr == htonl(inet_network(outside_nat_ip_address)) || remote_address.sin_addr.s_addr == htonl(inet_network(inside_nat_ip_address))) {
            continue; //loopback
        }

        long r_addr = ntohl(remote_address.sin_addr.s_addr);
        fill_socketaddr_in(&forward_reply_socket_address, 0, inside_nat_ip_address);
        if ((r_addr & 0xFFFFFF00) == (ntohl(forward_reply_socket_address.sin_addr.s_addr) & 0xFFFFFF00)) {
            fill_socketaddr_in(&forward_reply_socket_address, 0, outside_nat_ip_address);
            fill_socketaddr_in(&destination_socket_address, INIT_PORT, outside_nat_ip_address);
        } else {
            fill_socketaddr_in(&destination_socket_address, INIT_PORT, inside_nat_ip_address);
        }
        destination_socket_address.sin_addr.s_addr = htonl(ntohl(destination_socket_address.sin_addr.s_addr) & 0xFFFFFF00 | 0xFF);
        printf("Message %s --> ", inet_ntoa(remote_address.sin_addr));
        printf("%s", inet_ntoa(forward_reply_socket_address.sin_addr));
        printf("(%s)\n", inet_ntoa(destination_socket_address.sin_addr));

        int passthrough_socket = create_bounded_socket(forward_reply_socket_address);
        int broadcastEnable=1;
        if (setsockopt(passthrough_socket, SOL_SOCKET, SO_BROADCAST, &broadcastEnable, sizeof(broadcastEnable)) < 0) {
            perror("setopt failed");
        }
        
        if (sendto(passthrough_socket, receive_buffer, nr_bytes_received, 0, (struct sockaddr*)&destination_socket_address, sizeof(destination_socket_address)) <= 0) {
            perror("send failed");
        }
        close(passthrough_socket);
    }
}
