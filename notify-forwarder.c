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
#include <errno.h>

#define NOTIFY_PORT 3400
#define MAX_HEADERS_SIZE 10000
#define REPLY_TIMEOUT_USEC 5E5
#define LISTEN_BACKLOG 5
#define HOST_LINE "\r\nHOST: "
#define CONTENT_LENGTH_LINE "\r\nCONTENT-LENGTH:"

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

unsigned short get_content_length(const char* buffer) {
    char* cl_start = strstr(buffer, CONTENT_LENGTH_LINE);
    if (!cl_start) {
        return 0;
    }
    return (unsigned short) strtol(cl_start + strlen(CONTENT_LENGTH_LINE), NULL, 10);
}

unsigned short get_port(const char* buffer) {
    char* host_start = strstr(buffer, HOST_LINE) + strlen(HOST_LINE);
    char* host_end = strstr(host_start, ":");
    return (unsigned short) strtol(host_end + 1, NULL, 10);
}


char* receive_http(int conn) {
        int nr_bytes_received=0;
        char* headers = malloc(MAX_HEADERS_SIZE * sizeof(char));
        while(1) {
            if (nr_bytes_received == MAX_HEADERS_SIZE) {
                headers[MAX_HEADERS_SIZE - 1] = '\0';
                fprintf(stderr, "No header: %s\n", headers);
                exit(1);
            }

            int nr_bytes = recvfrom(conn, headers + nr_bytes_received, MAX_HEADERS_SIZE - nr_bytes_received - 1, 0, NULL, NULL);
            if (nr_bytes < 0) {
                free(headers);
                return NULL;
            }
            nr_bytes_received += nr_bytes;
            headers[nr_bytes_received] = '\0';
            if (strstr(headers, "\r\n\r\n")) {
                break;
            }
            usleep(5E4);
        }
        int header_length = strstr(headers, "\r\n\r\n") - headers + 4;
        int body_length = get_content_length(headers);
        int alloc_size = header_length + body_length + 1;
        char* data = realloc(headers, alloc_size * sizeof(char));
        data[alloc_size-1] = '\0';
        while (nr_bytes_received != header_length + body_length) {
            int nr_bytes = recvfrom(conn, data+nr_bytes_received, alloc_size-nr_bytes_received, 0, NULL, NULL);
            usleep(5E4);
            if (nr_bytes < 0) {
                free(headers);
                return NULL;
            }
            nr_bytes_received += nr_bytes;
        }
        return data;
}

int main(int argc, char** argv) {
    int notify_socket, forward_socket, sonos_connection;
    struct sockaddr_in notify_socket_address = {0}, forward_destination_address = {0}, sonos_box_address;
    socklen_t sonos_box_address_length=sizeof(sonos_box_address);
    struct timeval timeout = {0, REPLY_TIMEOUT_USEC};
    
    if (argc != 4) {
        fprintf(stderr, "Call with 3 arguments:\n%s external-interface-ip-address internal-interface-ip-address sonos-box-ip-address", argv[0]);
    }
    char* outside_nat_ip_address = argv[1];
    char* inside_nat_ip_address = argv[2];
    char* sonos_box_ip_address = argv[3];

    fill_socketaddr_in(&notify_socket_address, NOTIFY_PORT, inside_nat_ip_address);

    notify_socket = create_bounded_tcp_socket(notify_socket_address);

    listen(notify_socket, LISTEN_BACKLOG);

    printf("waiting for connection\n");
    while (1) {
        sonos_connection = accept(notify_socket, (struct sockaddr *)&sonos_box_address, &sonos_box_address_length);
        char* request = receive_http(sonos_connection);
        if (!request) {
            printf("strange, no request.... %s\n", strerror(errno));
            continue;
        }
        char* send_buffer = str_replace(request, sonos_box_ip_address, outside_nat_ip_address);
        char* host = get_host(send_buffer);
        unsigned short port = get_port(send_buffer);
        fill_socketaddr_in(&forward_destination_address, port, host);
        forward_socket = socket(AF_INET, SOCK_STREAM, 0);
        if (setsockopt(forward_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
            perror("Error");
        }
        connect(forward_socket, (struct sockaddr*) &forward_destination_address, sizeof(forward_destination_address));
        printf("connection %s --> %s(%d): forwarding %ld bytes...", inet_ntoa(sonos_box_address.sin_addr), host, port, strlen(send_buffer));
        send(forward_socket, send_buffer, strlen(send_buffer), 0);
        char* response = receive_http(forward_socket);
        if (!response) {
            printf("no response (%s)\n", strerror(errno));
            continue;
        }
        printf("replying %ld bytes\n", strlen(response));
        send(sonos_connection, response, strlen(response), 0);
        close(forward_socket);
        close(sonos_connection);
        free(host);
        free(send_buffer);
        free(request);
        free(response);
    }
}
