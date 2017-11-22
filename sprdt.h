#include <pthread.h>

//connection state
#define S_CLOSE 0
#define S_OPEN 1

//packet flags
#define F_SYN 0
#define F_PSH 1
#define F_FIN 2
#define F_ACK 3

//error number
#define E_FORBID -1
#define E_BIND -2
#define E_CONNECT -3
#define E_SELECT -4

//constants
#define SIZE_PKT 1024
#define SIZE_RBUF 32
#define SIZE_SBUF 128
#define SIZE_FBUF 1024
#define TIME_OUT 1
#define TIME_WAIT 5
#define INIT_CWINDOW 1
#define INIT_THRESHOLD 16
#define MAX_SEQ ((int)(((unsigned)(-1))>>1))
#define CNUM 8675309

typedef struct{
	int flag;
	int seq; //sequence number of the packet. -1 if no packet
	int size;
	char data[SIZE_PKT];
	int check;
}pkt; //packet structure

typedef struct{	
	pthread_t stid; //background thread for sending user data. -1 if no sender
	pthread_mutex_t smt; //send mutex
	int seq; //current sequence number
	int th,cw; //congestion window size and threshold
	int ft,rr; //front and rear of the buffer
	pkt sb[SIZE_SBUF]; //send buffer
}sds; //sender structure

typedef struct{
	pthread_t rtid; //background thread for receiving user data. -1 if no receiver
	pthread_mutex_t rmt; //receive mutex
	int ft,rr; //front and rear of the buffer
	pkt rb[SIZE_RBUF]; //receive buffer
}rcs; //receiver structure

typedef struct{
	int state;
	sds sd;
	rcs rc;
}conn; //connection structure

void sp_init(); //simplex rdt functions
int sp_socket();
int sp_connect(int sfd,char *ip,int port);
int sp_listen(int sfd,int port);
int sp_close(int sfd);
int sp_send(int sfd,void *buf,int size);
int sp_send_all(int fd,void *buf,int size);
int sp_send_file(int send_fd,int fd);
int sp_recv(int sfd,void *buf,int size);
int sp_recv_all(int fd,void *buf,int size);
int sp_recv_file(int recv_fd,int fd);
