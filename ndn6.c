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

// Estrutura para armazenar vizinhos (topologia)
typedef struct {
    char ip[INET_ADDRSTRLEN];
    int port;
} Neighbor;

// Variáveis globais para TCP (topologia)
Neighbor externalNeighbor = {"", 0};
Neighbor safeguardNeighbor = {"", 0};
Neighbor internalNeighbors[MAX_INTERNAL];
int numInternal = 0;

// Identificador do nó (IP e porta TCP)
char myIP[INET_ADDRSTRLEN];
int myPort;
char myTCP[16];  // string para o número da porta TCP

// UDP globals
int udp_sock;  // socket UDP
struct sockaddr_storage server_addr;
socklen_t server_addr_len;

// Função para exibir a topologia atual
void show_topology() {
    printf("----- Topologia Atual -----\n");
    printf("Vizinho Externo: %s:%d\n", externalNeighbor.ip, externalNeighbor.port);
    printf("Vizinho de Salvaguarda: %s:%d\n", safeguardNeighbor.ip, safeguardNeighbor.port);
    printf("Vizinhos Internos (%d):\n", numInternal);
    for (int i = 0; i < numInternal; i++) {
        printf("  %s:%d\n", internalNeighbors[i].ip, internalNeighbors[i].port);
    }
    printf("---------------------------\n");
}

// Função para adicionar um vizinho interno
void add_internal_neighbor(const char *ip, int port) {
    if (numInternal < MAX_INTERNAL) {
        strcpy(internalNeighbors[numInternal].ip, ip);
        internalNeighbors[numInternal].port = port;
        numInternal++;
        printf("Adicionado vizinho interno: %s:%d\n", ip, port);
    } else {
        printf("Limite de vizinhos internos atingido.\n");
    }
}

// Processa a mensagem recebida de um cliente TCP conectado
void process_client_message(int client_sock) {
    char buffer[MAX_BUFFER];
    ssize_t n = read(client_sock, buffer, MAX_BUFFER - 1);
    if (n > 0) {
        buffer[n] = '\0';
        printf("Mensagem TCP recebida: %s", buffer);
        char command[16], ip[INET_ADDRSTRLEN];
        int port;
        if (sscanf(buffer, "%s %s %d", command, ip, &port) == 3) {
            if (strcmp(command, "ENTRY") == 0) {
                add_internal_neighbor(ip, port);
            } else if (strcmp(command, "SAFE") == 0) {
                strcpy(safeguardNeighbor.ip, ip);
                safeguardNeighbor.port = port;
                printf("Atualizado vizinho de salvaguarda: %s:%d\n", ip, port);
            } else {
                printf("Comando TCP desconhecido: %s\n", command);
            }
        } else {
            printf("Formato de mensagem TCP inválido.\n");
        }
    } else if (n == 0) {
        // Conexão fechada pelo cliente
    } else {
        perror("Erro na leitura do socket do cliente");
    }
    close(client_sock);
}

// Função para realizar o direct join (comando "dj" ou "j")
// Se connectIP for "0.0.0.0", cria a rede com apenas este nó
void direct_join(const char *net, const char *connectIP, int connectPort) {
    // Se connectIP for "0.0.0.0", cria rede com o nó próprio
    if (strcmp(connectIP, "0.0.0.0") == 0) {
        printf("Criando rede com este nó (primeiro nó).\n");
        strcpy(externalNeighbor.ip, myIP);
        externalNeighbor.port = myPort;
        strcpy(safeguardNeighbor.ip, myIP);
        safeguardNeighbor.port = myPort;
        return;
    }

    // Caso contrário, conecta-se ao nó indicado via TCP
    int sockfd;
    struct sockaddr_in serv_addr;
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("Erro ao criar socket para direct join");
        return;
    }
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(connectPort);
    serv_addr.sin_addr.s_addr = inet_addr(connectIP);
    if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Erro ao conectar para direct join");
        close(sockfd);
        return;
    }
    char message[MAX_BUFFER];
    snprintf(message, sizeof(message), "ENTRY %s %d\n", myIP, myPort);
    write(sockfd, message, strlen(message));
    printf("Enviado ENTRY para %s:%d\n", connectIP, connectPort);
    // Atualiza o vizinho externo deste nó
    strcpy(externalNeighbor.ip, connectIP);
    externalNeighbor.port = connectPort;
    close(sockfd);
}

// Função para enviar mensagem de registro via UDP
int perform_registration(const char *net) {
    char reg_msg[MAX_BUFFER];
    snprintf(reg_msg, sizeof(reg_msg), "REG %s %s %s", net, myIP, myTCP);
    if(sendto(udp_sock, reg_msg, strlen(reg_msg), 0,
              (struct sockaddr*)&server_addr, server_addr_len) < 0) {
        perror("Erro no sendto REG");
        return -1;
    }
    printf("Enviado REG via UDP: %s\n", reg_msg);
    return 0;
}

// Função para enviar comando de join via UDP
int perform_join(const char *net) {
    char msg[MAX_BUFFER];
    snprintf(msg, sizeof(msg), "NODES %s", net);
    if(sendto(udp_sock, msg, strlen(msg), 0,
              (struct sockaddr*)&server_addr, server_addr_len) < 0) {
        perror("Erro no sendto NODES");
        return -1;
    }
    printf("Enviado NODES via UDP: %s\n", msg);
    return 0;
}

int main(int argc, char *argv[]) {
    // Uso: ./ndn cache IP TCP regIP regUDP
    if (argc < 6) {
        fprintf(stderr, "Uso: %s cache IP TCP regIP regUDP\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    int cache_size = atoi(argv[1]);
    strcpy(myIP, argv[2]);
    strcpy(myTCP, argv[3]);          // armazena a porta TCP como string
    myPort = atoi(argv[3]);          // converte para inteiro para operações locais
    const char *regIP = argv[4];
    const char *regUDP = argv[5];

    printf("Iniciando nó NDN: %s:%d, cache=%d, reg server %s:%s\n",
           myIP, myPort, cache_size, regIP, regUDP);

    // Configuração do socket do servidor TCP
    int server_sock;
    struct sockaddr_in serv_addr;
    server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0) {
        perror("Erro ao criar socket TCP do servidor");
        exit(EXIT_FAILURE);
    }
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = inet_addr(myIP); // ou use INADDR_ANY se preferir
    serv_addr.sin_port = htons(myPort);
    if (bind(server_sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Erro no bind TCP do servidor");
        exit(EXIT_FAILURE);
    }
    if (listen(server_sock, 5) < 0) {
        perror("Erro no listen TCP do servidor");
        exit(EXIT_FAILURE);
    }
    printf("Servidor TCP escutando em %s:%d\n", myIP, myPort);

    // Configuração do socket UDP para comunicação com o servidor de nós
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
        perror("Erro ao criar socket UDP");
        exit(EXIT_FAILURE);
    }
    memcpy(&server_addr, res->ai_addr, res->ai_addrlen);
    server_addr_len = res->ai_addrlen;
    freeaddrinfo(res);
    printf("Socket UDP configurado para o servidor %s:%s\n", regIP, regUDP);

    // Array para armazenar sockets de clientes TCP conectados
    int client_socks[MAX_CLIENTS];
    for (int i = 0; i < MAX_CLIENTS; i++) {
        client_socks[i] = -1;
    }

    // Loop principal: multiplexa STDIN, o socket do servidor TCP, os sockets dos clientes e o socket UDP
    while (1) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        // Adiciona STDIN
        FD_SET(STDIN_FILENO, &read_fds);
        int max_fd = STDIN_FILENO;
        // Adiciona o socket do servidor TCP
        FD_SET(server_sock, &read_fds);
        if (server_sock > max_fd)
            max_fd = server_sock;
        // Adiciona os sockets dos clientes TCP
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (client_socks[i] != -1) {
                FD_SET(client_socks[i], &read_fds);
                if (client_socks[i] > max_fd)
                    max_fd = client_socks[i];
            }
        }
        // Adiciona o socket UDP
        FD_SET(udp_sock, &read_fds);
        if (udp_sock > max_fd)
            max_fd = udp_sock;

        int activity = select(max_fd + 1, &read_fds, NULL, NULL, NULL);
        if (activity < 0) {
            if (errno == EINTR)
                continue;
            perror("Erro no select");
            break;
        }

        // Processa comandos do usuário via STDIN
        if (FD_ISSET(STDIN_FILENO, &read_fds)) {
            char input[MAX_BUFFER];
            if (fgets(input, MAX_BUFFER, stdin) != NULL) {
                input[strcspn(input, "\n")] = 0;
                // Comando direct join: dj net connectIP connectTCP
                if (strncmp(input, "dj", 2) == 0) {
                    char cmd[10], net[16], connectIP[INET_ADDRSTRLEN];
                    int connectPort;
                    if (sscanf(input, "%s %s %s %d", cmd, net, connectIP, &connectPort) == 4) {
                        direct_join(net, connectIP, connectPort);
                        // Após direct join, regista via UDP
                        perform_registration(net);
                    } else {
                        printf("Formato inválido para dj. Uso: dj net connectIP connectTCP\n");
                    }
                }
                // Comando join: j net
                else if (strncmp(input, "j", 1) == 0) {
                    char cmd[10], net[16];
                    if (sscanf(input, "%s %s", cmd, net) == 2) {
                        // Simulação: solicita os dados de conexão via STDIN
                        char connectIP[INET_ADDRSTRLEN];
                        char portStr[10];
                        int connectPort;
                        printf("Informe connectIP (ou 0.0.0.0 para criar rede): ");
                        if(fgets(connectIP, sizeof(connectIP), stdin)==NULL) continue;
                        connectIP[strcspn(connectIP, "\n")] = 0;
                        if(strcmp(connectIP, "0.0.0.0") != 0) {
                            printf("Informe connectPort: ");
                            if(fgets(portStr, sizeof(portStr), stdin)==NULL) continue;
                            connectPort = atoi(portStr);
                        } else {
                            connectPort = 0;
                        }
                        direct_join(net, connectIP, connectPort);
                        // Envia NODES via UDP para obter lista de nós e registrar
                        perform_join(net);
                        perform_registration(net);
                    } else {
                        printf("Formato inválido para join. Uso: j net\n");
                    }
                }
                // Comando para mostrar a topologia: st
                else if (strncmp(input, "st", 2) == 0) {
                    show_topology();
                }
                // Comando para sair: x
                else if (strncmp(input, "x", 1) == 0) {
                    printf("Saindo...\n");
                    break;
                }
                else {
                    printf("Comando não reconhecido.\n");
                }
            }
        }

        // Processa novas conexões no socket do servidor TCP
        if (FD_ISSET(server_sock, &read_fds)) {
            struct sockaddr_in cli_addr;
            socklen_t cli_len = sizeof(cli_addr);
            int new_sock = accept(server_sock, (struct sockaddr *)&cli_addr, &cli_len);
            if (new_sock < 0) {
                perror("Erro no accept");
            } else {
                // Adiciona o novo socket ao array de clientes
                int adicionado = 0;
                for (int i = 0; i < MAX_CLIENTS; i++) {
                    if (client_socks[i] == -1) {
                        client_socks[i] = new_sock;
                        adicionado = 1;
                        break;
                    }
                }
                if (!adicionado) {
                    printf("Número máximo de conexões atingido. Fechando nova conexão.\n");
                    close(new_sock);
                }
            }
        }

        // Processa mensagens UDP (ex: respostas do servidor de nós)
        if (FD_ISSET(udp_sock, &read_fds)) {
            char udp_buffer[MAX_BUFFER];
            ssize_t n = recvfrom(udp_sock, udp_buffer, MAX_BUFFER - 1, 0, NULL, NULL);
            if (n > 0) {
                udp_buffer[n] = '\0';
                printf("Mensagem UDP recebida: %s\n", udp_buffer);
                // Aqui você pode implementar o tratamento de OKREG, NODESLIST, etc.
            }
        }

        // Processa dados dos sockets de clientes TCP
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (client_socks[i] != -1 && FD_ISSET(client_socks[i], &read_fds)) {
                process_client_message(client_socks[i]);
                client_socks[i] = -1; // marca o socket como fechado
            }
        }
    }

    close(server_sock);
    close(udp_sock);
    return 0;
}
