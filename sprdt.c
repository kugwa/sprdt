#include <unistd.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netinet/udp.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sprdt.h"

static int maxfd;
static conn **cls;
static char *tf[]={"syn ","data","fin ","ack "};

static void mkpkt(pkt *p,int flag,int seq,int size,void *data) //make packet
{
	p->flag=flag;
	p->seq=seq;
	p->size=size;
	if(size>0 && data!=NULL)memcpy(p->data,data,size);
	p->check=CNUM;
}

static int chpkt(pkt *p) //check if the packet is corrupt
{
	if(p->check==CNUM)return 0;
	return -1;
}

static int bind2port(int sfd,int port) //bind sfd to port
{
	struct sockaddr_in sa;
	memset(&sa,0,sizeof(sa));

	sa.sin_family=AF_INET;
	sa.sin_addr.s_addr=htonl(INADDR_ANY);
	sa.sin_port=htons(port);
	
	return bind(sfd,(struct sockaddr*)&sa,sizeof(sa));
}

static int selectfd(int sfd,int usec) //wait sfd for usec
{
	fd_set rfds;
	struct timeval tv;
	FD_ZERO(&rfds);
	FD_SET(sfd,&rfds);
	tv.tv_sec=0;
	tv.tv_usec=usec;
	return select(maxfd,&rfds,NULL,NULL,&tv);
}	

static int diffustime(struct timeval *tv1,struct timeval *tv2) //return tv2-tv1 in usec
{
	return (tv2->tv_sec-tv1->tv_sec)*1000000+tv2->tv_usec-tv1->tv_usec;
}

static void sdb_reset(sds *s)
{
	s->seq=0;
	s->ft=0;
	s->rr=0;
}

static void sdb_init(sds *s) //initializing the send buffer
{
	sdb_reset(s);
	int i;for(i=0;i<SIZE_SBUF;i++)s->sb[i].seq=-1;
}

static int sdb_add(sds *s,void *buf,int qsize) //add min(qsize,s->sb[].size) bytes to the buffer
{
	int ret,i;
	pthread_mutex_lock(&s->smt);
	
	i=s->seq%SIZE_SBUF;
	if(s->sb[i].seq!=-1)ret=-1; //buffer is full
	else{
		if(qsize<SIZE_PKT)ret=qsize;
		else ret=SIZE_PKT;
		mkpkt(&s->sb[i],F_PSH,s->seq,ret,buf);
		s->seq++;
	}
	
	pthread_mutex_unlock(&s->smt);
	return ret;
}

static void rcb_reset(rcs *r)
{
	r->ft=0;
	r->rr=0;
}

static void rcb_init(rcs *r) //initializing the receive buffer
{
	rcb_reset(r);
	int i;for(i=0;i<SIZE_RBUF;i++)r->rb[i].seq=-1;
}

static int rcb_insert(rcs *r,pkt *p) //insert the packet into the buffer
{
	int ret,i;
	pthread_mutex_lock(&r->rmt);
	
	i=p->seq%SIZE_RBUF;
	if(p->seq-r->rr>=SIZE_RBUF)ret=-1; //not enough buffer
	else{
		if(p->seq>=r->rr && r->rb[i].seq==-1){ //not inserted yet
			memcpy(&r->rb[i],p,sizeof(pkt));
			if(p->seq>=r->ft)r->ft=p->seq+1;
		}
		ret=0;
	}
	
	pthread_mutex_unlock(&r->rmt);
	return ret;
}

static int rcb_fetch(rcs *r,void *buf,int qsize) //fetch min(qsize,r->rb[].size) bytes from the buffer
{
	int ret,i;
	pthread_mutex_lock(&r->rmt);
	
	i=r->rr%SIZE_RBUF;
	if(r->rb[i].seq==-1)ret=-1; //the packet has not arrived yet
	else if(qsize<r->rb[i].size){
		memcpy(buf,r->rb[i].data,qsize);
		r->rb[i].size-=qsize;
		memmove(r->rb[i].data,r->rb[i].data+qsize,r->rb[i].size);
		ret=qsize;
	}
	else{
		memcpy(buf,r->rb[i].data,r->rb[i].size);
		r->rb[i].seq=-1;
		r->rr++;
		ret=r->rb[i].size;
	}
	
	pthread_mutex_unlock(&r->rmt);
	return ret;
}

static void *bgsend(void *arg) //sender sends packets from the send buffer in background
{
	int sfd=(conn**)arg-cls;
	pkt psd,pack;
	int sret=-1;

	while(cls[sfd]->sd.seq>cls[sfd]->sd.rr || cls[sfd]->state!=S_CLOSE){
		int tmp,bound;
		pthread_mutex_lock(&cls[sfd]->sd.smt); //sending a group of packets
		if(cls[sfd]->sd.rr+cls[sfd]->sd.cw>cls[sfd]->sd.seq)bound=cls[sfd]->sd.seq;
		else bound=cls[sfd]->sd.rr+cls[sfd]->sd.cw;
		//pthread_mutex_unlock(&cls[sfd]->sd.smt);
		for(tmp=cls[sfd]->sd.rr;tmp<bound;tmp++){
			pkt *pp=&cls[sfd]->sd.sb[tmp%SIZE_SBUF];
			pp->seq=tmp; //if the packets was acked, mark it as unacked
			send(sfd,pp,sizeof(pkt),0);
			if(sret==0)fprintf(stderr,"resnd ");
			else fprintf(stderr,"send  ");
			fprintf(stderr,"%s #%d, winSize = %d\n",tf[pp->flag],pp->seq,cls[sfd]->sd.cw);
		}
		sret=-1; //sret > 0 if all packets in the window is acked, 0 if timeout, -1 if no packet sent
		while(cls[sfd]->sd.rr<bound){ //waiting for a group of acks
			if((sret=selectfd(sfd,TIME_OUT*1000000))<0)return;
			if(sret==0)break; //timeout
			recv(sfd,&pack,sizeof(pkt),0); //receiving a ack
			fprintf(stderr,"recv  %s #%d\n",tf[pack.flag],pack.seq);
			if(chpkt(&pack)==0 && pack.flag==F_ACK){ //mark the packet as acked
				//pthread_mutex_lock(&cls[sfd]->sd.smt);
				if(pack.seq>=cls[sfd]->sd.rr){
					cls[sfd]->sd.sb[pack.seq%SIZE_SBUF].seq=-1;
					if(pack.seq==cls[sfd]->sd.rr){
						int j;
						for(j=cls[sfd]->sd.rr+1;j<bound && cls[sfd]->sd.sb[j%SIZE_SBUF].seq==-1;j++);
						cls[sfd]->sd.rr=j;
					}
				}
				//pthread_mutex_unlock(&cls[sfd]->sd.smt);
			}
		}
		pthread_mutex_unlock(&cls[sfd]->sd.smt);
		
		if(sret>0){ //increase congestion window
			if(cls[sfd]->sd.cw<cls[sfd]->sd.th)cls[sfd]->sd.cw+=cls[sfd]->sd.cw;
			else cls[sfd]->sd.cw++;
		}
		else if(sret==0){ //decrease threshold and congestion window
			cls[sfd]->sd.th=cls[sfd]->sd.cw/2;
			cls[sfd]->sd.cw=1;
			fprintf(stderr,"time  out, threshold = %d\n",cls[sfd]->sd.th);
		}
	}

	mkpkt(&psd,F_FIN,0,0,NULL);
	while(1){
		int ret;
		send(sfd,&psd,sizeof(pkt),0); //sending a fin
		fprintf(stderr,"send  %s #%d\n",tf[psd.flag],psd.seq);
		if((ret=selectfd(sfd,TIME_OUT*1000000))<0)return;
		if(ret==0)continue; //timeout
		recv(sfd,&pack,sizeof(pkt),0); //receiving a ack
		fprintf(stderr,"recv  %s #%d\n",tf[pack.flag],pack.seq);
		if(chpkt(&pack)==0 && pack.flag==F_ACK)break;
	} //bgsend() ends
}

static void *bgrecv(void *arg) //receiver receives and stores packets into the receive buffer in background
{
	int sfd=(conn**)arg-cls;
	pkt prc,pack;

	while(1){
		recv(sfd,&prc,sizeof(pkt),0);
		fprintf(stderr,"recv  %s #%d\n",tf[prc.flag],prc.seq);
		if(chpkt(&prc)==0)switch(prc.flag){
			case F_SYN: //open connection request
				mkpkt(&pack,F_ACK,0,0,NULL); //re-ack the connection
				send(sfd,&pack,sizeof(pkt),0);
				fprintf(stderr,"send  %s #%d\n",tf[pack.flag],pack.seq);
				break;
			case F_FIN: //close connection request
				while(1){
					mkpkt(&pack,F_ACK,0,0,NULL);
					send(sfd,&pack,sizeof(pkt),0);
					fprintf(stderr,"send  %s #%d\n",tf[pack.flag],pack.seq);
					int ret=selectfd(sfd,TIME_WAIT*1000000);
					if(ret<=0)return; //bgrecv() ends
				}
			case F_PSH: //data packet
				if(rcb_insert(&cls[sfd]->rc,&prc)==0){
					mkpkt(&pack,F_ACK,prc.seq,0,NULL);
					send(sfd,&pack,sizeof(pkt),0);
					fprintf(stderr,"send  %s #%d\n",tf[pack.flag],pack.seq);
				}
				break;
		}
	}
}

void sp_init()
{
	int sfd;
	maxfd=getdtablesize();
	cls=(conn**)malloc(sizeof(conn*)*maxfd);
	for(sfd=0;sfd<maxfd;sfd++)cls[sfd]=NULL;
}

int sp_socket()
{
	return socket(AF_INET,SOCK_DGRAM,0);
}

int sp_connect(int sfd,char *ip,int port) //the sender connects to the receiver
{
	struct sockaddr_in sa;
	pkt psyn,pack;
	
	if(cls[sfd]!=NULL)return E_FORBID;
	if(bind2port(sfd,0)<0)return E_BIND; //ready to recv ack
	memset(&sa,0,sizeof(sa)); //setting receiver's address and port
	sa.sin_family=AF_INET;
	inet_pton(AF_INET,ip,&sa.sin_addr);
	sa.sin_port=htons(port);
	if(connect(sfd,(struct sockaddr*)&sa,sizeof(sa))<0)return E_CONNECT;
	
	mkpkt(&psyn,F_SYN,0,0,NULL);
	while(1){
		send(sfd,&psyn,sizeof(pkt),0); //sending a syn
		fprintf(stderr,"send  %s\n",tf[psyn.flag]);
		int ret=selectfd(sfd,TIME_OUT*1000000);
		if(ret<0)return E_SELECT;
		if(ret==0)continue; //timeout
		recv(sfd,&pack,sizeof(pkt),0); //receiving a ack
		fprintf(stderr,"recv  %s\n",tf[pack.flag]);
		if(chpkt(&pack)==0 && pack.flag==F_ACK)break; //connection established
		sleep(1); //bad ack, retry
	}
	
	cls[sfd]=(conn*)malloc(sizeof(conn)); //initializing sender's structure
	cls[sfd]->state=S_OPEN;
	cls[sfd]->rc.rtid=-1; //no receiver
	cls[sfd]->sd.th=INIT_THRESHOLD; //default threshold
	cls[sfd]->sd.cw=INIT_CWINDOW; //initial congestion window size
	pthread_mutex_init(&cls[sfd]->sd.smt,NULL);
	sdb_init(&cls[sfd]->sd);
	pthread_create(&cls[sfd]->sd.stid,NULL,bgsend,cls+sfd);
	return 0;
}

int sp_listen(int sfd,int port) //the receiver listens on this port
{
	struct sockaddr_in sa;
	struct in_addr cip;
	int cport,t=sizeof(sa);
	pkt psyn,pack;
	
	if(cls[sfd]!=NULL)return E_FORBID;
	if(bind2port(sfd,port)<0)return E_BIND; //ready to recv data
	do{ //waiting for a syn
		recvfrom(sfd,&psyn,sizeof(pkt),0,(struct sockaddr*)&sa,&t);
		fprintf(stderr,"recv  %s\n",tf[psyn.flag]);
	}while(chpkt(&psyn)<0 || psyn.flag!=F_SYN); //bad syn, retry
	if(connect(sfd,(struct sockaddr*)&sa,sizeof(sa))<0)return E_CONNECT; //setting sender's address and port
	
	mkpkt(&pack,F_ACK,0,0,NULL); //replying a ack
	send(sfd,&pack,sizeof(pkt),0);
	fprintf(stderr,"send  %s\n",tf[pack.flag]);
	
	cls[sfd]=(conn*)malloc(sizeof(conn)); //initializing receiver's structure
	cls[sfd]->state=S_OPEN;
	cls[sfd]->sd.stid=-1; //no sender
	pthread_mutex_init(&cls[sfd]->rc.rmt,NULL);
	rcb_init(&cls[sfd]->rc);
	pthread_create(&cls[sfd]->rc.rtid,NULL,bgrecv,cls+sfd);
	return 0;
}

int sp_close(int sfd)
{
	if(cls[sfd]==NULL || cls[sfd]->state!=S_OPEN)return E_FORBID;
	
	cls[sfd]->state=S_CLOSE;
	if(cls[sfd]->sd.stid!=-1){ //sender case
		pthread_join(cls[sfd]->sd.stid,NULL);
		pthread_mutex_destroy(&cls[sfd]->sd.smt);
	}
	else{ //receiver case
		pthread_join(cls[sfd]->rc.rtid,NULL);
		pthread_mutex_destroy(&cls[sfd]->rc.rmt);
	}
	
	free(cls[sfd]);
	close(sfd);
	return 0;
}

int sp_send(int sfd,void *buf,int size) //try to add size bytes into send buffer
{
	int ret,sum=0;
	if(cls[sfd]==NULL || cls[sfd]->state!=S_OPEN || cls[sfd]->sd.stid==-1)return E_FORBID;
	while(sum<size){
		ret=sdb_add(&cls[sfd]->sd,buf+sum,size-sum);
		if(ret<0)break; //not availiable now
		sum+=ret;
	}
	return sum;
}

int sp_send_all(int fd,void *buf,int size) //not return until all bytes added
{
        int ret,sum=0;
	while(sum<size){
		if((ret=sp_send(fd,buf+sum,size-sum))<0)break;
		sum+=ret;
	}
	return sum;
}

int sp_send_file(int send_fd,int fd)
{
	int ret;
	char buf[SIZE_FBUF];
	
	while(1){
		ret=read(fd,buf,SIZE_FBUF);
		sp_send_all(send_fd,&ret,sizeof(int));
		if(ret<=0)break;
		sp_send_all(send_fd,buf,ret);
	}
	return 0;
}

int sp_recv(int sfd,void *buf,int size) //try to fetch size bytes from receive buffer
{
	int ret,sum=0;
	if(cls[sfd]==NULL || cls[sfd]->state!=S_OPEN || cls[sfd]->rc.rtid==-1)return E_FORBID;
	while(sum<size){
		ret=rcb_fetch(&cls[sfd]->rc,buf+sum,size-sum);
		if(ret<0)break; //nothing to retrieve
		sum+=ret;
	}
	return sum;
}

int sp_recv_all(int fd,void *buf,int size) //not return until all bytes fetched
{
        int ret,sum=0;
	while(sum<size){
		if((ret=sp_recv(fd,buf+sum,size-sum))<0)break;
		sum+=ret;
	}
	return sum;
}

int sp_recv_file(int recv_fd,int fd)
{
	int ret;
	char buf[SIZE_FBUF];
	
	while(1){
		sp_recv_all(recv_fd,&ret,sizeof(int));
		if(ret<=0)break;
		sp_recv_all(recv_fd,buf,ret);
		write(fd,buf,ret);
	}
	return 0;
}
