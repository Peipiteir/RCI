#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netdb.h>
#include <time.h>

#define MAX_NODES 10
#define BUFFER_SIZE 1024
#define REG_SERVER_IP "193.136.138.142"
#define REG_SERVER_PORT "59000"

typedef struct {
    char ip[INET_ADDRSTRLEN];
    int port;
} NodeID;

typedef struct {
    NodeID external;
    NodeID safeguard;
    NodeID internals[MAX_NODES];
    int num_internals;
} Topology;

typedef struct {
    int cache_size;
    NodeID self;
    NodeID reg_server;
    Topology topology;
    int tcp_fd;
    int udp_fd;
    fd_set read_fds;
} NDNNode;

// Funções auxiliares
void init_node(NDNNode *node, int cache, char *ip, int tcp_port, char *reg_ip, int reg_udp) {
    node->cache_size = cache;
    strcpy(node->self.ip, ip);
    node->self.port = tcp_port;
    strcpy(node->reg_server.ip, reg_ip);
    node->reg_server.port = reg_udp;
    memset(&node->topology, 0, sizeof(Topology));
    node->topology.num_internals = 0;
}

int setup_socket(const char *port, int socktype, int flags) {
    struct addrinfo hints, *res;
    int fd, errcode;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = socktype;
    hints.ai_flags = flags;

    if ((errcode = getaddrinfo(NULL, port, &hints, &res))) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(errcode));
        return -1;
    }

    if ((fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol)) == -1) {
        perror("socket");
        freeaddrinfo(res);
        return -1;
    }

    if (bind(fd, res->ai_addr, res->ai_addrlen) == -1) {
        perror("bind");
        close(fd);
        freeaddrinfo(res);
        return -1;
    }

    freeaddrinfo(res);
    return fd;
}

int tcp_connect(const char *host, const char *port) {
    struct addrinfo hints, *res;
    int fd, errcode;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    if ((errcode = getaddrinfo(host, port, &hints, &res))) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(errcode));
        return -1;
    }

    if ((fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol)) == -1) {
        perror("socket");
        freeaddrinfo(res);
        return -1;
    }

    if (connect(fd, res->ai_addr, res->ai_addrlen) == -1) {
        perror("connect");
        close(fd);
        freeaddrinfo(res);
        return -1;
    }

    freeaddrinfo(res);
    return fd;
}

// Comandos
void show_topology(NDNNode *node) {
    printf("\n=== Network Topology ===\n");
    printf("External Neighbor: %s:%d\n", node->topology.external.ip, node->topology.external.port);
    printf("Safeguard Node:    %s:%d\n", node->topology.safeguard.ip, node->topology.safeguard.port);
    printf("Internal Neighbors:\n");
    for (int i = 0; i < node->topology.num_internals; i++) {
        printf("- %s:%d\n", node->topology.internals[i].ip, node->topology.internals[i].port);
    }
    printf("=======================\n");
}

void handle_direct_join(NDNNode *node, char *net, char *connect_ip, int connect_port) {
    if (strcmp(connect_ip, "0.0.0.0") == 0) {
        printf("Created new network %s\n", net);
        node->topology.external.port = -1;
        return;
    }

    char port_str[6];
    snprintf(port_str, sizeof(port_str), "%d", connect_port);
    int sock = tcp_connect(connect_ip, port_str);
    
    if (sock == -1) {
        fprintf(stderr, "Connection to %s:%d failed\n", connect_ip, connect_port);
        return;
    }

    char entry_msg[BUFFER_SIZE];
    snprintf(entry_msg, BUFFER_SIZE, "ENTRY %s %d\n", node->self.ip, node->self.port);
    if (send(sock, entry_msg, strlen(entry_msg), 0) == -1) {
        perror("send");
    }

    char response[BUFFER_SIZE];
    ssize_t n = recv(sock, response, BUFFER_SIZE - 1, 0);
    if (n > 0) {
        response[n] = '\0';
        if (strncmp(response, "SAFE", 4) == 0) {
            sscanf(response + 5, "%s %d", node->topology.safeguard.ip, &node->topology.safeguard.port);
        }
    }

    strcpy(node->topology.external.ip, connect_ip);
    node->topology.external.port = connect_port;
    close(sock);
    printf("Successfully joined network through %s:%d\n", connect_ip, connect_port);
}

void process_command(NDNNode *node, char *command) {
    char cmd[20], net[4], ip[16], port_str[6];
    
    if (sscanf(command, "direct join %3s %15s %5s", net, ip, port_str) == 3) {
        int port = atoi(port_str);
        handle_direct_join(node, net, ip, port);
    }
    else if (strncmp(command, "show topology", 12) == 0) {
        show_topology(node);
    }
    else if (strncmp(command, "exit", 4) == 0) {
        close(node->tcp_fd);
        close(node->udp_fd);
        exit(0);
    }
    else {
        printf("Unknown command\n");
    }
}

// Loop principal
void event_loop(NDNNode *node) {
    FD_ZERO(&node->read_fds);
    FD_SET(STDIN_FILENO, &node->read_fds);
    FD_SET(node->tcp_fd, &node->read_fds);

    while (1) {
        fd_set tmp_fds = node->read_fds;
        if (select(FD_SETSIZE, &tmp_fds, NULL, NULL, NULL) == -1) {
            perror("select");
            exit(EXIT_FAILURE);
        }

        if (FD_ISSET(STDIN_FILENO, &tmp_fds)) {
            char command[BUFFER_SIZE];
            if (fgets(command, BUFFER_SIZE, stdin)) {
                process_command(node, command);
            }
        }

        if (FD_ISSET(node->tcp_fd, &tmp_fds)) {
            struct sockaddr_in addr;
            socklen_t addrlen = sizeof(addr);
            int newfd = accept(node->tcp_fd, (struct sockaddr *)&addr, &addrlen);
            close(newfd); // Simplificado para exemplo
        }
    }
}

int main(int argc, char *argv[]) {
    if (argc < 4) {
        fprintf(stderr, "Usage: %s cache IP TCP [regIP regUDP]\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    NDNNode node = {0};
    init_node(&node, atoi(argv[1]), argv[2], atoi(argv[3]),
             (argc > 4) ? argv[4] : REG_SERVER_IP,
             (argc > 5) ? atoi(argv[5]) : atoi(REG_SERVER_PORT));

    char tcp_port[6];
    snprintf(tcp_port, sizeof(tcp_port), "%d", node.self.port);
    node.tcp_fd = setup_socket(tcp_port, SOCK_STREAM, AI_PASSIVE);
    
    if (listen(node.tcp_fd, 5) == -1) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    printf("Node %s:%d ready\n", node.self.ip, node.self.port);
    event_loop(&node);

    return 0;
}