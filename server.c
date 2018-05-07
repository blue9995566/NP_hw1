/*
** server.c -- a stream socket server demo
*/
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <ctype.h>

#define PORT "6666"  // the port users will be connecting to

#define BACKLOG 5 // how many pending connections queue will hold

static char* next;
static char* next_arg;
static char* args[512];
static char* digi;
static int next_count;
static int cmd_no=0;
static char* ptr_env;
static int _break=0;
static int first;
static char* filename;

void sigchld_handler(int s)
{
	(void)s; // quiet unused variable warning

	// waitpid() might overwrite errno, so we save and restore it:
	int saved_errno = errno;

	while(waitpid(-1, NULL, WNOHANG) > 0);

	errno = saved_errno;
}

// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
	if (sa->sa_family == AF_INET) {
		return &(((struct sockaddr_in*)sa)->sin_addr);
	}

	return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

static void split(char* cmd);
static int ispipe(char* cmd);
static void run(int type);
static int fd[1024][2];
static int tofile(char *args[]);

static int pipein_num(int* pipein);
int main(void)
{
	setenv("PATH","bin:.",1);
	int sockfd, new_fd;  // listen on sock_fd, new connection on new_fd
	struct addrinfo hints, *servinfo, *p;
	struct sockaddr_storage their_addr; // connector's address information
	socklen_t sin_size;
	struct sigaction sa;
	int yes=1;
	char s[INET6_ADDRSTRLEN];
	int rv;

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE; // use my IP

	if ((rv = getaddrinfo(NULL, PORT, &hints, &servinfo)) != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
		return 1;
	}

	// loop through all the results and bind to the first we can
	for(p = servinfo; p != NULL; p = p->ai_next) {
		if ((sockfd = socket(p->ai_family, p->ai_socktype,p->ai_protocol)) == -1) {
			perror("server: socket");
			continue;
		}

		if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes,sizeof(int)) == -1) {
			perror("setsockopt");
			exit(1);
		}

		if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
			close(sockfd);
			perror("server: bind");
			continue;
		}

		break;
	}

	freeaddrinfo(servinfo); // all done with this structure

	if (p == NULL)  {
		fprintf(stderr, "server: failed to bind\n");
		exit(1);
	}

	if (listen(sockfd, BACKLOG) == -1) {
		perror("listen");
		exit(1);
	}

	sa.sa_handler = sigchld_handler; // reap all dead processes
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;
	if (sigaction(SIGCHLD, &sa, NULL) == -1) {
		perror("sigaction");
		exit(1);
	}
	printf("server: waiting for connections...\n");

	while(1) {  // main accept() loop
		sin_size = sizeof their_addr;
		new_fd = accept(sockfd, (struct sockaddr *)&their_addr, &sin_size);
		if (new_fd == -1) {
			perror("accept");
			continue;
		}

		inet_ntop(their_addr.ss_family,get_in_addr((struct sockaddr *)&their_addr),s, sizeof s);
		printf("server: got connection from %s\n", s);

		if (!fork()) { // this is the child process
			close(sockfd); // child doesn't need the listener
			char client_message[10000],*welcome="****************************************\n"
			"** Welcome to the information server. **\n"
			"****************************************\n",*sym="% ";
			char *buffer;
			send(new_fd, welcome,strlen(welcome),0);
			while(1){
				first=1;
				bzero(client_message,10000);
				send(new_fd, sym,strlen(sym),0);
				int count=read(new_fd,client_message,10000);

				if(count<=0) exit(0);

				dup2(new_fd,1);
				dup2(new_fd,2);
				char* cmd=client_message;
				next=strchr(cmd,'|');
				if(next!=NULL) *next='\0';
				while(next!=NULL){
					first=0;
					_break=0;
					split(cmd);
					run(ispipe(next));
					++cmd_no;
					if(_break) break;   //unknow command
					cmd=next;
					next=strchr(cmd,'|');
					if(next==NULL) break;
					*next='\0';
				}
				if(!_break){
					split(cmd);
					run(0);
					++cmd_no;
				}
				_break=0;
			}
			
		}
		close(new_fd);// parent doesn't need this
	}

	return 0;
}
static char* skipwhite(char* s)
{
	while (isspace(*s)) ++s;
	return s;
}
static void split(char* cmd){
	cmd = skipwhite(cmd);
	next_arg = strchr(cmd,' ');
	int i = 0;
 
	while(next_arg != NULL) {
		next_arg[0] = '\0';
		args[i] = cmd;
		++i;
		cmd = skipwhite(next_arg + 1);
		next_arg = strchr(cmd, ' ');
	}
	if (cmd[0] != '\0') {
		args[i] = cmd;
		next_arg = strchr(cmd,'\r');
		if(next_arg==NULL) next_arg = strchr(cmd,'\n');
		next_arg[0] = '\0';
		++i; 
	}
	args[i] = NULL;
}

static int ispipe(char* cmd){
	char num[4];
	for (int i=0;i<4;i++){  // initialize
		num[i]=' ';
	}
	int i=0;
	if(cmd!=NULL){
		digi=cmd+1;
		if(isspace(digi[0])) i=1;
		else{
			while(isdigit(*digi)){
			i++;
			digi++;
			}
			if(i>0){
				strncpy(num,digi-i,i);
				i=atoi(num);
			}
		}
		next=digi;
	}
	return i;
}
static void run(int next_count){
	// printf("%d\n",cmd_no);
	int inuse=0;
	if(args[0] == NULL){
		--cmd_no;
	}
	else if (strcmp(args[0], "exit") == 0) exit(0);
	else if(strcmp(args[0], "setenv") == 0){
		if(args[1]!=NULL&&args[2]!=NULL) setenv(args[1],args[2],1);
	}else if(strcmp(args[0], "printenv") == 0){
		if(args[1]!=NULL){
			char *ptr_path = getenv(args[1]);
			printf("%s=%s\n",args[1],ptr_path);
		}
	}
	else{
		int self=1;  // 1 is not self, 0 is self.
		if(cmd_no>1023) cmd_no-=1024;
		if(fd[cmd_no][0]==0) { // check pipe open or not.
			pipe(fd[cmd_no]);
			self=0;
		}
		int next_cmd=cmd_no+next_count;
		if(next_cmd>1023) next_cmd-=1024;
		if(fd[next_cmd][0]!=0) inuse=1;
		if((next_count!=0) && (fd[next_cmd][0]==0)) pipe(fd[next_cmd]);
		
		// printf("%d\n",next_count);
		// printf("%s:",args[0]);
		// printf("now:%d,fd[%d][0]:%d,fd[%d][1]:%d\n",cmd_no,cmd_no,fd[cmd_no][0],cmd_no,fd[cmd_no][1]);
		// printf("----next:%d,fd[%d][0]:%d,fd[%d][1]:%d----\n",next_cmd,next_cmd,fd[next_cmd][0],next_cmd,fd[next_cmd][1]);
		FILE *fp;
		int tofiles=tofile(args);
		if(next_count==0){
			if(tofiles){
				fp=fopen(filename,"w");
				dup2(fileno(fp),1);
			}
		}
		int status;
		int pid=fork();
		if(pid==0){  //child process
			if(next_count!=0) {      //write to pipe.
				dup2(fd[next_cmd][1],1);
			}
			if(fd[cmd_no][0]!=0 && self) {    //get the pipe input.
				close(fd[cmd_no][1]);
				dup2(fd[cmd_no][0],0);
			}
			if (execvp(args[0],args) == -1){
				dup2(2,1);
				printf("Unknown command: [%s].\n", args[0]);
				_exit(EXIT_FAILURE); // If child fails
			}

		}else{   //parent process
			close(fd[cmd_no][1]);
			waitpid(pid,&status,0);
			if(next_count==0 && tofiles) close(fileno(fp));
			// printf("status:%d\n",status);
			if(status==256  && !first) {
				if(next_count>0 && !inuse){
					close(fd[next_cmd][0]);
					close(fd[next_cmd][1]);
					fd[next_cmd][0]=0;
					fd[next_cmd][1]=0;
				}
				int fakefd[2];
				pipe(fakefd);
				close(fakefd[0]);
				fd[cmd_no][1]=fakefd[1];
				//fake fd
				--cmd_no;
				_break=1;
			}
			else{
				close(fd[cmd_no][0]);
				fd[cmd_no][0]=0;
				fd[cmd_no][1]=0;
			}
			// wait(NULL);
		}
	}	
}

static int pipein_num(int* pipein){
	int	i=0;
	for(int j=0;j<1024;j++){
		if(pipein[j]==0) break;
		++i; 
	}
	return i;
}

static int tofile(char *args[]){
	int ret=0;
	for(int i=0;i<512;i++){
		if(args[i]==NULL) break;
		if(strcmp(args[i],">") == 0){
			ret=1;
			filename=args[i+1];
			args[i]=NULL;
			break;
		}
	}
	return ret;
}