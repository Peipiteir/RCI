#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <time.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#define MYPORT "58000"


int main(void)
{
    char in_str[128];

    fd_set inputs, testfds;
    struct timeval timeout;

//    int i,n;

    int out_fds,errcode, ret;

    int max_fd;

    char prt_str[90];


// socket variables
    struct addrinfo hints, *res;
    struct sockaddr_in udp_useraddr;
    socklen_t addrlen;
    int ufd;

    char host[NI_MAXHOST], service[NI_MAXSERV];


// UDP SERVER SECTION
    memset(&hints,0,sizeof(hints));
    hints.ai_family=AF_INET;
    hints.ai_socktype=SOCK_DGRAM;
    hints.ai_flags=AI_PASSIVE|AI_NUMERICSERV;

    if((errcode=getaddrinfo(NULL,MYPORT,&hints,&res))!=0)
        exit(1);// On error

    ufd=socket(res->ai_family,res->ai_socktype,res->ai_protocol);
    if(ufd==-1)
        exit(1);

    if(bind(ufd,res->ai_addr,res->ai_addrlen)==-1)
    {
        sprintf(prt_str,"Bind error UDP server\n");
        write(1,prt_str,strlen(prt_str));
        exit(1);// On error
    }
    if(res!=NULL)
        freeaddrinfo(res);

    FD_ZERO(&inputs); // Clear input mask
    FD_SET(0,&inputs); // Set standard input channel on
    FD_SET(ufd,&inputs); // Set UDP channel on

//    printf("Size of fd_set: %d\n",sizeof(fd_set));
//    printf("Value of FD_SETSIZE: %d\n",FD_SETSIZE);

    max_fd=ufd;

    while(1)
    {
        testfds=inputs; // Reload mask
//        printf("testfds byte: %d\n",((char *)&testfds)[0]); // Debug
        memset((void *)&timeout,0,sizeof(timeout));
        timeout.tv_sec=10;

        out_fds=select(max_fd+1,&testfds,(fd_set *)NULL,(fd_set *)NULL,(struct timeval *) &timeout);
// testfds is now '1' at the positions that were activated by FD_SET
//        printf("testfds byte: %d\n",((char *)&testfds)[0]); // Debug

        switch(out_fds)
        {
            case 0:
                printf("\n ---------------Timeout event-----------------\n");
                break;
            case -1:
                perror("select");
                exit(1);
            default:
                if(FD_ISSET(0,&testfds))
                {
                    fgets(in_str,50,stdin);
                    printf("---Input at keyboard: %s\n",in_str);
                    if(!memcmp(in_str,"_STOP",5))
                    {
                        write(1,"Terminating\n",12);
                        exit(0);
                    }
                }
                if(FD_ISSET(ufd,&testfds))
                {
                    addrlen = sizeof(udp_useraddr);
                    ret=recvfrom(ufd,prt_str,80,0,(struct sockaddr *)&udp_useraddr,&addrlen);
                    if(ret>0)
                    {
                        if(strlen(prt_str)>0)
                            prt_str[ret-1]=0;
                        printf("---UDP socket: %s\n",prt_str);
                        errcode=getnameinfo( (struct sockaddr *) &udp_useraddr,addrlen,host,sizeof host, service,sizeof service,0);
                        if(errcode==0)
                            printf("       Sent by [%s:%s]\n",host,service);
                        if(!memcmp(prt_str,"_STOP",5))
                        {
                            write(1,"Terminating\n",12);
                            exit(0);
                        }
                    }
                }
        }
    }
}

