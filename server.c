
/************************************************************************/
/*   PROGRAM NAME: server.c  (works with client.c)                      */
/*                                                                      */
/*   Server creates a socket to listen for the connection from up to 10 */
/*   clients. When the communication is established, Server echoes data */
/*   from Client to all other connected clients. Furthermore, Server    */
/*   echoes a message to all clients whenver a new client connects or a */
/*   current client disconnects.                                        */
/*                                                                      */
/*   To run this program, first compile the server.c and run it         */
/*   on a server machine. Then run the client program on another        */
/*   machine.                                                           */
/*                                                                      */
/*   LINUX:      gcc -o server  server.c -lpthread -lnsl                */
/*                                                                      */
/************************************************************************/


//
//  server.cpp
//  CS 3800 Assignment 3
//  Mike Fanger and Joel Bierbaum
//


#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>  /* define socket */
#include <netinet/in.h>  /* define internet socket */
#include <netdb.h>       /* define internet socket */
#include <pthread.h>     /* POSIX threads */
#include <signal.h>
#include <string.h>
#include <stdbool.h>
#include <arpa/inet.h>

#define SERVER_PORT 9999
#define MAX_CLIENTS 10
#define MAX_MESSAGE 512

int srv_sock;
int clients[MAX_CLIENTS];
unsigned int client_count = 0;
bool done = false;
pthread_mutex_t count_lock;
pthread_mutex_t array_lock;

void* clientHandler(void* arg);
int findEmptySlot(int clients[MAX_CLIENTS]);
void emitMessage(char message[MAX_MESSAGE], size_t bytes, int sender_sock, int clients[MAX_CLIENTS]);
void emitMessageAll(char message[MAX_MESSAGE], size_t bytes, int clients[MAX_CLIENTS]);
void closeSockets(int clients[MAX_CLIENTS]);
void closeServer();
void* delayHandler(void* arg);

int main(int argc, const char * argv[]) {
    
    // Prevent stdout from buffering
    setbuf(stdout, NULL);
    
    // Stores socket descriptors for newly accepted clients
    int clt_sock;
    
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    
    struct sockaddr_in client_addr = { AF_INET };
    unsigned int client_len = sizeof(client_addr);
    
    
    /* Initialize clients array */
    int i;
    for(i = 0; i < MAX_CLIENTS; i++)
    {
        clients[i] = -1;
    }
    
    /* Create server socket */
    if( (srv_sock = socket(AF_INET, SOCK_STREAM, 0) ) == -1 )
    {
        perror("SERVER: socket failed\n");
        exit(1);
    }
    
    /* Bind the socket to an internet port */
    if( bind(srv_sock, (struct sockaddr*) &server_addr, sizeof(server_addr)) == -1 )
    {
        perror("SERVER: bind failed\n");
        exit(1);
    }
    
    /* Listen for clients */
    if( listen(srv_sock, 10) == -1 )
    {
        perror("SERVER: listen failed\n");
        exit(1);
    }
    
    printf("SERVER is listening for clients to establish a connection\n");
    
    /* Set handler for closing server */
    signal(SIGINT, closeServer);
    
    /* Wait for client connection request */
    while(true)
    {
        
        if( (clt_sock = accept(srv_sock, (struct sockaddr*)&client_addr, &client_len)) == -1 )
        {
            perror("server: accept failed\n");
            exit(1);
            break;
        }
        
        pthread_mutex_lock(&count_lock);
        if(client_count >= MAX_CLIENTS)
        {
            pthread_mutex_unlock(&count_lock);
            
            char buf[] = "/server_full";
            write(clt_sock, buf, sizeof(buf));
            close(clt_sock);
            continue;
        }
        
        client_count++;
        pthread_mutex_unlock(&count_lock);
        
        pthread_t tid;
        pthread_create(&tid, NULL, clientHandler, &clt_sock);
    }
    
    return 0;
}



void* clientHandler(void* arg)
{
    int clt_sock = *(int*)arg;
    char buf[MAX_MESSAGE]; bzero(buf, sizeof(buf));
    long bytes_read;
    char client_name[MAX_MESSAGE]; bzero(client_name, sizeof(buf));
    
    /* Insert new client into clients array */
    int idx = findEmptySlot(clients);
    pthread_mutex_lock(&array_lock);
    clients[idx] = clt_sock;
    pthread_mutex_unlock(&array_lock);
    
    /* Read client name */
    bytes_read = read(clt_sock, buf, sizeof(buf));
    char greeting[] = " connected to the server";
    strcpy(client_name, buf);
    strcat(buf, greeting);
    printf("%s\n", buf);
    
    /* Inform all clients of the connected user */
    int i;
    pthread_mutex_lock(&array_lock);
    for(i = 0; i < MAX_CLIENTS; i++)
    {
        if(clients[i] == -1) continue;
    
        write(clients[i], buf, strlen(buf));
    }
    pthread_mutex_unlock(&array_lock);
    
    bzero(buf, sizeof(buf));
    
    /* Read messages from the client and emit to all other connect clients */
    while( (bytes_read = read(clt_sock, buf, sizeof(buf))) != 0)
    {
        if(strcmp(buf,"/exit")==0 || strcmp(buf,"/quit")==0 || strcmp(buf,"/part")==0)
        {
            break;
        }
        
        printf("%s: %s\n", client_name, buf);
        
        char message[MAX_MESSAGE]; bzero(message, sizeof(message));
        strcpy(message, client_name);
        strcat(message, ": ");
        strcat(message, buf);
        
        emitMessage(message, strlen(message), clt_sock, clients);
        bzero(buf, sizeof(buf));
    }
    
    /* Update client state variables and close socket */
    pthread_mutex_lock(&array_lock);
    clients[idx] = -1;
    pthread_mutex_unlock(&array_lock);
    
    pthread_mutex_lock(&count_lock);
    client_count--;
    pthread_mutex_unlock(&count_lock);
    
    close(clt_sock);
    
    /* Inform users that a user is exiting */
    char exit_message[MAX_MESSAGE]; bzero(exit_message, sizeof(exit_message));
    strcpy(exit_message, client_name);
    strcat(exit_message, " disconnected from the server");
    printf("%s\n", exit_message);
    
    if(bytes_read > 0)
    {
        emitMessage(exit_message, strlen(exit_message), clt_sock, clients);
    }
    
    return NULL;
}

// Returns index of an empty slot in the clients array.
// If array is full, returns -1
// THREAD SAFE
int findEmptySlot(int clients[MAX_CLIENTS])
{
    int idx = -1, i;
    bool done = false;
    
    for(i = 0; i < MAX_CLIENTS && !done; i++)
    {
        pthread_mutex_lock(&array_lock);
        if(clients[i] == -1)
        {
            idx = i;
            done = true;
        }
        pthread_mutex_unlock(&array_lock);
    }
    
    return idx;
}

// Sends message to all connected clients, except the client connected to sender_sock.
// THREAD SAFE
void emitMessage(char message[MAX_MESSAGE], size_t bytes, int sender_sock, int clients[MAX_CLIENTS])
{
    int i;
    pthread_mutex_lock(&array_lock);
    for(i = 0; i < MAX_CLIENTS; i++)
    {
        if(clients[i] == -1) continue;
        if(clients[i] == sender_sock) continue;
        
        int result = write(clients[i], message, bytes);
        if(result == -1)
        {
            printf("Write error from emitMessage");
        }
    }
    pthread_mutex_unlock(&array_lock);
}

// Sends message to all connected clients
// THREAD SAFE
void emitMessageAll(char message[MAX_MESSAGE], size_t bytes, int clients[MAX_CLIENTS])
{
    int i;
    pthread_mutex_lock(&array_lock);
    for(i = 0; i < MAX_CLIENTS; i++)
    {
        if(clients[i] == -1) continue;
        
        int result = write(clients[i], message, bytes);
        if(result == -1)
        {
            printf("Write error from emitMessageAll");
        }
    }
    pthread_mutex_unlock(&array_lock);
}

// Closes all currently open socket connections
// THREAD SAFE
void closeSockets(int clients[MAX_CLIENTS])
{
    int i;
    pthread_mutex_lock(&array_lock);
    for(i = 0; i < MAX_CLIENTS; i++)
    {
        if(clients[i] == -1) continue;
        close(clients[i]);
    }
    pthread_mutex_unlock(&array_lock);
}

// Handler for cleanly closing server
void closeServer()
{
    printf("\rServer will shut down in approximately 10 seconds\n");
    char message[] = "/server_closing";
    emitMessageAll(message, strlen(message), clients);
    sleep(10);
    
    closeSockets(clients);
    close(srv_sock);
    
    exit(0);
}



