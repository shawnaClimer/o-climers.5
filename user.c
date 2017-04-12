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
#include "constants.h"


//for shared memory clock
static int *shared;

//shared memory Resource Descriptors
static resource *block;

void sighandler(int sigid){
	printf("Caught signal %d\n", sigid);
	
	//cleanup shared memory
	detachshared();
	
	exit(sigid);
}
int detachshared(){//detach from shared memory clock and resources
	if((shmdt(shared) == -1) || (shmdt(block) == -1)){
		perror("failed to detach from shared memory");
		return -1;
	}else{
		return 1;
	}
}

int main(int argc, char **argv){
	
	puts(argv[1]);
	//get bound sent as parameter
	int bound = atoi(argv[1]);
	
	//attach to resource descriptors in shared memory
	key_t rdkey;
	int rdmid;
	resource *blockptr;
	//create key
	if((rdkey = ftok("oss.c", 5)) == -1){
		perror("rdkey error");
		return 1;
	}
	//get shared memory
	if((rdmid = shmget(rdkey, (sizeof(resource) * MAXQUEUE), IPC_CREAT | 0666)) == -1){
		perror("failed to create resource descriptors shared memory");
		return 1;
	}
	//attach to shared memory
	if((block = (resource *)shmat(rdmid, NULL, 0)) == (void *)-1){
		perror("failed to attach to resource descriptors memory");
		return 1;
	}
	blockptr = block;	
	//attach to clock shared memory
	key_t key;
	int shmid;
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
	clock = shared;
	
	/* int startSec, startNs;//start "time" for process
	startSec = clock[0];
	startNs = clock[1];
	int runTime = rand() % 100000;
	int endSec = startSec;
	int endNs = startNs + runTime;
	if(endNs > 1000000000){
		endSec++;
		endNs -= 1000000000;
	} */
	
	//attach to message queue in shared memory
	int msqid;
	key_t msgkey;
	message_buf sbuf, rbuf;
	size_t buf_length = 0;
	
	if((msgkey = ftok("oss.c", 2)) == -1){
		perror("msgkey error");
		return 1;
	}
	if((msqid = msgget(msgkey, 0666)) < 0){
		perror("msgget from user");
		return 1;
	}
	int mypid = getpid();
	
	//initialize random number generator
	srand((unsigned) time(NULL));
	
	//time to request/release resource
	int checkrsc = rand() % bound;
	int startsec = clock[0];//start sec
	int startns = clock[1];//start ns
	int currentsec;//current seconds
	
	int terminate = 0;//1 if should terminate
	int timetocheck = rand() % 250001;//time to check if should terminate
	int prevns = startns;//ns when process starts
	int currentns;//current ns
	
	int numrsc[20];//keep track of number of each resource allocated to me
	for (int i = 0; i < 20; i++){
		numrsc[i] = 0;
	}
	int haversc = 0;//num of resources
	
	while(terminate == 0){
		//signal handler
		signal(SIGINT, sighandler);
		
		//TODO request critical section
		//look for message type PID critical section "token"
		if(msgrcv(msqid, &rbuf, MSGSZ, mypid, 0) < 0){
			//printf("message not received.\n");
		}else{
			printf("critical section token received.\n");
			//time to request/release resource?
			currentsec = clock[0];
			currentns = clock[1];
			if (((currentsec * 1000000000) + currentns) >= (((startsec * 1000000000) + startns) + checkrsc)){
				startsec = currentsec;//set for next time
				startns = currentns;//set for next time
				int index = rand() % 20;//randomly choose which resource to request/release
				if (rand() % 10 < 8){//80% chance request
					//TODO request resource
					
					haversc++;
				}else{
					//release a resource
					if (haversc > 0){//have at least one resource
						int release = 0;
						while (release == 0){//release = 1 when one is released
							if (numrsc[index] != 0){
								numrsc[index]--;
								release = 1;
								//access rd and increase num available
								blockptr[index].num_avail++;
								//decrease number in num resources 
								numrsc[index]--;
								haversc--;//decrease total num of resources allocated
							}//end update quanities of resource
						}//end trying to release
					}//end if have resource
				}//end release resource
			}//end time to check for request/release resource
			
			//update clock
			clock[1] += rand() % 1000;
			if(clock[1] > 1000000000){
				clock[0] += 1;
				clock[1] -= 1000000000;
			}
			
			//check if should terminate
			currentns = clock[1];
			if ((currentns - prevns) >= timetocheck){
				if (rand() % 10 < 2){//20% chance for termination
					terminate = 1;
				}
				timetocheck = rand() % 250001;//set next time to check
			}
			//release all resources so can terminate
			if (terminate == 1){
				for (int i = 0; i < 20; i++){
					while (numrsc[i] > 0){
						blockptr[i].num_avail++;
						numrsc[i]--;
					}
				}
			}
			//release critical section
			//message type 1
			sbuf.mtype = 1;
			sbuf.mtext[0] = mypid;
			
			//send message
			if(msgsnd(msqid, &sbuf, MSGSZ, IPC_NOWAIT) < 0){
			//if(msgsnd(msqid, &sbuf, buf_length, IPC_NOWAIT) < 0) {
				printf("%d, %d\n", msqid, sbuf.mtype);//, sbuf.mtext[0], buf_length);
				perror("msgsnd from user");
				return 1;
			}else{
				printf("critical section token sent.\n");
			}
		}//end critical section
		
		
		
			//old code
			//find my pcb
			int foundpcb = 0;
			int i;
			for(i = 0; i < MAXQUEUE; i++){
				if(blockptr[i].pid == mypid){
					foundpcb = 1;
					break;
				}
			}
			//update pcb
			if(foundpcb == 1){
				printf("found my pcb\n");
				//check timesys > timeran
				if(blockptr[i].timesys > (blockptr[i].totalcpu + timeran)){
					printf("need more time, requesting requeue\n");
					//more = 1;//needs more time
					blockptr[i].totalcpu += timeran;
					blockptr[i].timeburst = timeran;
				}else{
					printf("completed my process. terminating.\n");
					timeran = (blockptr[i].timesys - blockptr[i].totalcpu);
					//leftover = ((blockptr[i].totalcpu + timeran) - blockptr[i].timesys);
					//timeran = (timeran - leftover);
					//timeran = ((blockptr[i].totalcpu + timeran) - blockptr[i].timesys);
					blockptr[i].totalcpu += timeran;
					blockptr[i].timeburst = timeran;
					timeisup = 1;//done
					//timeran = leftover;
				}
				
			}//end found and updated pcb
			
			//blockptr[i].timesys = runTime;
			//check time 
			clock = shared;
			
			
			
		}//end received message token
		
	}//end of while loop
	
	//TODO send message that process is terminating
			
	//code for freeing shared memory
	if(detachshared() == -1){
		return 1;
	}
	/* if(shmdt(shared) == -1){
		perror("failed to detach from shared memory");
		return 1;
	} */
	
	
	return 0;
}