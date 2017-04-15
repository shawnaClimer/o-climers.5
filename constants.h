//max num processes
#define MAX		18
//for message queue
#define MSGSZ	2
typedef struct msgbuf {
	long mtype;
	int mtext[MSGSZ];
} message_buf;

//for Resource Descriptors
typedef struct rdescriptor {
	//pids request
	int request_pids[MAX];
	//pids currently allocated to 
	int current_pids[MAX];//use index of pid in pids[] for index here
	int total_num;//number of resource
	int num_avail;//number available
	int share;//shareable = 1, not shareable = 0
} resource;