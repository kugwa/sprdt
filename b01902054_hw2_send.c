#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include "sprdt.h"

int main(int argc,char **argv)
{
	int ret,sfd,fd;

	if(argc!=4){
		fprintf(stderr,"Usage: sender [file] [ip] [port]\n");
		return 1;
	}
	if((fd=open(argv[1],O_RDONLY))<0){
		fprintf(stderr,"open error\n");
		return 1;
	}
	
	sp_init();
	sfd=sp_socket();
	if((ret=sp_connect(sfd,argv[2],atoi(argv[3])))<0){
		fprintf(stderr,"sp_connect error %d %d\n",ret,errno);
		return 1;
	}
	
	sp_send_file(sfd,fd);
	
	sp_close(sfd);
	close(fd);
	return 0;
}
