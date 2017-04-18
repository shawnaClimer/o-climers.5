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
	 int i;
	for ( i = 0; i < MAX; i++){
		if (pidptr[i] != 1){
			kill(pidptr[i], SIGQUIT);
		}
	} 
	/* while(pidptr[i] != '\0'){
		if(pidptr[i] != 1){
			kill(pidptr[i], SIGQUIT);
		}
		i++; */
	//}
	
	//cleanup shared memory
	cleanup();
	
	exit(sigid);
}
int cleanup(){
	int i;
	for ( i = 0; i < MAX; i++){
		if (pidptr[i] != 1){
			kill(pidptr[i], SIGQUIT);
		}
	}
	/* while(pidptr[i] != '\0'){
		if(pidptr[i] != 1){
			kill(pidptr[i], SIGQUIT);
		}
		i++;
	} */
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
	int hflag=0, sflag=0, lflag=0, tflag=0, vflag=0;
	static char usage[] = "usage: %s -h \n-v \n-l filename \n-i y \n-t z\n";
	
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
			case 'v':
				vflag = 1;//verbose on
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
		puts("-h for help\n-l to name log file\n-s for number of slaves\n-i for number of increments per slave\n-t time for master termination\n-v verbose on\n");
	}
	//set default filename for log
	if(lflag == 0){
		filename = "test.out";
	}
	puts(filename);
	//number of slaves
	int numSlaves = 10; 
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
	pid_t pids[MAX];//pid_t *pidptr points to this
	pidptr = pids;
	//initialize pids[]
	//printf("initializing pids[]\n");
	int i;
	for(i = 0; i < MAX; i++){
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
	int timetofork = rand() % 500000000;//500 milliseconds
	int currentns, prevns, prevsec = 0;
	
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
			blockptr[i].current_pids[j] = 0;//initialize number resource each pid has
		}
		//printf("resource %d has share = %d and num_avail = %d\n", i, blockptr[i].share, blockptr[i].num_avail);
	}
	
	//put message type 1 (critical section token) into message queue
	sbuf.mtype = 1;
	//send message
	if(msgsnd(msqid, &sbuf, 0, IPC_NOWAIT) < 0) {
		printf("%d, %d\n", msqid, sbuf.mtype);
		perror("msgsnd");
		//cleanup();
		return 1;
	}else{
		//printf("critical section token available\n");
	}
	
	//time interval for deadlock detection
	//long deadlock_check = 5000000000;
	int deadlock_check = 4;//4 seconds
	int last_checksec = 0;
	int last_checkns = 0;
	int currentsec;//current sec on clock
	
	//statistics
	int requests_granted = 0;//total requests granted
	int num_natural = 0;//terminated naturally
	int num_deadlock = 0;//terminated by deadlock
	int num_check = 0;//num times deadlock ran
	int each_deadlock = 0;//num terminated in each deadlock
	double per_killed[5];//percent killed in deadlock
	
	while(totalProcesses < 100 && clock[0] < 20 && (nowtime - starttime) < endTime){
		//signal handler
		signal(SIGINT, sighandler);
		
		errno = 0;
		//check for critical section token in message queue
		if(msgrcv(msqid, &rbuf, 0, 1, MSG_NOERROR | IPC_NOWAIT) < 0){
			if(errno != ENOMSG){
				perror("msgrcv in oss");
				//cleanup();
				return 1;
			}
			
		}else{
			//printf("critical section token received\n");
			clock[1] += rand() % 1000000;
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
				//cleanup();
				return 1;
			}else{
				//printf("critical section token available\n");
			}
		}		
		//fork children
		currentsec = clock[0];
		currentns = clock[1];
		//if time to fork new process && current number of processes < max number
		if(((((currentsec * 1000000000) + currentns) - ((prevsec * 1000000000) + prevns)) >= timetofork) && (currentnum < numSlaves)){
			prevns = currentns;
			prevsec = currentsec;
			//find empty pids[]
			for(i = 0; i < numSlaves; i++){
				if(pids[i] == 1){
					break;
				}
			}
			pids[i] = fork();
			if(pids[i] == -1){
				perror("Failed to fork");
				//cleanup();
				return 1;
			}
			
			//convert i to a string to send as parameter
			int length = snprintf(NULL, 0, "%d", i);
			char* pidnum = malloc(length + 1);
			snprintf(pidnum, (length + 1), "%d", i);
			
			if(pids[i] == 0){
				execl("user", "user", "10000", pidnum, NULL);
				perror("Child failed to exec user");
				//cleanup();
				return 1;
			}
			
			free(pidnum);
			//printf("forked a child\n");
			totalProcesses++;//add to total processes	
			currentnum++;//add to current number of processes
		}
		errno = 0;
		//check for critical section token in message queue
		if(msgrcv(msqid, &rbuf, 0, 1, MSG_NOERROR | IPC_NOWAIT) < 0){
			if(errno != ENOMSG){
				perror("msgrcv in oss");
				//cleanup();
				return 1;
			}
			//printf("message not received.\n");
		}else{
			//check for resource requests in resource descriptors
			for (i = 0; i < 20; i++){
				int j;
				for (j = 0; j < MAX; j++){
					if ((blockptr[i].num_avail > 0) || (blockptr[i].share == 1)){
						//allocate resource
						if (blockptr[i].request_pids[j] != 0){
							//find pid index in pids[]
							int x;
							for (x = 0; x < numSlaves; x++){
								if (pids[x] == blockptr[i].request_pids[j]){
									blockptr[i].current_pids[x]++;//update quantity of resource this process has
									//printf("current_pids updated\n");
									break;
								}
							}
							blockptr[i].num_avail--;//decrease num available
							requests_granted++;//total requests granted					
							//send request granted message to pid
							sbuf.mtype = blockptr[i].request_pids[j];
							if(msgsnd(msqid, &sbuf, 0, IPC_NOWAIT) < 0){
								perror("resource granted message send error");
								//cleanup();
								return 1;
							}
							
							//log request granted if verbose
							if (vflag == 1){
								if(loglength < 1000){//log file is under 1000 lines
									FILE *logfile;
									logfile = fopen(filename, "a");
									if(logfile == NULL){
										perror("Log file failed to open");
										//cleanup();
										return 1;
									}
									fprintf(logfile, "OSS: Granted request for resource %d for pid %d at time %d : %d\n", i, blockptr[i].request_pids[j], clock[0], clock[1]);
									fclose(logfile);
									loglength++;
								}
							}
							blockptr[i].request_pids[j] = 0;//remove pid from request
						}
					}
				}
			}
		
			//deadlock detection
			currentsec = clock[0];
			currentns = clock[1];
			if ((currentsec - last_checksec) >= deadlock_check){
			//if ((((currentsec * 1000000000) + currentns) - ((last_checksec * 1000000000) + last_checkns)) >= deadlock_check){
				last_checksec = currentsec;
				last_checkns = currentns;
				num_check++;//total times checking for deadlock
				int deadlock = 0;//no deadlock
				//int catch = 0;//jsut to make sure not an ifinite loop
				//printf("checking for deadlock at %d : %d\n", currentsec, currentns);
				do {
					int x;
					for (i = 0; i < 20; i++){
						//if not shareable and none available
						if ((blockptr[i].share == 0) && (blockptr[i].num_avail == 0)){
							int j;
							for (j = 0; j < MAX; j++){
								//pid in requests, none available
								if (blockptr[i].request_pids[j] != 0){
									deadlock = 1;
									if(loglength < 1000){//log file is under 1000 lines
										FILE *logfile;
										logfile = fopen(filename, "a");
										if(logfile == NULL){
											perror("Log file failed to open");
											//cleanup();
											return 1;
										}
										fprintf(logfile, "OSS: Detected deadlock for pid %d wanting resource %d at time %d : %d\n", blockptr[i].request_pids[j], i, clock[0], clock[1]);
										fclose(logfile);
										loglength++;
									}
									//printf("found deadlock\n");
									//find pid and remove resources and delete process
									pid = blockptr[i].request_pids[j];
									blockptr[i].request_pids[j] = 0;//remove from request_pids
									for (x = 0; x < numSlaves; x++){
										if (pids[x] == pid){
											//printf("found pid %d involved in deadlock. is pids[%d]\n", pid, x);
											break;
										}
									}
									
									break;//break out of for loop for j requests
								}
							}
							break;//break out of for loop for i resources
						}
					}
					if (deadlock == 1){
						if(loglength < 1000){//log file is under 1000 lines
							FILE *logfile;
							logfile = fopen(filename, "a");
							if(logfile == NULL){
								perror("Log file failed to open");
								//cleanup();
								return 1;
							}
							fprintf(logfile, "OSS: Removing resources from and killing pid %d at time %d : %d\n", pid, clock[0], clock[1]);
							fclose(logfile);
							loglength++;
						}
						//remove pid and its resources
						for (i = 0; i < 20; i++){
							while (blockptr[i].current_pids[x] > 0){
								blockptr[i].current_pids[x]--;
								blockptr[i].num_avail++;
								if(loglength < 1000){//log file is under 1000 lines
									FILE *logfile;
									logfile = fopen(filename, "a");
									if(logfile == NULL){
										perror("Log file failed to open");
										//cleanup();
										return 1;
									}
									fprintf(logfile, "\tResource %d released\n", i);
									fclose(logfile);
									loglength++;
								}
							}
						}
						kill(pid, SIGQUIT);
						pids[x] = 1;//remove from pids
						currentnum--;//decrease current num processes
						deadlock = 0;//hopeful
						num_deadlock++;//total terminated by deadlock
						each_deadlock++;//total in this deadlock
					}
					
					//test if still deadlock
					for (i = 0; i < 20; i++){
						if ((blockptr[i].share == 0) && (blockptr[i].num_avail == 0)){
							int j;
							for (j = 0; j < MAX; j++){
								if (blockptr[i].request_pids[j] != 0){
									deadlock = 1;//still deadlocked
									if(loglength < 1000){//log file is under 1000 lines
										FILE *logfile;
										logfile = fopen(filename, "a");
										if(logfile == NULL){
											perror("Log file failed to open");
											//cleanup();
											return 1;
										}
										fprintf(logfile, "\tDeadlock still exists for pid %d\n", blockptr[i].request_pids[j]);
										fclose(logfile);
										loglength++;
									}
									break;
								}
							}
							break;
						}
					}
					//catch++;
				}while (deadlock == 1);// && catch < 10);
				double temp = (each_deadlock / ((double)(currentnum + each_deadlock)));
				per_killed[(num_check - 1)] = temp;
				//printf("%d killed this deadlock out of %d current num = %.2f percent\n", each_deadlock, currentnum + each_deadlock, temp);
				each_deadlock = 0;
			}
			//put critical section token back into message queue	
			sbuf.mtype = 1;
			//send message
			if(msgsnd(msqid, &sbuf, 0, IPC_NOWAIT) < 0) {
				printf("%d, %d, %d, %d\n", msqid, sbuf.mtype);
				perror("msgsnd critical section token");
				//cleanup();
				return 1;
			}else{
				//printf("critical section token available\n");
			}
		}
	
		
		//check for child termination messages
		errno = 0;
		if(msgrcv(msqid, &rbuf, sizeof(int [MSGSZ]), 2, MSG_NOERROR | IPC_NOWAIT) < 0){
			if(errno != ENOMSG){
				perror("msgrcv in oss");
				//cleanup();
				return 1;
			}
				//printf("message time up from user not received.\n");
		}else{
			num_natural++;
			childsec = rbuf.mtext[0];
			childns = rbuf.mtext[1];
			//printf("time up message from user received.\n");
			//update pids[], current num processes
			pid = wait(&status);//make sure child terminated
			//find pid in pids[]
			for(i = 0; i < numSlaves; i++){
				if(pids[i] == pid){
					//printf("found pid that terminated.\n");
					pids[i] = 1;
					currentnum--;
				}
			}
				
		}
				
		//get current time
		if(clock_gettime(clockid, &now) == 0){
			nowtime = now.tv_sec;
		}
	}
	//terminate any leftover children
	/* while(currentnum > 0){
		currentnum--;
		kill(pids[currentnum], SIGQUIT);
	}  */
	for (i = 0; i < numSlaves; i++){
		if (pids[i] != 1){
			kill(pids[i], SIGQUIT);
		}
	}
	printf("%d total processes started\n", totalProcesses);
	printf("%d total processes terminated naturally\n", num_natural);
	printf("%d total processes terminated by deadlock\n", num_deadlock);
	printf("%d total times deadlock detection ran\n", num_check);
	double temp = 0;
	for (i = 0; i < num_check; i++){
		temp += per_killed[i];
	}
	if (temp != 0){
		temp = (temp / num_check);
	}
	
	printf("%.2f percent average killed in deadlock\n", temp * 100);
	printf("%d total requests granted\n", requests_granted);
	
	
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