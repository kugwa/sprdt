#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include "sprdt.h"

int main(int argc,char **argv)
{
	int ret,sfd,fd;

	if(argc!=3){
		fprintf(stderr,"Usage: receiver [file] [port]\n");
		return 1;
	}
	if((fd=open(argv[1],O_WRONLY | O_CREAT | O_TRUNC,0664))<0){
		fprintf(stderr,"open error\n");
		return 1;
	}
	
	sp_init();
	sfd=sp_socket();
	if((ret=sp_listen(sfd,atoi(argv[2])))<0){
		fprintf(stderr,"sp_listen error %d %d\n",ret,errno);
		return 1;
	}
	
	sp_recv_file(sfd,fd);
	
	sp_close(sfd);
	close(fd);
	return 0;
}
