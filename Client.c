#include<stdio.h>
#include<stdlib.h>
#include<sys/socket.h>
#include<sys/types.h>
#include<netinet/in.h>
#include<strings.h>
#include<unistd.h>
#include<sys/stat.h>
#include<netdb.h>
#include<ctype.h>
#include<string.h>
#include<fcntl.h>
#include<sys/wait.h>
#include<signal.h>


#define buffer_size 1024
#define max_clients 5


int childpid;
int sockfd;
char send_buffer[buffer_size];


void error(char *msg)
{
  perror(msg);
  exit(0);
}

void sigCHandler(int sig)
{
    fflush(stdout);
    bzero(send_buffer, buffer_size);
    sprintf(send_buffer, "/quit");
    int n = send(sockfd, send_buffer, sizeof(send_buffer), 0);
    kill(childpid, SIGKILL);
}


void sigZhandler()
{
    fflush(stdout);
    bzero(send_buffer, buffer_size);
    sprintf(send_buffer, "/quit");
    int n = send(sockfd, send_buffer, sizeof(send_buffer), 0);
    kill(childpid, SIGKILL);
}


int uid[max_clients];
int flag = 0;

int main(int argc,char *argv[]){
    
    signal(SIGINT, sigCHandler);        
    signal(SIGTSTP, sigZhandler); 
    int portno;
    portno = atoi(argv[1]);
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    struct hostent *server;
    server = gethostbyname("127.0.0.1");
    if (server == NULL){
    fprintf(stderr,"ERROR, no such host");
    exit(0);
    }
    struct sockaddr_in serv_addr;
    bzero((char *) &serv_addr, sizeof(serv_addr)); // initializes buffer
    serv_addr.sin_family = AF_INET; // for IPv4 family
    bcopy((char *)server-> h_addr , (char *) &serv_addr.sin_addr.s_addr, server->h_length);
    serv_addr.sin_port = htons(portno); //defining port number
    if (connect(sockfd,(struct sockaddr *) &serv_addr,sizeof(serv_addr)) < 0)
    {
         error("ERROR connecting");
    }
    char buffer[buffer_size];
    bzero(buffer, buffer_size);
    int n;
    if((childpid = fork()) == 0){
        while(1){
            bzero(send_buffer, buffer_size);
            fgets(send_buffer, buffer_size, stdin);
            n = send(sockfd, send_buffer, sizeof(send_buffer), 0);
            if(strncmp(buffer, "/quit", 5) == 0){
                flag = 1;
                break;
            }
        }
        while(wait(NULL)>0);
    }else{
        while(1){
            if(sockfd <= 0) break;
            if(flag == 1) break;
            n = recv(sockfd, buffer, sizeof(buffer), 0);
            if(n < 0){
                printf("message not received");
            }else{
                if(strncmp(buffer, "active_clients", 14) == 0){
                    int index = 0;
                    for(int i=0;i<max_clients;i++) uid[i] = 0;
                    printf("Active clients are : \n");
                    while(1){
                        bzero(buffer, buffer_size);
                        n = recv(sockfd, buffer, sizeof(buffer), 0);
                        if(strncmp(buffer, "active_clients_end", 18) == 0){
                            break;
                        }
                        printf("%s\n", buffer);
                        int temp = atoi(buffer);
                        uid[index] = temp;
                        index++;
                    }
                }
                else if(strncmp(buffer, "active_groups_start", 19) == 0){
                    printf("Server : Groups which you are part of are : \n");
                    while(1){
                        bzero(buffer, buffer_size);
                        n = recv(sockfd, buffer, sizeof(buffer), 0);
                        if(strncmp(buffer, "active_groups_end", 17) == 0){
                            break;
                        }
                        printf("%s\n", buffer);
                    }
                }
                else if(strncmp(buffer, "quit", 4) == 0){
                    bzero(buffer, buffer_size);
                    printf("Exiting system\n");
                    break;
                }
                else{
                    printf("%s\n", buffer);
                    bzero(buffer, buffer_size);
                }
            }
        }
    }
    close(sockfd);
    exit(0);
    return 0;
}
