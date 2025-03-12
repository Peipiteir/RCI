#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#define MAX_BUFFER 256
#define MAX_INTERNAL 10
#define MAX_CLIENTS 10

// Structure to store neighbors (topology)
typedef struct {
    char ip[INET_ADDRSTRLEN];
    int port;
} Neighbor;

// Global variables for TCP (topology)
Neighbor externalNeighbor = {"", 0};
Neighbor safeguardNeighbor = {"", 0};
Neighbor internalNeighbors[MAX_INTERNAL];
int numInternal = 0;

// Node identifier (IP and TCP port)
char myIP[INET_ADDRSTRLEN];
int myPort;
char myTCP[16];  // string for the TCP port number

// UDP globals
int udp_sock;  // UDP socket
struct sockaddr_storage server_addr;
socklen_t server_addr_len;

// Function to display the current topology
void show_topology() {
    printf("----- Current Topology -----\n");
    printf("External Neighbor: %s:%d\n", externalNeighbor.ip, externalNeighbor.port);
    printf("Safeguard Neighbor: %s:%d\n", safeguardNeighbor.ip, safeguardNeighbor.port);
    printf("Internal Neighbors (%d):\n", numInternal);
    for (int i = 0; i < numInternal; i++) {
        printf("  %s:%d\n", internalNeighbors[i].ip, internalNeighbors[i].port);
    }
    printf("----------------------------\n");
}

// Function to add an internal neighbor
void add_internal_neighbor(const char *ip, int port) {
    if (numInternal < MAX_INTERNAL) {
        strcpy(internalNeighbors[numInternal].ip, ip);
        internalNeighbors[numInternal].port = port;
        numInternal++;
        printf("Added internal neighbor: %s:%d\n", ip, port);
    } else {
        printf("Internal neighbor limit reached.\n");
    }
}

// Process the message received from a connected TCP client
void process_client_message(int client_sock) {
    char buffer[MAX_BUFFER];
    ssize_t n = read(client_sock, buffer, MAX_BUFFER - 1);
    if (n > 0) {
        buffer[n] = '\0';
        printf("TCP message received: %s", buffer);
        char command[16], ip[INET_ADDRSTRLEN];
        int port;
        if (sscanf(buffer, "%s %s %d", command, ip, &port) == 3) {
            if (strcmp(command, "ENTRY") == 0) {
                add_internal_neighbor(ip, port);
            } else if (strcmp(command, "SAFE") == 0) {
                strcpy(safeguardNeighbor.ip, ip);
                safeguardNeighbor.port = port;
                printf("Updated safeguard neighbor: %s:%d\n", ip, port);
            } else {
                printf("Unknown TCP command: %s\n", command);
            }
        } else {
            printf("Invalid TCP message format.\n");
        }
    } else if (n == 0) {
        // Connection closed by the client
    } else {
        perror("Error reading from client socket");
    }
    close(client_sock);
}

// Function to perform direct join (command "dj" or "j")
// If connectIP is "0.0.0.0", creates a network with just this node.
void direct_join(const char *net, const char *connectIP, int connectPort) {
    // If connectIP is "0.0.0.0", create network with the node itself.
    if (strcmp(connectIP, "0.0.0.0") == 0) {
        printf("Creating network with this node (first node).\n");
        strcpy(externalNeighbor.ip, myIP);
        externalNeighbor.port = myPort;
        strcpy(safeguardNeighbor.ip, myIP);
        safeguardNeighbor.port = myPort;
        return;
    }

    // Otherwise, connect to the indicated node via TCP.
    int sockfd;
    struct sockaddr_in serv_addr;
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("Error creating socket for direct join");
        return;
    }
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(connectPort);
    serv_addr.sin_addr.s_addr = inet_addr(connectIP);
    if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Error connecting for direct join");
        close(sockfd);
        return;
    }
    char message[MAX_BUFFER];
    snprintf(message, sizeof(message), "ENTRY %s %d\n", myIP, myPort);
    write(sockfd, message, strlen(message));
    printf("Sent ENTRY to %s:%d\n", connectIP, connectPort);
    // Update the external neighbor of this node
    strcpy(externalNeighbor.ip, connectIP);
    externalNeighbor.port = connectPort;
    close(sockfd);
}

// Function to send registration message via UDP
int perform_registration(const char *net) {
    char reg_msg[MAX_BUFFER];
    snprintf(reg_msg, sizeof(reg_msg), "REG %s %s %s", net, myIP, myTCP);
    if(sendto(udp_sock, reg_msg, strlen(reg_msg), 0,
              (struct sockaddr*)&server_addr, server_addr_len) < 0) {
        perror("Error in sendto REG");
        return -1;
    }
    printf("Sent REG via UDP: %s\n", reg_msg);
    return 0;
}

// Function to send join command via UDP
int perform_join(const char *net) {
    char msg[MAX_BUFFER];
    snprintf(msg, sizeof(msg), "NODES %s", net);
    if(sendto(udp_sock, msg, strlen(msg), 0,
              (struct sockaddr*)&server_addr, server_addr_len) < 0) {
        perror("Error in sendto NODES");
        return -1;
    }
    printf("Sent NODES via UDP: %s\n", msg);
    return 0;
}

int main(int argc, char *argv[]) {
    // Usage: ./ndn cache IP TCP regIP regUDP
    if (argc < 6) {
        fprintf(stderr, "Usage: %s cache IP TCP regIP regUDP\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    int cache_size = atoi(argv[1]);
    strcpy(myIP, argv[2]);
    strcpy(myTCP, argv[3]);         // store TCP port as a string
    myPort = atoi(argv[3]);         // convert to integer for local operations
    const char *regIP = argv[4];
    const char *regUDP = argv[5];

    printf("Starting NDN node: %s:%d, cache=%d, reg server %s:%s\n",
           myIP, myPort, cache_size, regIP, regUDP);

    // Configure TCP server socket
    int server_sock;
    struct sockaddr_in serv_addr;
    server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0) {
        perror("Error creating TCP server socket");
        exit(EXIT_FAILURE);
    }
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = inet_addr(myIP); // or use INADDR_ANY if preferred
    serv_addr.sin_port = htons(myPort);
    if (bind(server_sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Error in TCP server bind");
        exit(EXIT_FAILURE);
    }
    if (listen(server_sock, 5) < 0) {
        perror("Error in TCP server listen");
        exit(EXIT_FAILURE);
    }
    printf("TCP Server listening on %s:%d\n", myIP, myPort);

    // Configure UDP socket for communication with the node server
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;      // IPv4
    hints.ai_socktype = SOCK_DGRAM; // UDP
    int err = getaddrinfo(regIP, regUDP, &hints, &res);
    if(err != 0) {
        fprintf(stderr, "getaddrinfo UDP: %s\n", gai_strerror(err));
        exit(EXIT_FAILURE);
    }
    udp_sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if(udp_sock < 0) {
        perror("Error creating UDP socket");
        exit(EXIT_FAILURE);
    }
    memcpy(&server_addr, res->ai_addr, res->ai_addrlen);
    server_addr_len = res->ai_addrlen;
    freeaddrinfo(res);
    printf("UDP Socket configured for server %s:%s\n", regIP, regUDP);

    // Array to store connected TCP client sockets
    int client_socks[MAX_CLIENTS];
    for (int i = 0; i < MAX_CLIENTS; i++) {
        client_socks[i] = -1;
    }

    // Main loop: multiplexes STDIN, TCP server socket, client sockets, and UDP socket
    while (1) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        // Add STDIN
        FD_SET(STDIN_FILENO, &read_fds);
        int max_fd = STDIN_FILENO;
        // Add TCP server socket
        FD_SET(server_sock, &read_fds);
        if (server_sock > max_fd)
            max_fd = server_sock;
        // Add TCP client sockets
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (client_socks[i] != -1) {
                FD_SET(client_socks[i], &read_fds);
                if (client_socks[i] > max_fd)
                    max_fd = client_socks[i];
            }
        }
        // Add UDP socket
        FD_SET(udp_sock, &read_fds);
        if (udp_sock > max_fd)
            max_fd = udp_sock;

        int activity = select(max_fd + 1, &read_fds, NULL, NULL, NULL);
        if (activity < 0) {
            if (errno == EINTR)
                continue;
            perror("Error in select");
            break;
        }

        // Process user commands from STDIN
        if (FD_ISSET(STDIN_FILENO, &read_fds)) {
            char input[MAX_BUFFER];
            if (fgets(input, MAX_BUFFER, stdin) != NULL) {
                input[strcspn(input, "\n")] = 0;
                // Direct join command: dj net connectIP connectTCP
                if (strncmp(input, "dj", 2) == 0) {
                    char cmd[10], net[16], connectIP[INET_ADDRSTRLEN];
                    int connectPort;
                    if (sscanf(input, "%s %s %s %d", cmd, net, connectIP, &connectPort) == 4) {
                        direct_join(net, connectIP, connectPort);
                        // After direct join, register via UDP
                        perform_registration(net);
                    } else {
                        printf("Invalid format for dj. Usage: dj net connectIP connectTCP\n");
                    }
                }
                // Join command: j net
                else if (strncmp(input, "j", 1) == 0) {
                    char cmd[10], net[16];
                    if (sscanf(input, "%s %s", cmd, net) == 2) {
                        // Simulation: ask for connection details via STDIN
                        char connectIP[INET_ADDRSTRLEN];
                        char portStr[10];
                        int connectPort;
                        printf("Enter connectIP (or 0.0.0.0 to create network): ");
                        if(fgets(connectIP, sizeof(connectIP), stdin)==NULL) continue;
                        connectIP[strcspn(connectIP, "\n")] = 0;
                        if(strcmp(connectIP, "0.0.0.0") != 0) {
                            printf("Enter connectPort: ");
                            if(fgets(portStr, sizeof(portStr), stdin)==NULL) continue;
                            connectPort = atoi(portStr);
                        } else {
                            connectPort = 0;
                        }
                        direct_join(net, connectIP, connectPort);
                        // Send NODES via UDP to get nodes list and register
                        perform_join(net);
                        perform_registration(net);
                    } else {
                        printf("Invalid format for join. Usage: j net\n");
                    }
                }
                // Command to show topology: st
                else if (strncmp(input, "st", 2) == 0) {
                    show_topology();
                }
                // Command to exit: x
                else if (strncmp(input, "x", 1) == 0) {
                    printf("Exiting...\n");
                    break;
                }
                else {
                    printf("Unknown command.\n");
                }
            }
        }

        // Process new connections on the TCP server socket
        if (FD_ISSET(server_sock, &read_fds)) {
            struct sockaddr_in cli_addr;
            socklen_t cli_len = sizeof(cli_addr);
            int new_sock = accept(server_sock, (struct sockaddr *)&cli_addr, &cli_len);
            if (new_sock < 0) {
                perror("Error on accept");
            } else {
                // Add the new socket to the client sockets array
                int added = 0;
                for (int i = 0; i < MAX_CLIENTS; i++) {
                    if (client_socks[i] == -1) {
                        client_socks[i] = new_sock;
                        added = 1;
                        break;
                    }
                }
                if (!added) {
                    printf("Maximum connections reached. Closing new connection.\n");
                    close(new_sock);
                }
            }
        }

        // Process UDP messages (e.g., responses from the node server)
        if (FD_ISSET(udp_sock, &read_fds)) {
            char udp_buffer[MAX_BUFFER];
            ssize_t n = recvfrom(udp_sock, udp_buffer, MAX_BUFFER - 1, 0, NULL, NULL);
            if (n > 0) {
                udp_buffer[n] = '\0';
                printf("UDP message received: %s\n", udp_buffer);
                // Here you can implement handling of OKREG, NODESLIST, etc.
            }
        }

        // Process TCP client sockets data
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (client_socks[i] != -1 && FD_ISSET(client_socks[i], &read_fds)) {
                process_client_message(client_socks[i]);
                client_socks[i] = -1; // mark the socket as closed
            }
        }
    }

    close(server_sock);
    close(udp_sock);
    return 0;
}
