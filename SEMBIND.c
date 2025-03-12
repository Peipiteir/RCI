#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <arpa/inet.h>
#include <netdb.h>

#define MAX_BUF 256

// Estrutura para guardar a topologia do nó
typedef struct {
    char id[64];                   // Identificador do nó (IP:TCP)
    char vizinho_externo[64];      // Nó com o qual se ligou (vizinho externo)
    char vizinho_salvaguarda[64];   // Nó de salvaguarda (obtido via SAFE)
    char vizinhos_internos[10][64]; // Lista de nós internos (até 10, para exemplo)
    int num_vizinhos;
} Topologia;

Topologia topo;

int udp_sock;  // Socket UDP para comunicação com o servidor
struct sockaddr_storage server_addr;
socklen_t server_addr_len;

int tcp_sock = -1; // Socket TCP ativo (usado no direct join, quando aberto)

/* 
 * Função: perform_registration
 * Envia a mensagem "REG net IP TCP" via UDP para o servidor e aguarda o OKREG.
 */
int perform_registration(const char *net, const char *own_ip, const char *own_tcp) {
    char reg_msg[MAX_BUF];
    snprintf(reg_msg, sizeof(reg_msg), "REG %s %s %s", net, own_ip, own_tcp);
    if(sendto(udp_sock, reg_msg, strlen(reg_msg), 0,
              (struct sockaddr*)&server_addr, server_addr_len) < 0) {
        perror("sendto REG");
        return -1;
    }
    printf("Enviado REG: %s\n", reg_msg);
    return 0;
}

/* 
 * Função: perform_direct_join
 * Realiza o direct join: se o connectIP for "0.0.0.0", cria a rede com nó próprio.
 * Caso contrário, cria uma ligação TCP com o nó indicado, envia a mensagem ENTRY e
 * aguarda (via select) pela mensagem SAFE, que atualiza o vizinho de salvaguarda.
 */
int perform_direct_join(const char *net, const char *connectIP, const char *connectTCP,
                          const char *own_ip, const char *own_tcp) {
    printf("Direct join: conectando a %s:%s na rede %s\n", connectIP, connectTCP, net);
    // Se connectIP for "0.0.0.0", a rede é criada com o nó próprio.
    if(strcmp(connectIP, "0.0.0.0") == 0) {
        strncpy(topo.vizinho_externo, topo.id, sizeof(topo.vizinho_externo));
        strncpy(topo.vizinho_salvaguarda, topo.id, sizeof(topo.vizinho_salvaguarda));
        printf("Rede criada com nó próprio.\n");
        return 0;
    }

    // Cria socket TCP e conecta-se a connectIP:connectTCP
    struct addrinfo hints, *res, *p;
    int sockfd;
    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%s", connectTCP);
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    int err = getaddrinfo(connectIP, port_str, &hints, &res);
    if(err != 0) {
        fprintf(stderr, "getaddrinfo TCP: %s\n", gai_strerror(err));
        return -1;
    }
    for(p = res; p != NULL; p = p->ai_next) {
        sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if(sockfd == -1)
            continue;
        if(connect(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            close(sockfd);
            continue;
        }
        break;
    }
    if(p == NULL) {
        fprintf(stderr, "Erro: não foi possível conectar a %s:%s\n", connectIP, connectTCP);
        freeaddrinfo(res);
        return -1;
    }
    freeaddrinfo(res);
    tcp_sock = sockfd; // Guarda o socket TCP para uso no select

    // Envia a mensagem ENTRY: "ENTRY own_ip own_tcp\n"
    char entry_msg[MAX_BUF];
    snprintf(entry_msg, sizeof(entry_msg), "ENTRY %s %s\n", own_ip, own_tcp);
    if(write(tcp_sock, entry_msg, strlen(entry_msg)) < 0) {
        perror("write ENTRY");
        close(tcp_sock);
        tcp_sock = -1;
        return -1;
    }
    printf("Enviado ENTRY: %s", entry_msg);

    // Atualiza a topologia: define o vizinho externo e adiciona-o aos internos
    snprintf(topo.vizinho_externo, sizeof(topo.vizinho_externo), "%s:%s", connectIP, connectTCP);
    if(topo.num_vizinhos < 10) {
        snprintf(topo.vizinhos_internos[topo.num_vizinhos], sizeof(topo.vizinhos_internos[topo.num_vizinhos]),
                 "%s:%s", connectIP, connectTCP);
        topo.num_vizinhos++;
    }
    // A receção da mensagem SAFE será tratada no loop principal (via select) no socket tcp_sock
    return 0;
}

/* 
 * Função: perform_join
 * Envia a mensagem "NODES net" via UDP para o servidor. A resposta (NODESLIST)
 * será processada no loop principal quando chegar pelo socket UDP.
 */
int perform_join(const char *net, const char *own_ip, const char *own_tcp) {
    char msg[MAX_BUF];
    snprintf(msg, sizeof(msg), "NODES %s", net);
    if(sendto(udp_sock, msg, strlen(msg), 0,
              (struct sockaddr*)&server_addr, server_addr_len) < 0) {
        perror("sendto NODES");
        return -1;
    }
    printf("Enviado NODES: %s\n", msg);
    // A resposta do servidor será processada na parte de UDP do select.
    return 0;
}

/* 
 * Função: show_topology
 * Exibe a topologia atual do nó.
 */
void show_topology() {
    printf("----- Topologia -----\n");
    printf("ID: %s\n", topo.id);
    printf("Vizinho Externo: %s\n", topo.vizinho_externo);
    printf("Vizinho de Salvaguarda: %s\n", topo.vizinho_salvaguarda);
    printf("Vizinhos Internos (%d):\n", topo.num_vizinhos);
    for (int i = 0; i < topo.num_vizinhos; i++){
        printf("  %s\n", topo.vizinhos_internos[i]);
    }
    printf("----------------------\n");
}

int main(int argc, char *argv[]) {
    if(argc < 6) {
        fprintf(stderr, "Uso: %s cache IP TCP regIP regUDP\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    // Parâmetros de linha de comando
    const char *cache_size = argv[1]; // tamanho da cache (não utilizado neste exemplo)
    const char *own_ip = argv[2];
    const char *own_tcp = argv[3];
    const char *regIP = argv[4];
    const char *regUDP = argv[5];

    // Inicializa a topologia: o nó inicia com ele próprio como vizinho externo e salvaguarda.
    memset(&topo, 0, sizeof(topo));
    snprintf(topo.id, sizeof(topo.id), "%s:%s", own_ip, own_tcp);
    strncpy(topo.vizinho_externo, topo.id, sizeof(topo.vizinho_externo));
    strncpy(topo.vizinho_salvaguarda, topo.id, sizeof(topo.vizinho_salvaguarda));
    topo.num_vizinhos = 0;

    // Configura o socket UDP para comunicação com o servidor de nós
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;      // IPv4
    hints.ai_socktype = SOCK_DGRAM; // UDP
    int err = getaddrinfo(regIP, regUDP, &hints, &res);
    if(err != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(err));
        exit(EXIT_FAILURE);
    }
    udp_sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if(udp_sock < 0) {
        perror("socket UDP");
        exit(EXIT_FAILURE);
    }
    // Guarda a informação do servidor
    memcpy(&server_addr, res->ai_addr, res->ai_addrlen);
    server_addr_len = res->ai_addrlen;
    freeaddrinfo(res);

    // Loop principal com multiplexação usando select()
    char input_line[256];
    fd_set read_fds;
    int max_fd;
    while(1) {
        FD_ZERO(&read_fds);
        FD_SET(STDIN_FILENO, &read_fds);
        FD_SET(udp_sock, &read_fds);
        max_fd = udp_sock;
        if(tcp_sock != -1) {
            FD_SET(tcp_sock, &read_fds);
            if(tcp_sock > max_fd)
                max_fd = tcp_sock;
        }
        int activity = select(max_fd+1, &read_fds, NULL, NULL, NULL);
        if(activity < 0) {
            perror("select");
            break;
        }
        // Verifica entrada do utilizador (STDIN)
        if(FD_ISSET(STDIN_FILENO, &read_fds)) {
            if(fgets(input_line, sizeof(input_line), stdin) == NULL)
                break;
            input_line[strcspn(input_line, "\n")] = 0; // remove \n

            if(strcmp(input_line, "exit") == 0) {
                break;
            } else if(strncmp(input_line, "show topology", 13) == 0) {
                show_topology();
            } // Processamento do comando "direct join":
            else if(strncmp(input_line, "direct join", 11) == 0) {
                // Formato esperado: direct join net connectIP connectTCP
                char cmd1[16], cmd2[16], net[16], connectIP[64], connectTCP[16];
                if(sscanf(input_line, "%s %s %s %s %s", cmd1, cmd2, net, connectIP, connectTCP) == 5) {
                    perform_direct_join(net, connectIP, connectTCP, own_ip, own_tcp);
                    // Após o direct join, regista o nó no servidor via UDP
                    perform_registration(net, own_ip, own_tcp);
                } else {
                    printf("Comando inválido. Uso: direct join net connectIP connectTCP\n");
                }
            }
            
            else if(strncmp(input_line, "join", 4) == 0) {
                // Formato: join net
                char cmd[16], net[16];
                if(sscanf(input_line, "%s %s", cmd, net) == 2) {
                    perform_join(net, own_ip, own_tcp);
                    // Após o join, o nó regista-se no servidor
                    perform_registration(net, own_ip, own_tcp);
                } else {
                    printf("Comando inválido. Uso: join net\n");
                }
            }
            else {
                printf("Comando desconhecido: %s\n", input_line);
            }
        } // Fim do processamento de STDIN

        // Verifica se há mensagens UDP (respostas do servidor)
        if(FD_ISSET(udp_sock, &read_fds)) {
            char udp_buf[256];
            int n = recvfrom(udp_sock, udp_buf, sizeof(udp_buf)-1, 0, NULL, NULL);
            if(n > 0) {
                udp_buf[n] = '\0';
                // Se a mensagem for OKREG, informa o utilizador
                if(strncmp(udp_buf, "OKREG", 5) == 0) {
                    printf("Registro confirmado pelo servidor: %s\n", udp_buf);
                }
                // Se a mensagem for NODESLIST, faz o join (escolhe o primeiro nó)
                else if(strncmp(udp_buf, "NODESLIST", 9) == 0) {
                    char *saveptr;
                    char *line = strtok_r(udp_buf, "\n", &saveptr); // "NODESLIST net"
                    line = strtok_r(NULL, "\n", &saveptr); // primeiro nó da lista
                    if(line) {
                        char chosenIP[64], chosenTCP[16];
                        if(sscanf(line, "%s %s", chosenIP, chosenTCP) == 2) {
                            printf("Join: conectando ao nó %s:%s\n", chosenIP, chosenTCP);
                            perform_direct_join(saveptr, chosenIP, chosenTCP, own_ip, own_tcp);
                        } else {
                            printf("Resposta do servidor mal formatada.\n");
                        }
                    } else {
                        // Se a lista estiver vazia, o nó cria a rede consigo próprio.
                        printf("Rede vazia. Criando rede com nó próprio.\n");
                        strncpy(topo.vizinho_externo, topo.id, sizeof(topo.vizinho_externo));
                        strncpy(topo.vizinho_salvaguarda, topo.id, sizeof(topo.vizinho_salvaguarda));
                    }
                } else {
                    printf("Mensagem UDP recebida: %s\n", udp_buf);
                }
            }
        }

        // Verifica se há dados no socket TCP (aguardando a mensagem SAFE)
        if(tcp_sock != -1 && FD_ISSET(tcp_sock, &read_fds)) {
            char tcp_buf[256];
            int n = read(tcp_sock, tcp_buf, sizeof(tcp_buf)-1);
            if(n > 0) {
                tcp_buf[n] = '\0';
                // Espera a mensagem SAFE no formato: "SAFE ip tcp\n"
                if(strncmp(tcp_buf, "SAFE", 4) == 0) {
                    char safe_cmd[16], safe_ip[64], safe_tcp[16];
                    if(sscanf(tcp_buf, "%s %s %s", safe_cmd, safe_ip, safe_tcp) == 3) {
                        snprintf(topo.vizinho_salvaguarda, sizeof(topo.vizinho_salvaguarda),
                                 "%s:%s", safe_ip, safe_tcp);
                        printf("Recebido SAFE: novo vizinho de salvaguarda: %s:%s\n", safe_ip, safe_tcp);
                    } else {
                        printf("Mensagem SAFE mal formatada: %s\n", tcp_buf);
                    }
                } else {
                    printf("Mensagem TCP recebida: %s\n", tcp_buf);
                }
                close(tcp_sock);
                tcp_sock = -1;
            }
            else if(n == 0) {
                // conexão fechada
                close(tcp_sock);
                tcp_sock = -1;
            }
        }
    } // Fim do loop principal

    close(udp_sock);
    if(tcp_sock != -1)
        close(tcp_sock);
    return 0;
}
