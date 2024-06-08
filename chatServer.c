#include <signal.h>
#include <malloc.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <time.h>
#include "chatServer.h"

static int end_server = 0;

void intHandler(int SIG_INT) {
	/* use a flag to end_server to break the main loop */
	end_server = 1;
    //printf("\n");
}

int main (int argc, char *argv[]){

	signal(SIGINT, intHandler);
   
	conn_pool_t* pool = malloc(sizeof(conn_pool_t));
    if (pool == NULL){
        perror("malloc");
        return 1;
    }
	if (initPool(pool)){
        free(pool);
        return 1;
    }
    if(argc != 2){
        free(pool);
        printf("Usage: server <port>");
        return 1;
    }
    int port = atoi(argv[1]);
    if(port >65535 || port <=0){
        free(pool);

        printf("Usage: server <port>");
        return 1;
    }
    int listen_socket=socket(AF_INET, SOCK_STREAM, 0);
    if(listen_socket== -1){
        perror("socket");
        exit(1);
    }
    int on=1;
    if(ioctl(listen_socket,FIONBIO,(char *)&on)< 0){
        perror("ioctl");
        free(pool);
        close(listen_socket);
        return 1;
    }	
	struct sockaddr_in server;
    memset(&server,0,sizeof(struct sockaddr_in));
    server.sin_family=AF_INET;
    server.sin_addr.s_addr=htonl(INADDR_ANY);
    server.sin_port=htons(port);
    if(bind(listen_socket,(struct sockaddr*)&server,sizeof(server)) < 0){
        perror("error: bind");
        free(pool);
        exit(1);
    }
    if(listen(listen_socket, 5) < 0){
        perror("listen");
        free(pool);
        close(listen_socket);
        return 1;
    }
    if(addConn(listen_socket,pool)){
        free(pool);
        close(listen_socket);
        return 1;
    }
    struct conn* listen_conn= pool->conn_head;
	do{
        pool->ready_read_set =pool->read_set;
		pool->ready_write_set=pool->write_set;
		printf("Waiting on select()...\nMaxFd %d\n", pool->maxfd);
		pool->nready=select(pool->maxfd+1, &pool->ready_read_set, &pool->ready_write_set,NULL,NULL);
		if(end_server==1){
            pool->nready=0;
            FD_ZERO(&pool->ready_read_set);
            FD_ZERO(&pool->ready_write_set);
        }
		size_t ready_des_counter=pool->nready;
		for(struct conn* cursor=pool->conn_head; cursor != NULL && ready_des_counter> 0;){
			size_t process_organizer=1;
			if (FD_ISSET(cursor->fd,&pool->ready_read_set)){
				ready_des_counter--;
				if(cursor->fd==listen_socket){
					int sd=accept(listen_socket,NULL,NULL);
					if (sd <0){
							return 1;
					}
					addConn(sd,pool);
                    printf("New incoming connection on sd %d\n", sd);
				}
				else{
					printf("Descriptor %d is readable\n", cursor->fd);
                    char response_buffer[BUFFER_SIZE];
					int bytesRead=BUFFER_SIZE;
                    int total_bytes=0;
					while(bytesRead ==BUFFER_SIZE){
                        memset(response_buffer,'\0',BUFFER_SIZE);
                        bytesRead =read(cursor->fd,response_buffer,BUFFER_SIZE);
                        if (bytesRead == 0 && process_organizer == 1) {
                            printf("Connection closed for sd %d\n", cursor->fd);
                            int current_fd = cursor->fd;
                            cursor = cursor->next;  // Advance cursor before removing the connection
                            removeConn(current_fd, pool);
                            process_organizer = 0;
                            break;
                        }
						else if(bytesRead>0){	
                            total_bytes += bytesRead;
                            addMsg(cursor->fd,response_buffer,bytesRead,pool);
                            FD_CLR(cursor->fd,&pool->write_set);
						}
						else{ // finish to read
            				break;
        				}
                        process_organizer=2;
					} 
					if(bytesRead > 0 && process_organizer!=0){
                        printf("%d bytes received from sd %d\n", total_bytes, cursor->fd);
                    }        
				}
			} 
            if (process_organizer!=0 ){
                if(FD_ISSET(cursor->fd,&pool->ready_write_set)){
                    ready_des_counter--;
                    writeToClient(cursor->fd,pool);
                }
                cursor=cursor->next;
            }
     	} /* End of loop through selectable descriptors */
		int flag=0;
        while (flag==0 && listen_conn->write_msg_head !=NULL){
            struct conn* cursor =pool->conn_head;
            while(cursor != NULL){
                if (cursor->write_msg_head != NULL && cursor !=listen_conn && cursor->write_msg_head->message==listen_conn->write_msg_head->message){
                    flag=1;
                    break;
                }
                cursor=cursor->next;
            }
            if(flag==0){
                msg_t* free_message=listen_conn->write_msg_head;
                listen_conn->write_msg_head=listen_conn->write_msg_head->next;
                free_message->next=NULL;
                free(free_message->message);
                free(free_message);
            }
        }
   } 
   while (end_server == 0);
	msg_t* delete_messages=listen_conn->write_msg_head;
    while(delete_messages !=NULL){
        free(delete_messages->message);
        delete_messages=delete_messages->next;
    }
    while(pool->conn_head->next !=NULL){
        removeConn(pool->conn_head->next->fd,pool);
    }
    removeConn(listen_conn->fd,pool);
    free(pool);
	return 0;
}
int initPool(conn_pool_t* pool) {
	if(pool== 0){
        perror("pool not initialized");
        return 1;
    }
	pool->maxfd=0;
    pool->nready=0;                   
	FD_ZERO(&pool->read_set);                
	FD_ZERO(&pool->ready_read_set);  
	FD_ZERO(&pool->write_set);               
	FD_ZERO(&pool->ready_write_set); 
	pool->conn_head=NULL;
	pool->nr_conns=0;
	return 0;
}
int addConn(int sd, conn_pool_t* pool) {
    if(pool ==NULL || sd <0){
        perror("pool not initialized");
        return 1;
    }
    struct conn* new_conn=(struct conn*)calloc(1,sizeof(struct conn));
    if(new_conn==NULL){
        perror("malloc");
        return 1;
    }
    new_conn->fd=sd;
    new_conn->write_msg_head=NULL;
    new_conn->write_msg_tail=NULL;
    new_conn->next = NULL;
    new_conn->prev = NULL;
    if(pool->conn_head==NULL){
        pool->conn_head=new_conn;
        new_conn->prev=NULL;
    }
    else{
        conn_t* cursor =pool->conn_head;
        while (cursor->next !=NULL){
            cursor=cursor->next;
        }
        cursor->next =new_conn;
        new_conn->prev=cursor;
    }
    if(pool->maxfd < sd){
        pool->maxfd=sd;
    }
    pool->nr_conns++;
    FD_SET(sd,&pool->read_set);
    return 0;
}

int removeConn(int sd, conn_pool_t* pool) {
    if (pool == NULL ||sd < 0){
        return 1;
    }
    if (pool->conn_head->next != NULL){
        printf("removing connection with sd %d \n", sd);
    }
    shutdown(sd, SHUT_RDWR);
    FD_CLR(sd, &pool->read_set);
    FD_CLR(sd, &pool->write_set);
    FD_CLR(sd, &pool->ready_read_set);
    FD_CLR(sd, &pool->ready_write_set);
    struct conn* del_conn = NULL;
    for(struct conn* it = pool->conn_head; it != NULL; it = it->next){
        if (it->fd == sd){
            del_conn = it;
            if (del_conn->fd != pool->conn_head->fd){
                del_conn->prev->next = del_conn->next;
            }
            else {
                pool->conn_head = pool->conn_head->next;
            }
            if (del_conn->next != NULL){
                del_conn->next->prev = del_conn->prev;
            }
            del_conn->prev = NULL;
            del_conn->next = NULL;
            break;
        }
    }
    if (del_conn == NULL){
        // socket not found
        return 1;
    }
    if(sd == pool->maxfd){
        pool->maxfd = 0;
        for(struct conn* it = pool->conn_head; it != NULL; it=it->next){
            if (it->fd > pool->maxfd){
                pool->maxfd = it->fd;
            }
        }
    }
    if (del_conn->write_msg_head != NULL){
        msg_t* it = del_conn->write_msg_head->next;
        for(; it->next != NULL; it=it->next){
            free(it->prev);
        }
        free(it);
    }
    pool->nr_conns--;
    close(del_conn->fd);
    free(del_conn);
    return 0;
}

int addMsg(int sd,char* buffer,int len,conn_pool_t* pool) {
    if(pool == NULL || buffer == NULL || sd < 0 || len <= 0){
        return 1;
    }
    char* message =(char*)calloc(len + 1, sizeof(char));
    if(message == NULL){
        // malloc
        return 1;
    }
    strncpy(message, buffer, len);
    struct conn* cursor =pool->conn_head;
    while(cursor != NULL ){
        if(cursor->fd != sd){
            msg_t* new_msg =(msg_t*)calloc(1,sizeof(msg_t));
            if(new_msg ==NULL){
                // malloc
                return 1;
            }
            new_msg->message=message;
            new_msg->size=len;
            new_msg->next=NULL;
            new_msg->prev =cursor->write_msg_tail;
            if(cursor->write_msg_head==NULL){
                cursor->write_msg_head=new_msg;
            }
            else{
                cursor->write_msg_tail->next =new_msg;
            }
            cursor->write_msg_tail =new_msg;
            FD_SET(cursor->fd,&pool->write_set);
        }
        cursor=cursor->next;
    }
    return 0;
}

int writeToClient(int sd,conn_pool_t* pool) {
    if(pool ==NULL || sd <0){
        return 1; // Return error code
    }
    struct conn* client =pool->conn_head;
    while(client !=NULL && client->fd !=sd){
        client =client->next;
    }
    if (client==NULL){
        return 1; // Return error code
    }
    msg_t* cursor =client->write_msg_head;
    while(cursor !=NULL){
        printf("%s", cursor->message);
        int buffer_curser=0;
        while(cursor->size -buffer_curser >0) {
            int bytesWrite = (int)write(sd,cursor->message+buffer_curser,cursor->size -buffer_curser);
            if (bytesWrite==0) {
                break;
            } 
            else if(bytesWrite ==-1) {
                return 1;
            }
            buffer_curser +=bytesWrite;
        }
        client->write_msg_head=cursor->next;
        cursor->next=NULL;
        cursor->prev=NULL;
        free(cursor);
        cursor=client->write_msg_head;
    }
    FD_CLR(sd,&pool->write_set);
    FD_CLR(sd,&pool->ready_write_set);
    return 0;
}

