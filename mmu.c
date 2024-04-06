#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <limits.h>
#include <sys/shm.h>
#include <stdbool.h>

#define FROM_PROCESS 10				//To send a msg to process
#define TO_PROCESS 20				//To recv a msg from process
#define INVALID_PAGE_REFERENCE -2	//Page reference not valid
#define PAGE_FAULT -1
#define PROCESS_OVER -9				
#define PAGE_FAULT_HANDLED 1		//Type 1 msg
#define TERMINATED 2				//Type 2 msg

int timestamp=0;					//Global timestamp
int fault_freq[10000];			//Frequency of page faults
int fault_freq_index=0;			//Index for the frequency of page faults
FILE *outfile;

typedef struct{						//Page Table Entry has the Frame number, valid/invalid and time of use (timestamp)
	int frame;
	bool valid;
	int time;
}PTentry;

typedef struct{						//To enter necessary information for a process: The id, number of pages, allocated number of frames and used number of frames
	int pid;
	int m;
	int allocount;
	int usecount;
}process;

typedef struct{						//Free Frame List: Stores the size of the list and the free frames available
	int size;
	int ffl[];
}FFL;

// typedef struct{						//Translation Lookaside Buffer Stores the id, pageno, frameno, timestamp (for replacing in TLB) and valid (if something exists in TLB or not)
// 	int pid;
// 	int pageno;
// 	int frameno;
// 	int time;
// 	bool valid;
// }TLB;

//Message Queue Structures

struct MQ3_recvbuf{					//To receive id and pageno from the process via MQ3	
	long mtype;
	int id;
	int pageno;
};

struct MQ3_sendbuf{					//To send frameno to process via MQ3
	long mtype;
	int frameno;
};

struct MQ2buf{						//To send msg to scheduler via MQ2
	long mtype;
	char mbuf[1];
};

int PTid, FFLid;					//ids for various queues and shared memories
int MQ2id, MQ3id;
int PCBid;

process *PCB;						//Structures
PTentry *PT;
FFL *freeFL;
// vector<TLB> tlb;					//TLB

int m,k,s;

void sendFrameNo(int id, int frame)			//send frame number to process specified by id
{
	struct MQ3_sendbuf msg_to_process;
	int length;

	msg_to_process.mtype=TO_PROCESS+id;		//msg to process
	msg_to_process.frameno=frame;
	length=sizeof(struct MQ3_sendbuf)-sizeof(long);

	if(msgsnd(MQ3id,&msg_to_process,length,0)==-1)	//send frame number
	{
		perror("Error in sending message");
		exit(1);
	}
}

void sendMsgToScheduler(int type)			//send type1/type2 msg to scheduler
{
	struct MQ2buf msg_to_scheduler;
	int length;

	msg_to_scheduler.mtype=type;
	length=sizeof(struct MQ2buf)-sizeof(long);

	if(msgsnd(MQ2id,&msg_to_scheduler,length,0)==-1)	//send the msg
	{
		perror("Error in sending message");
		exit(1);
	}
}


int handlePageFault(int id,int pageno)			//handle the page faults
{
	int i,frameno;
	if(freeFL->size==0||PCB[id].usecount>PCB[id].allocount)			//if there is no free frame or if the page has all its allocated number of frames used
	{
		int min=INT_MAX,mini=-1;			//find the frame with the minimum timestamp, specifying the LRU policy
		for(i=0;i<PCB[id].m;i++)
		{
			if(PT[id*m+i].valid==true)
			{
				if(PT[id*m+i].time<min)
				{
					min=PT[id*m+i].time;	//minimum timestamp is found
					mini = i;
				}
			}
		}
		PT[id*m+mini].valid=false;			//that page table entry is made invalid
		frameno=PT[id*m+mini].frame;		//corresponding frame is returned 
	}

	else
	{
		frameno=freeFL->ffl[freeFL->size-1];		//otherwise get a free frame and allot it to the corresponding process
		freeFL->size-=1;
		PCB[id].usecount++;
	}
	return frameno;
}

void freeFrames(int id)					//When a process is over/terminated, free all the frames allotted to it
{
	int i= 0;
	for(i= 0;i<PCB[i].m;i++)
	{
		if(PT[id*m+i].valid==true)
		{
			freeFL->ffl[freeFL->size]=PT[id*m+i].frame;		//add the frame to FFL
			freeFL->size += 1;								//increase the size 
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
void serviceMessageRequest()			//Service message requests
{
	int id,pageno,length,frameno,i,found;
	int mintime,mini;
	struct MQ3_recvbuf msg_from_process;
	struct MQ3_sendbuf msg_to_process;
	length=sizeof(struct MQ3_recvbuf)-sizeof(long);
	if(msgrcv(MQ3id,&msg_from_process,length,FROM_PROCESS,0)==-1)		//Receive a msg from the process
	{
		perror("Error in receiving message");
		exit(1);
	}
	id=msg_from_process.id;
	pageno=msg_from_process.pageno;				//Retrieve the process id and page number requested

	if (pageno==PROCESS_OVER)					//if -9 is received, free frames and send type 2 msg to scheduler
	{
		freeFrames(id);
		sendMsgToScheduler(TERMINATED);
		return;
	}

	timestamp++;								//Increase the timestamp
	// printf("Page reference: ("<<timestamp<<", "<<id<<", "<<pageno<<")\n";
	printf("Page reference: (%d, %d, %d)\n",timestamp,id,pageno);
	fprintf(outfile,"Page reference: (%d, %d, %d)\n",timestamp,id,pageno);

	if (pageno>PCB[id].m||pageno<0)				//If we refer to an invalid page number
	{
		// cout<<"Invalid Page Reference: ("<<id<<", "<<pageno<<")\n";
		printf("Invalid Page Reference: (%d, %d)\n",id,pageno);
		fprintf(outfile,"Invalid Page Reference: (%d, %d)\n",id,pageno);
		
		sendFrameNo(id,INVALID_PAGE_REFERENCE);	//Send invalid reference to process

		freeFrames(id);							//Free frames and terminate the process
		sendMsgToScheduler(TERMINATED);
	}

	else 							//if a valid page numeber is used
	{
		// for(i=0;i<s;i++)			//Go through TLB in SET ASSOCIATIVE manner (here assume it is shown sequentially)
		// {
		// 	if(tlb[i].valid==true&&tlb[i].pid==id&&tlb[i].pageno==pageno)
		// 	{
		// 		tlb[i].time=timestamp;			//if found in TLB
		// 		cout<<"Found in TLB\n";
		// 		sendFrameNo(id,tlb[i].frameno);
		// 		return;
		// 	}
		// }

		if(PT[id*m+pageno].valid==true)			//if found in page table but not in TLB
		{
			frameno=PT[id*m+pageno].frame;

			// updateTLB(id,pageno,frameno);		//update TLB and return frame number
			sendFrameNo(id,frameno);
			PT[id*m+pageno].time=timestamp;
		}
		else
		{
			// cout<<"Page Fault: ("<<id<<", "<<pageno<<")\n";
			printf("Page Fault: (%d, %d)\n",id,pageno);
			fprintf(outfile,"Page Fault: (%d, %d)\n",id,pageno);
			fault_freq[id]+=1;
			sendFrameNo(id,PAGE_FAULT);			//otherwise we get a page fault, we handle the page fault, update TLB and PT
			frameno=handlePageFault(id,pageno);
			// updateTLB(id,pageno,frameno);
			PT[id*m+pageno].valid=true;
			PT[id*m+pageno].time=timestamp;
			PT[id*m+pageno].frame=frameno;
			sendMsgToScheduler(PAGE_FAULT_HANDLED);		//tell scheduler that page fault is handled
		}
	}	
}

void complete(int signo)			//Signal Handler for SIGUSR1
{
	int i;
	if(signo==SIGUSR2) 
	{

		printf("Frequency of Page Faults for Each Process:\n");	
		fprintf(outfile,"Frequency of Page Faults for Each Process:\n");
		printf("PID\tFrequency\n");
		fprintf(outfile,"PID\tFrequency\n");
		for(i=0;i<k;i++)
		{	
			// cout<<i<<"\t"<<fault_freq[i]<<endl;
			printf("%d\t%d\n",i,fault_freq[i]);
			fprintf(outfile,"%d\t%d\n",i,fault_freq[i]);
		}

		shmdt(PCB);					//Detach various shared memory segments
		shmdt(PT);
		shmdt(freeFL);
		fclose(outfile);			//close the file
		exit(0); 
	}
}

int main(int argc, char const *argv[])			//Main Function
{
	signal(SIGUSR2,complete);					//Install Signal Handler to get the signals
	sleep(1);									//Just to show the context switch for better visualisation, otherwise the page access gets completed within 250 ms
	if (argc<9)
	{
		perror("Invalid Number of Arguments\n");
		exit(1);
	}

	MQ2id=atoi(argv[1]);						//Get various ids and other parameters
	MQ3id=atoi(argv[2]);
	PTid=atoi(argv[3]);
	FFLid=atoi(argv[4]);
	PCBid=atoi(argv[5]);
	m=atoi(argv[6]);
	k=atoi(argv[7]);
	s=atoi(argv[8]);

	int i;
	
	// tlb.resize(s);								//Make a TLB of size s with all initial elements as false
	// for(i=0;i<s;i++) tlb[i].valid=false;

	for(i=0;i<k;i++) fault_freq[i]=0;	//Page faults for all processes initially 0
	
	PCB=(process *)(shmat(PCBid,NULL,0));		//Attach the various data structures to the shared memory via the id
	PT=(PTentry *)(shmat(PTid,NULL,0));
	freeFL=(FFL *)(shmat(FFLid,NULL,0));

	outfile=fopen("a.txt","w");
	while(1)
	{
		serviceMessageRequest();				//Service the various requests received
	}
	return 0;
}
