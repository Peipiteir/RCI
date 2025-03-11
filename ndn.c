#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// join (j) net 
// Entrada do nó na rede net. Os valores de net são representados por três dígitos, 
// podendo variar entre 000 e 999.
// void join(){}

// // direct join (dj) net connectIP connectTCP 
// // Entrada do nó na rede net, ligando-se ao nó com identificador connectIP  
// // connectTCP, sem registo no servidor de nós. Se connectIP  for 0.0.0.0, 
// // então a rede é criada com apenas o nó.
// void direct_join(){}

// // create (c) name 
// // Criação de um objeto com nome name. Os valores de name são representados por 
// // sequências alfanuméricas com um máximo de 100 carateres. Para simplificar, 
// // cria-se apenas o nome do objeto, omitindo-se o objeto propriamente dito. 
// void create(){}

// // delete (dl) name 
// // Remoção do objeto com nome name.
// void delete(){}

// // retrieve (r) name  
// // Pesquisa do objeto com nome name.
// void retrieve(){}

// // show topology (st) 
// // Visualização dos identificadores dos vizinhos externo, de salvaguarda e internos.  
// void show_topology(){}

// // show names (sn) 
// // Visualização dos nomes de todos os objetos guardados no nó. 
// void show_names(){}

// // show interest table (si) 
// // Visualização de todas as entradas da tabela de interesses pendentes.   
// void show_interest_table(){}

// // leave (l) 
// // Saída do nó da rede. 
// void leave(){}

// // exit (x) 
// // Fecho da aplicação. 
// void exit(){}


//invocada com comando ./ndn cache IP TCP regIP regUDP
int main(int argc, char *argv[]){

    if(argc < 6) exit(0);
    /**cache: tamanho de cache
     * IP: endereco da maquina que aloja a aplicacao
     * TCP: porto TCP de escuta
     * regIP e regUDP: endereco IP e porto UDP fornecido (193.136.138.142 e 59000)
    */
    int cache, TCP, regUDP;
    char IP[16], regIP[16];

    cache = atoi(argv[1]);
    strcpy(IP, argv[2]);
    TCP = atoi(argv[3]);
    strcpy(regIP, argv[4]);
    regUDP = atoi(argv[5]);

    printf("%d\n", argc);
    return 0;
}