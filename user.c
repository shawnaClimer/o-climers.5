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

//for int[] of resources
static int numrsc[20];

void sighandler(int sigid){
	printf("Caught signal %d\n", sigid);
	//if terminated naturally, all resources have been released
	//if in deadlock terminated, need to release resources
	//don't have to worry about race conditions, b/c no one is allowed to run right now
	int i;
	for (i = 0; i < 20; i++){
		while (numrsc[i] > 0){
			block[i].num_avail++;//update resource descriptor
			numrsc[i]--;
			//printf("releasing resources b/c deadlock termination\n");
		}
	}
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
	
	//puts(argv[1]);
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
	if((rdmid = shmget(rdkey, (sizeof(resource) * 20), IPC_CREAT | 0666)) == -1){
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
	srand( time(NULL) );
	
	//time to request/release resource
	int checkrsc = rand() % bound;
	int startsec = clock[0];//start sec
	int startns = clock[1];//start ns
	int currentsec;//current seconds
	
	int terminate = 0;//1 if should terminate
	int timetocheck = rand() % 250001;//time to check if should terminate
	int prevns = startns;//ns when process starts
	int currentns;//current ns
	
	//int numrsc[20];//keep track of number of each resource allocated to me
	
	int i;
	for (i = 0; i < 20; i++){
		numrsc[i] = 0;
	}
	int haversc = 0;//num of resources
	
	while(terminate == 0){
		//signal handler
		signal(SIGINT, sighandler);
		
		//look for message type 1 critical section "token"
		if(msgrcv(msqid, &rbuf, 0, 1, 0) < 0){
			//printf("message not received.\n");
		}else{
			//printf("critical section token received.\n");
			//update clock
			clock[1] += rand() % 1000;
			if(clock[1] > 1000000000){
				clock[0] += 1;
				clock[1] -= 1000000000;
			}
			
			//check if should terminate
			currentns = clock[1];
			if ((currentns - prevns) >= timetocheck){
				if (rand() % 100 < 10){//10% chance for termination
				//printf("time to terminate\n");
					terminate = 1;
				}
				timetocheck = rand() % 250001;//set next time to check
			}
			//release all resources so can terminate
			if (terminate == 1){
				//send message that i'm terminating
				sbuf.mtype = 2;//message type 2 
				sbuf.mtext[0] = clock[0];
				sbuf.mtext[1] = clock[1];
				buf_length = sizeof(sbuf.mtext);
				if(msgsnd(msqid, &sbuf, buf_length, IPC_NOWAIT) < 0){
					printf("%d, %d\n", msqid, sbuf.mtype);
					perror("time msgsend");
					detachshared();
				}else{
					printf("user terminating message sent. %d, %d\n", sbuf.mtext[0], sbuf.mtext[1]);
				}
				//printf("releasing resources naturally\n");
				for (i = 0; i < 20; i++){
					while (numrsc[i] > 0){
						blockptr[i].num_avail++;//update resource descriptor
						numrsc[i]--;
					}
				}
				
			}
			//time to request/release resource?
			currentsec = clock[0];
			currentns = clock[1];
			if (((currentsec * 1000000000) + currentns) >= (((startsec * 1000000000) + startns) + checkrsc)){
				startsec = currentsec;//set for next time
				startns = currentns;//set for next time
				int index = rand() % 20;//randomly choose which resource to request/release
				if (rand() % 100 < 80){//80% chance request
					//request resource
					//printf("requesting resource.\n");
					for (i = 0; i < MAX; i++){//loop through pids request in resource descriptor
						if (blockptr[index].request_pids[i] == 0){
							blockptr[index].request_pids[i] = mypid;
							//printf("put pid in request q for resource %d\n", index);
							break;
						}
					} 
					//release critical section
					//message type 1
					sbuf.mtype = 1;
					//send message
					if(msgsnd(msqid, &sbuf, 0, IPC_NOWAIT) < 0){
					//if(msgsnd(msqid, &sbuf, buf_length, IPC_NOWAIT) < 0) {
						printf("%d, %d\n", msqid, sbuf.mtype);//, sbuf.mtext[0], buf_length);
						perror("msgsnd from user");
						detachshared();
					}else{
						//printf("critical section token sent from user.\n");
					}
					
					//wait for request to be granted by message rcv of type mypid
					if (msgrcv(msqid, &rbuf, 0, mypid, 0) < 0){
						
					}else {
						//printf("user received request granted message\n");
						haversc++;
						numrsc[index]++;
					}
					
								
				}else{
					//printf("releasing resource.\n");
					//release a resource
					if (numrsc[index] != 0){
						//access rd and increase num available
						blockptr[index].num_avail++;
						printf("resource %d now has %d available\n", index, blockptr[index].num_avail);
						//decrease number in num resources 
						numrsc[index]--;
						haversc--;//decrease total num of resources allocated
					}
					//release critical section
					//message type 1
					sbuf.mtype = 1;
					//send message
					if(msgsnd(msqid, &sbuf, 0, IPC_NOWAIT) < 0){
					//if(msgsnd(msqid, &sbuf, buf_length, IPC_NOWAIT) < 0) {
						printf("%d, %d\n", msqid, sbuf.mtype);//, sbuf.mtext[0], buf_length);
						perror("msgsnd from user");
						detachshared();
					}else{
						//printf("critical section token sent from user.\n");
					}
					
				}//end release resource
			}//end time to check for request/release resource
			else{
				//release critical section
					//message type 1
					sbuf.mtype = 1;
					//send message
					if(msgsnd(msqid, &sbuf, 0, IPC_NOWAIT) < 0){
					//if(msgsnd(msqid, &sbuf, buf_length, IPC_NOWAIT) < 0) {
						printf("%d, %d\n", msqid, sbuf.mtype);//, sbuf.mtext[0], buf_length);
						perror("msgsnd from user");
						detachshared();
					}else{
						//printf("critical section token sent from user.\n");
					}
			}//release critical section token if not request/release resource time
			
			
			
		}
		
	}//end of while loop
	
			
	//code for freeing shared memory
	if(detachshared() == -1){
		return 1;
	}
		
	
	return 0;
}