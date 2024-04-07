#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <fcntl.h>
#include <string.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <limits.h>
#include <sys/shm.h>


#define FROM_PROCESS 10			  
#define TO_PROCESS 20			 
#define INVALID_PAGE_REFERENCE -2 
#define PAGE_FAULT -1
#define PROCESS_OVER -9
#define PAGE_FAULT_HANDLED 1 
#define TERMINATED 2		 

int timestamp = 0;		  
int fault_frequency[10000];	  
int fault_frequency_index = 0; 
int logfile;

typedef struct
{ // Page Table Entry has the Frame number, valid/invalid and time of use (timestamp)
	int frame;
	bool valid;
	int time;
} PageTableEntry;

typedef struct
{ // To enter necessary information for a process: The id, number of pages, allocated number of frames and used number of frames
	int pid;
	int m;
	int allocount;
	int usecount;
} process;

typedef struct
{ // Free Frame List: Stores the size of the list and the free frames available
	int size;
	int ffl[];
} FFL;

// typedef struct{						//Translation Lookaside Buffer Stores the id, pageno, frameno, timestamp (for replacing in TLB) and valid (if something exists in TLB or not)
// 	int pid;
// 	int pageno;
// 	int frameno;
// 	int time;
// 	bool valid;
// }TLB;

// Message Queues

struct MQ2_buffer
{ // To send msg to scheduler via MQ2
	long mtype;
	char mbuf[1];
};

struct MQ3_recv_buffer
{ // To receive id and pageno from the process via MQ3
	long mtype;
	int id;
	int pageno;
};

struct MQ3_send_buffer
{ // To send frameno to process via MQ3
	long mtype;
	int frameno;
};

int SM1, SM2; // ids for various queues and shared memories
int MQ2, MQ3;
int ProcessBlock_ID;

process *ProcessBlock; // Structures
PageTableEntry *PageTable;
FFL *FreeFrameList;
// vector<TLB> tlb;					//TLB

int m, k, s;

void done(int signo) // Signal Handler for SIGUSR1
{
	int i;
	if (signo == SIGUSR2)
	{
		printf("Frequency of Page Faults for Each Process:\n");
		write(logfile, "Frequency of Page Faults for Each Process:\n", 43);
		printf("PID\tFrequency\n");
		write(logfile, "PID\tFrequency\n", 14);
		for (i = 0; i < k; i++)
		{			
			printf("%d\t%d\n", i, fault_frequency[i]);
			char buf[100];
			memset(buf, 0, sizeof(buf));
			sprintf(buf, "%d\t%d\n", i, fault_frequency[i]);
			write(logfile, buf, strlen(buf));
		}

		shmdt(ProcessBlock); // Detach various shared memory segments
		shmdt(PageTable);
		shmdt(FreeFrameList);
		close(logfile); // close the file
		exit(0);
	}
}

int HandlePageFault(int id, int pageno) // handle the page faults
{
	int i, frameno;
	if (FreeFrameList->size == 0 || ProcessBlock[id].usecount > ProcessBlock[id].allocount) // if there is no free frame or if the page has all its allocated number of frames used
	{
		int min = INT_MAX, mini = -1; // find the frame with the minimum timestamp, specifying the LRU policy
		for (i = 0; i < ProcessBlock[id].m; i++)
		{
			if (PageTable[id * m + i].valid == true)
			{
				if (PageTable[id * m + i].time < min)
				{
					min = PageTable[id * m + i].time; // minimum timestamp is found
					mini = i;
				}
			}
		}
		PageTable[id * m + mini].valid = false;   // that page table entry is made invalid
		frameno = PageTable[id * m + mini].frame; // corresponding frame is returned
	}
	else
	{
		frameno = FreeFrameList->ffl[FreeFrameList->size - 1]; // otherwise get a free frame and allot it to the corresponding process
		FreeFrameList->size -= 1;
		ProcessBlock[id].usecount++;
	}
	return frameno;
}

void SendFrameNumber(int id, int frame) // send frame number to process specified by id
{
	struct MQ3_send_buffer message_to_process;
	int length;

	message_to_process.mtype = TO_PROCESS + id; // message to process
	message_to_process.frameno = frame;
	length = sizeof(struct MQ3_send_buffer) - sizeof(long);

	if (msgsnd(MQ3, &message_to_process, length, 0) == -1) // send frame number
	{
		perror("Error in sending message");
		exit(1);
	}
}

void SendMessageToScheduler(int type) // send type1/type2 message to scheduler
{
	struct MQ2_buffer message_to_scheduler;
	int length;

	message_to_scheduler.mtype = type;
	length = sizeof(struct MQ2_buffer) - sizeof(long);

	if (msgsnd(MQ2, &message_to_scheduler, length, 0) == -1) 
	{
		perror("Error in sending message");
		exit(1);
	}
}

void FreeFrames(int id) // When a process is over/terminated, free all the frames allotted to it
{
	int i = 0;
	for (i = 0; i < ProcessBlock[i].m; i++)
	{
		if (PageTable[id * m + i].valid == true)
		{
			FreeFrameList->ffl[FreeFrameList->size] = PageTable[id * m + i].frame; // add the frame to FreeFramesList
			FreeFrameList->size += 1;								  // increment size
		}
	}
}

void ServiceMessageRequest() // Service message requests
{
	int id, pageno, length, frameno, i, found;
	int mintime, mini;
	struct MQ3_recv_buffer message_from_process;
	struct MQ3_send_buffer message_to_process;
	length = sizeof(struct MQ3_recv_buffer) - sizeof(long);
	if (msgrcv(MQ3, &message_from_process, length, FROM_PROCESS, 0) == -1) // Receive a message from the process
	{
		perror("Error in receiving message");
		exit(1);
	}
	id = message_from_process.id;
	pageno = message_from_process.pageno; // Retrieve the process id and page number requested

	if (pageno == PROCESS_OVER) // if -9 is received, free frames and send type 2 message to scheduler
	{
		FreeFrames(id);
		SendMessageToScheduler(TERMINATED);
		return;
	}

	timestamp++; // Increase the timestamp
	
	printf("Page reference: (%d, %d, %d)\n", timestamp, id, pageno);
	char buf[1000];
	memset(buf, 0, sizeof(buf));
	sprintf(buf, "Page reference: (%d, %d, %d)\n", timestamp, id, pageno);	
	write(logfile, buf, strlen(buf));
	memset(buf, 0, sizeof(buf));

	if(pageno > ProcessBlock[id].m || pageno < 0) // If we refer to an invalid page number
	{		
		printf("Invalid Page Reference: (%d, %d)\n", id, pageno);		
		sprintf(buf, "Invalid Page Reference: (%d, %d)\n", id, pageno);
		write(logfile, buf, strlen(buf));
		SendFrameNumber(id, INVALID_PAGE_REFERENCE); // Send invalid reference to process
		FreeFrames(id); // Free frames and terminate the process
		SendMessageToScheduler(TERMINATED);
	}
	else
	{
		// for(i=0;i<s;i++)			//Go through TLB in SET ASSOCIATIVE manner (here assume it is shown sequentially)
		// {
		// 	if(tlb[i].valid==true&&tlb[i].pid==id&&tlb[i].pageno==pageno)
		// 	{
		// 		tlb[i].time=timestamp;			//if found in TLB
		// 		cout<<"Found in TLB\n";
		// 		SendFrameNumber(id,tlb[i].frameno);
		// 		return;
		// 	}
		// }

		if (PageTable[id * m + pageno].valid == true) // if found in page table but not in TLB
		{
			frameno = PageTable[id * m + pageno].frame;

			// updateTLB(id,pageno,frameno);		//update TLB and return frame number
			SendFrameNumber(id, frameno);
			PageTable[id * m + pageno].time = timestamp;
		}
		else
		{			
			printf("Page Fault: (%d, %d)\n", id, pageno);			
			sprintf(buf, "Page Fault: (%d, %d)\n", id, pageno);
			write(logfile, buf, strlen(buf));
			fault_frequency[id] += 1;
			SendFrameNumber(id, PAGE_FAULT); // otherwise we get a page fault, we handle the page fault, update TLB and PageTable
			frameno = HandlePageFault(id, pageno);
			// updateTLB(id,pageno,frameno);
			PageTable[id * m + pageno].valid = true;
			PageTable[id * m + pageno].time = timestamp;
			PageTable[id * m + pageno].frame = frameno;
			SendMessageToScheduler(PAGE_FAULT_HANDLED); // tell scheduler that page fault is handled
		}
	}
}

// void updateTLB(int id,int pageno,int frameno)			//Update the TLB with the given ID, Page no and Frame no
// //HERE AS WE CANNOT HAVE ASSOCIATIVITY BECAUSE C++ PROGRAMMING IS SEQUENTIAL, WE WILL ASSUME THAT ANY LOOPS IN THE FOLLOWING
// //FUNCTION RUN ALL THE LOOP VARIABLE CASES PARALLELLY. THIS IS JUST A SIMULATION OF THE SAME
// {
// 	int i;
// 	int mintime=INT_MAX,mini;
// 	int found=0;
// 	for(i=0;i<s;i++)					//Parallelly go through all the TLB indices
// 	{
// 		if(tlb[i].valid==false) 		//if the we get an empty place, update it with the current results and break
// 		{
// 			tlb[i].valid=true;
// 			tlb[i].time=timestamp;
// 			tlb[i].pid=id;
// 			tlb[i].pageno=pageno;
// 			tlb[i].frameno=frameno;
// 			found=1;
// 			break;
// 		}
// 		else
// 		{
// 			if(tlb[i].time<mintime)		//otherwise find the min timestamp
// 			{
// 				mintime=tlb[i].time;
// 				mini=i;
// 			}
// 		}
// 	}

// 	if(found==0)						//if we could not find an empty space, change the min timestamp position
// 	{
// 		tlb[mini].time=timestamp;
// 		tlb[mini].pid=id;
// 		tlb[mini].pageno=pageno;
// 		tlb[mini].frameno=frameno;
// 	}

// }

int main(int argc, char const *argv[]) // Main Function
{
	logfile = open("report.txt", O_WRONLY | O_CREAT | O_TRUNC, 0666); // Open file to stroe output
	write(logfile, "MMU Logs\n", 10);
	signal(SIGUSR2, done); 
	signal(SIGUSR2, done); 
	sleep(1);				   // Induced to show the context switch for better visualisation, otherwise the page access gets completed within 250 ms
	if (argc < 9)
	{
		perror("Invalid Number of Arguments\n");
		exit(1);
	}

	MQ2 = atoi(argv[1]); // Access Arguments
	MQ3 = atoi(argv[2]);
	SM1 = atoi(argv[3]);
	SM2 = atoi(argv[4]);
	ProcessBlock_ID = atoi(argv[5]);
	m = atoi(argv[6]);
	k = atoi(argv[7]);
	s = atoi(argv[8]);

	int i;	

	for (i = 0; i < k; i++)
		fault_frequency[i] = 0; // Page faults for all processes initially 0

	ProcessBlock = (process *)(shmat(ProcessBlock_ID, NULL, 0)); // Attach the various data structures to the shared memory via the id
	PageTable = (PageTableEntry *)(shmat(SM1, NULL, 0));
	FreeFrameList = (FFL *)(shmat(SM2, NULL, 0));

	while (1)
	{
		ServiceMessageRequest(); // Service the various requests received
	}
	return 0;
}
