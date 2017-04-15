#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <ctype.h>
#include <signal.h>
#include <time.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/msg.h>
#include <errno.h>
#include "constants.h"


//for shared memory clock
static int *shared;
static int shmid;

//shared memory Resource Descriptors
static resource *block;
static int rdmid;

//for pids
static pid_t *pidptr;
//for message queue
static int msqid;

void sighandler(int sigid){
	printf("Caught signal %d\n", sigid);
	//send kill message to children
	//access pids[] to kill each child
	int i = 0;
	while(pidptr[i] != '\0'){
		if(pidptr[i] != 0){
			kill(pidptr[i], SIGQUIT);
		}
		i++;
	}
	
	//cleanup shared memory
	cleanup();
	
	exit(sigid);
}
int cleanup(){
	detachshared();
	removeshared();
	deletequeue();
	return 0;
}

int deletequeue(){
	//delete message queue
	struct msqid_ds *buf;
	if(msgctl(msqid, IPC_RMID, buf) == -1){
		perror("msgctl: remove queue failed.");
		return -1;
	}
}
int detachshared(){
	//detach from shared memory clock and resource descriptors
	if((shmdt(shared) == -1) || (shmdt(block) == -1)){
		perror("failed to detach from shared memory");
		return -1;
	}
	
	
}
int removeshared(){
	//remove shared memory clock and resource descriptors
	if((shmctl(shmid, IPC_RMID, NULL) == -1) || (shmctl(rdmid, IPC_RMID, NULL) == -1)){
		perror("failed to delete shared memory");
		return -1;
	}
	
}
int main(int argc, char **argv){
	
	//getopt
	extern char *optarg;
	extern int optind;
	int c, err = 0;
	int hflag=0, sflag=0, lflag=0, tflag=0;
	static char usage[] = "usage: %s -h  \n-l filename \n-i y \n-t z\n";
	
	char *filename, *x, *z;
	
	while((c = getopt(argc, argv, "hs:l:i:t:")) != -1)
		switch (c) {
			case 'h':
				hflag = 1;
				break;
			case 's':
				sflag = 1;
				x = optarg;//max number of slave processes
				break;
			case 'l':
				lflag = 1;
				filename = optarg;//log file 
				break;
			
			case 't':
				tflag = 1;
				z = optarg;//time until master terminates
				break;
			case '?':
				err = 1;
				break;
		}
		
	if(err){
		fprintf(stderr, usage, argv[0]);
		exit(1);
	}
	//help
	if(hflag){
		puts("-h for help\n-l to name log file\n-s for number of slaves\n-i for number of increments per slave\n-t time for master termination\n");
	}
	//set default filename for log
	if(lflag == 0){
		filename = "test.out";
	}
	puts(filename);
	//number of slaves
	int numSlaves = 5; 
	if(sflag){//change numSlaves
		numSlaves = atoi(x);
	}
	
	//time in seconds for master to terminate
	int endTime = 2;
	if(tflag){//change endTime
		endTime = atoi(z);
	}
	
	//create message queue in shared memory
	key_t msgkey;
	message_buf sbuf, rbuf;
	size_t buf_length = 0;
	
	if((msgkey = ftok("oss.c", 2)) == -1){
		perror("msgkey error");
		return 1;
	}
	if((msqid = msgget(msgkey, IPC_CREAT | 0666)) < 0){
		perror("msgget from oss");
		return 1;
	}
	
	//create Resource Descriptors in shared memory
	key_t rdkey;
	resource *blockptr;
	//create key
	if((rdkey = ftok("oss.c", 5)) == -1){
		perror("rdkey error");
		return 1;
	}
	//get shared memory change to sizeof *block?
	if((rdmid = shmget(rdkey, (sizeof(resource) * 20), IPC_CREAT | 0666)) == -1){
		perror("failed to create resource descriptors shared memory");
		return 1;
	}
	//attach to shared memory
	if((block = (resource *)shmat(rdmid, NULL, 0)) == (void *)-1){
		perror("failed to attach to resource descriptors memory");
		return 1;
	}
	//delete after detach
	//shmctl(rdmid, IPC_RMID, 0);
	
	blockptr = block;
	
	//create clock in shared memory
	key_t key;
	//int shmid;
	//int *shared;
	int *clock;
	void *shmaddr = NULL;
	
	if((key = ftok("oss.c", 7)) == -1){
		perror("key error");
		return 1;
	} 
	//get the shared memory
	if((shmid = shmget(key, (sizeof(int) * 2), IPC_CREAT | 0666)) == -1){
		perror("failed to create shared memory");
		return 1;
	}
	//attach to shared memory
	if((shared = (int *)shmat(shmid, shmaddr, 0)) == (void *)-1){
		perror("failed to attach");
		if(shmctl(shmid, IPC_RMID, NULL) == -1){
			perror("failed to remove memory seg");
		}
		return 1;
	}
	//delete after detach
	//shmctl(shmid, IPC_RMID, 0);

	clock = shared;
	clock[0] = 0;//initialize "clock" to zero
	clock[1] = 0;
	
	//create start time
	struct timespec start, now;
	clockid_t clockid;//clockid for timer
	clockid = CLOCK_REALTIME;
	long starttime, nowtime;
	if(clock_gettime(clockid, &start) == 0){
		starttime = start.tv_sec;
	}
	if(clock_gettime(clockid, &now) == 0){
		nowtime = now.tv_sec;
	}
	
	int totalProcesses = 0;//keep count of total processes created
	int currentnum = 0;//keep count of current processes in system
	
	//for forking children
	pid_t pids[numSlaves];//pid_t *pidptr points to this
	pidptr = pids;
	//initialize pids[]
	printf("initializing pids[]\n");
	int i;
	for(i = 0; i < numSlaves; i++){
		pids[i] = 1;
	}
	//pid
	pid_t pid;
	int thispid;
	
	int childsec, childns;//for time sent by child
	int status;//for wait(&status)
	int sendnext = 1;//send next process message to run
	int loglength = 0;//for log file
	
	//int childclock = 0;//1 when child is given access to clock
	
	//initialize random number generator
	srand( time(NULL) );
	
	//interval between forking children
	int timetofork = rand() % 500000;
	int currentns, prevns = 0;
	
	//initialize Resource Descriptors
	for (i = 0; i < 20; i++){
		if (rand() % 100 > 78){//20% chance
			blockptr[i].share = 1;
			blockptr[i].num_avail = 0;
		}else{
			blockptr[i].share = 0;
			blockptr[i].total_num = (rand() % 10) + 1;
			blockptr[i].num_avail = blockptr[i].total_num;
		}
		int j;
		for (j = 0; j < MAX; j++){
			blockptr[i].request_pids[j] = 0;//initialize request array
		}
		printf("resource %d has share = %d and num_avail = %d\n", i, blockptr[i].share, blockptr[i].num_avail);
	}
	
	//put message type 1 (critical section token) into message queue
	sbuf.mtype = 1;
	//send message
	if(msgsnd(msqid, &sbuf, 0, IPC_NOWAIT) < 0) {
		printf("%d, %d\n", msqid, sbuf.mtype);
		perror("msgsnd");
		cleanup();
	}else{
		printf("critical section token available\n");
	}
	
	while(totalProcesses < 100 && clock[0] < 20 && (nowtime - starttime) < endTime){
		//signal handler
		signal(SIGINT, sighandler);
		
		//check for critical section token in message queue
		if(msgrcv(msqid, &rbuf, 0, 1, 0) < 0){
			//printf("message not received.\n");
		}else{
			//printf("critical section token received\n");
			clock[1] += rand() % 1000;
			if(clock[1] > 1000000000){
				clock[0] += 1;
				clock[1] -= 1000000000;
			}
		
			//put critical section token back into message queue	
			sbuf.mtype = 1;
			//send message
			if(msgsnd(msqid, &sbuf, 0, IPC_NOWAIT) < 0) {
				printf("%d, %d, %d, %d\n", msqid, sbuf.mtype);
				perror("msgsnd critical section token");
				cleanup();
			}else{
				//printf("critical section token available\n");
			}
		}		
		//fork children
		currentns = clock[1];
		//if time to fork new process && current number of processes < max number
		if(((currentns - prevns) >= timetofork) && (currentnum < numSlaves)){
			prevns = currentns;
			//find empty pids[]
			for(i = 0; i < numSlaves; i++){
				if(pids[i] == 1){
					break;
				}
			}
			pids[i] = fork();
			if(pids[i] == -1){
				perror("Failed to fork");
				cleanup();
			}
			if(pids[i] == 0){
				execl("user", "user", "10000", NULL);
				perror("Child failed to exec user");
				cleanup();
			}
			//printf("forked a child\n");
			totalProcesses++;//add to total processes	
			currentnum++;//add to current number of processes
		}
		
		
		//check for resource requests in resource descriptors
		for (i = 0; i < 20; i++){
			int j;
			for (j = 0; j < MAX; j++){
				if ((blockptr[i].num_avail > 0) || (blockptr[i].share == 1)){
					//allocate resource
					if (blockptr[i].request_pids[j] != 0){
						//send request granted message to pid
						sbuf.mtype = blockptr[i].request_pids[j];
						if(msgsnd(msqid, &sbuf, 0, IPC_NOWAIT) < 0){
							perror("resource granted message send error");
							cleanup();
						}else{
							blockptr[i].num_avail--;
							blockptr[i].request_pids[j] = 0;
							//printf("resource granted message sent\n");
						}
					}
				}
			}
		}
		
	
		
		//check for child termination messages
		errno = 0;
		if(msgrcv(msqid, &rbuf, sizeof(int [MSGSZ]), 2, MSG_NOERROR | IPC_NOWAIT) < 0){
			if(errno != ENOMSG){
				perror("msgrcv in oss");
				cleanup();
			}
				//printf("message time up from user not received.\n");
		}else{
			childsec = rbuf.mtext[0];
			childns = rbuf.mtext[1];
			printf("time up message from user received.\n");
			//TODO write to log
			//TODO update pids[], current num processes
				
		}
		//TODO run deadlock detection
		
		//get current time
		if(clock_gettime(clockid, &now) == 0){
			nowtime = now.tv_sec;
		}
	}
	//terminate any leftover children
	while(currentnum > 0){
		currentnum--;
		kill(pids[currentnum], SIGQUIT);
	} 
	
	//code for freeing shared memory
	/* if(detachshared() == -1){
		return 1;
	}
	if(removeshared() == -1){
		return 1;
	}
	
	//delete message queue
	if(deletequeue() == -1){
		return 1;
	} */
	cleanup();
	return 0;
}