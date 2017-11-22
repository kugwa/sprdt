#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/udp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sprdt.h"

char *tf[]={"syn ","data","fin ","ack "};

int usec()
{
	struct timeval tv;
	gettimeofday(&tv,NULL);
	return tv.tv_usec;
}

int main(int argc,char **argv)
{
	int sfd; //socket fd
	struct sockaddr_in ssa,csa; //server and client
	int iscn=0; //1 if the connection has been established
	int rate,ntot=0,ndrop=0; //loss rate, number of received packets and dropped packets
	
	if(argc!=5){
		fprintf(stderr,"Usage: agent [drop_rate] [listen_port] [server_ip] [server_port]\n");
		return 1;
	}
	
	memset(&ssa,0,sizeof(ssa)); //setting server address and port
	ssa.sin_family=AF_INET;
	inet_pton(AF_INET,argv[3],&ssa.sin_addr);
	ssa.sin_port=htons(atoi(argv[4]));
	
	memset(&csa,0,sizeof(csa)); //binding to the listening port
	csa.sin_family=AF_INET;
	csa.sin_addr.s_addr=htonl(INADDR_ANY);
	csa.sin_port=htons(atoi(argv[2]));
	sfd=socket(AF_INET,SOCK_DGRAM,0);
	if(bind(sfd,(struct sockaddr*)&csa,sizeof(csa))<0){
		fprintf(stderr,"bind error\n");
		return 1;
	}
	
	rate=atoi(argv[1]);
	while(1){
		pkt pfw;
		struct sockaddr_in rsa;
		int size=sizeof(rsa);
		
		recvfrom(sfd,&pfw,sizeof(pkt),0,(struct sockaddr*)&rsa,&size);
		ntot++;
		fprintf(stderr,"get  %s #%d\n",tf[pfw.flag],pfw.seq);
		if(usec()%1000<rate && pfw.flag==F_PSH){ //drop
			ndrop++;
			fprintf(stderr,"drop %s #%d, loss rate = %.4f\n",tf[pfw.flag],pfw.seq,(float)ndrop/ntot);
		}
		else if(rsa.sin_port==ssa.sin_port){ //from server
			sendto(sfd,&pfw,sizeof(pkt),0,(struct sockaddr*)&csa,sizeof(csa));
			fprintf(stderr,"fwd  %s #%d\n",tf[pfw.flag],pfw.seq);
		}
		else{ //from client
			if(iscn==0){ //setting client address and port
				memcpy(&csa,&rsa,sizeof(csa));
				iscn=1;
			}
			sendto(sfd,&pfw,sizeof(pkt),0,(struct sockaddr*)&ssa,sizeof(ssa));
			fprintf(stderr,"fwd  %s #%d, loss rate = %.4f\n",tf[pfw.flag],pfw.seq,(float)ndrop/ntot);
		}
	}
	return 0;
}
