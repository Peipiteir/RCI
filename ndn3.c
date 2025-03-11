#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/select.h>
#include <errno.h>

#define MAX_BUFFER 256
#define MAX_INTERNAL 10
#define MAX_CLIENTS 10

// Estrutura para armazenar vizinhos
typedef struct {
    char ip[INET_ADDRSTRLEN];
    int port;
} Neighbor;

// Variáveis globais que representam o estado do nó
Neighbor externalNeighbor = {"", 0};
Neighbor safeguardNeighbor = {"", 0};
Neighbor internalNeighbors[MAX_INTERNAL];
int numInternal = 0;

// Identificador do nó (IP e porta)
char myIP[INET_ADDRSTRLEN];
int myPort;

// Função para exibir a topologia atual
void show_topology() {
    printf("Topo Atual:\n");
    printf("  Vizinho Externo: %s:%d\n", externalNeighbor.ip, externalNeighbor.port);
    printf("  Vizinho de Salvaguarda: %s:%d\n", safeguardNeighbor.ip, safeguardNeighbor.port);
    printf("  Vizinhos Internos (%d):\n", numInternal);
    for (int i = 0; i < numInternal; i++) {
        printf("    %s:%d\n", internalNeighbors[i].ip, internalNeighbors[i].port);
    }
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

// Processa a mensagem recebida de um cliente conectado
void process_client_message(int client_sock) {
    char buffer[MAX_BUFFER];
    ssize_t n = read(client_sock, buffer, MAX_BUFFER - 1);
    if (n > 0) {
        buffer[n] = '\0';
        printf("Mensagem recebida: %s", buffer);
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
                printf("Comando desconhecido recebido: %s\n", command);
            }
        } else {
            printf("Formato de mensagem inválido.\n");
        }
    } else if (n == 0) {
        // Conexão fechada pelo cliente
    } else {
        perror("Erro na leitura do socket do cliente");
    }
    close(client_sock);
}

// Função para realizar o direct join (comando "dj")
// Formato: dj net connectIP connectTCP
void direct_join(const char *net, const char *connectIP, int connectPort) {
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
    sprintf(message, "ENTRY %s %d\n", myIP, myPort);
    write(sockfd, message, strlen(message));
    printf("Enviado ENTRY para %s:%d\n", connectIP, connectPort);
    // Atualiza o vizinho externo deste nó
    strcpy(externalNeighbor.ip, connectIP);
    externalNeighbor.port = connectPort;
    close(sockfd);
}

int main(int argc, char *argv[]) {
    // Uso: ./ndn cache IP TCP regIP regUDP
    if (argc < 6) {
        fprintf(stderr, "Uso: %s cache IP TCP regIP regUDP\n", argv[0]);
        exit(1);
    }
    int cache_size = atoi(argv[1]);
    strcpy(myIP, argv[2]);
    myPort = atoi(argv[3]);
    char regIP[INET_ADDRSTRLEN];
    strcpy(regIP, argv[4]);
    int regUDP = atoi(argv[5]);

    printf("Iniciando nó NDN: %s:%d, cache=%d, reg server %s:%d\n",
           myIP, myPort, cache_size, regIP, regUDP);

    // Configuração do socket do servidor TCP
    int server_sock;
    struct sockaddr_in serv_addr;
    server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0) {
        perror("Erro ao criar socket TCP do servidor");
        exit(1);
    }
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    // Pode usar INADDR_ANY ou myIP, conforme necessário
    serv_addr.sin_addr.s_addr = inet_addr(myIP);
    serv_addr.sin_port = htons(myPort);
    if (bind(server_sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Erro no bind TCP do servidor");
        exit(1);
    }
    if (listen(server_sock, 5) < 0) {
        perror("Erro no listen TCP do servidor");
        exit(1);
    }
    printf("Servidor TCP escutando em %s:%d\n", myIP, myPort);

    // Array para armazenar sockets de clientes conectados
    int client_socks[MAX_CLIENTS];
    for (int i = 0; i < MAX_CLIENTS; i++) {
        client_socks[i] = -1;
    }

    // Loop principal: multiplexa a entrada padrão, o socket do servidor e os sockets dos clientes
    while (1) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        // Adiciona STDIN
        FD_SET(STDIN_FILENO, &read_fds);
        int max_fd = STDIN_FILENO;
        // Adiciona o socket do servidor
        FD_SET(server_sock, &read_fds);
        if (server_sock > max_fd)
            max_fd = server_sock;
        // Adiciona os sockets dos clientes
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (client_socks[i] != -1) {
                FD_SET(client_socks[i], &read_fds);
                if (client_socks[i] > max_fd)
                    max_fd = client_socks[i];
            }
        }

        int activity = select(max_fd + 1, &read_fds, NULL, NULL, NULL);
        if (activity < 0) {
            if (errno == EINTR)
                continue;
            perror("Erro no select");
            break;
        }

        // Verifica entrada na STDIN (comandos do usuário)
        if (FD_ISSET(STDIN_FILENO, &read_fds)) {
            char input[MAX_BUFFER];
            if (fgets(input, MAX_BUFFER, stdin) != NULL) {
                input[strcspn(input, "\n")] = 0; // remove \n
                // Comando direct join: dj net connectIP connectTCP
                if (strncmp(input, "dj", 2) == 0) {
                    char cmd[10], net[10], connectIP[INET_ADDRSTRLEN];
                    int connectPort;
                    if (sscanf(input, "%s %s %s %d", cmd, net, connectIP, &connectPort) == 4) {
                        direct_join(net, connectIP, connectPort);
                    } else {
                        printf("Formato inválido para dj. Uso: dj net connectIP connectTCP\n");
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

        // Verifica se há novas conexões no socket do servidor
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

        // Verifica se há dados em algum socket de cliente
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (client_socks[i] != -1 && FD_ISSET(client_socks[i], &read_fds)) {
                process_client_message(client_socks[i]);
                client_socks[i] = -1; // Marca o socket como fechado
            }
        }
    }

    close(server_sock);
    return 0;
}
