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

#define NOTIFY_PORT 3400
#define RECEIVE_BUFFER_SIZE 1E5
#define REPLY_TIMEOUT_SEC 5
#define LISTEN_BACKLOG 5
#define HOST_LINE "\r\nHOST: "

void fill_socketaddr_in(struct sockaddr_in* sock_address, uint16_t port, const char* ip_address) {
    sock_address->sin_family = AF_INET;
    sock_address->sin_port = htons(port);

    if (inet_pton(AF_INET, ip_address, &sock_address->sin_addr) != 1) {
        fprintf(stderr, "Not a valid address: %s\n", ip_address);
        exit(1);
   }
}

int create_bounded_tcp_socket(struct sockaddr_in sock_address) {
    int sock;
    
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("cannot create socket");
        exit(1);
    }

    if (bind(sock, (struct sockaddr *)&sock_address, sizeof(sock_address)) < 0) {
        perror("bind failed");
        exit(1);
    }

    return sock;
}

char* get_host(const char* buffer) {
        char* host_start = strstr(buffer, HOST_LINE) + strlen(HOST_LINE);
        char* host_end = strstr(host_start, ":");
        char* host = malloc((host_end - host_start + 1));
        strncpy(host, host_start, host_end - host_start);
        host[host_end - host_start] = '\0';
        return host;
}

unsigned short get_port(const char* buffer) {
        char* host_start = strstr(buffer, HOST_LINE) + strlen(HOST_LINE);
        char* host_end = strstr(host_start, ":");
        return (unsigned short) strtol(host_end + 1, NULL, 10);
}


int main(int argc, char** argv) {
    int notify_socket, forward_socket, sonos_connection;
    struct sockaddr_in notify_socket_address = {0}, forward_destination_address = {0}, sonos_box_address;
    socklen_t sonos_box_address_length;
    char* receive_buffer=malloc(RECEIVE_BUFFER_SIZE * sizeof(char));
    struct timeval timeout = {REPLY_TIMEOUT_SEC, 0};
    
    if (argc != 4) {
        fprintf(stderr, "Call with 3 arguments:\n%s external-interface-ip-address internal-interface-ip-address sonos-box-ip-address", argv[0]);
    }
    char* outside_nat_ip_address = argv[1];
    char* inside_nat_ip_address = argv[2];
    char* sonos_box_ip_address = argv[3];

    fill_socketaddr_in(&notify_socket_address, NOTIFY_PORT, inside_nat_ip_address);

    notify_socket = create_bounded_tcp_socket(notify_socket_address);

    listen(notify_socket, LISTEN_BACKLOG);

    while (1) {
        int nr_bytes_receieved;
        printf("waiting for connection\n");
        sonos_connection = accept(notify_socket, (struct sockaddr *)&sonos_box_address, &sonos_box_address_length);
        usleep(5E5); //sleep for the complete request to be available
        nr_bytes_receieved = recv(sonos_connection, receive_buffer, RECEIVE_BUFFER_SIZE, 0);
        char* send_buffer = str_replace(receive_buffer, sonos_box_ip_address, outside_nat_ip_address);
        char* host = get_host(send_buffer);
        unsigned short port = get_port(send_buffer);
        fill_socketaddr_in(&forward_destination_address, port, host);
        forward_socket = socket(AF_INET, SOCK_STREAM, 0);
        if (setsockopt(forward_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
            perror("Error");
        }
        connect(forward_socket, (struct sockaddr*) &forward_destination_address, sizeof(forward_destination_address));
        printf("forwarding %s\n", send_buffer);
        send(forward_socket, send_buffer, strlen(send_buffer), 0);
        usleep(5E5);
        nr_bytes_receieved = recv(forward_socket, receive_buffer, RECEIVE_BUFFER_SIZE, 0);
        printf("replying %s\n", receive_buffer);
        send(sonos_connection, receive_buffer, strlen(receive_buffer), 0);
        close(forward_socket);
        close(sonos_connection);
        free(host);
        free(send_buffer);
    }
}
