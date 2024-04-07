#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include <signal.h>
#include <string.h>
#include <time.h>
#include <sys/msg.h>
#include <sys/ipc.h>
#include <sys/types.h>
#include <sys/shm.h>

#define PROCESS_OVER -9
#define TO_SCHEDULER 10
#define FROM_SCHEDULER 20 
#define TO_MMU 10
#define FROM_MMU 20 
#define PAGE_FAULT -1
#define INVALID_PAGE_REFERENCE -2

int len = 0;
int pages[100];

typedef struct MQ3_send_buffer{			//Send message to MMU via MQ3
	long mtype;         	
	int id;
	int pageno;
}MQ3_send_buffer;

typedef struct MQ3_recv_buffer{			//Receive message from MMU via MQ3
	long mtype;          
	int frameno;
}MQ3_recv_buffer;

struct MQ1_buffer{				//Send/recv message to scheduler via MQ1
	long mtype;         
	int id;
};

int main(int argc, char *argv[])
{
	if (argc<4)
	{
		perror("Invalid Number of Arguments\n");		
		exit(1);
	}

	int pid,MQ1, MQ3;
	int i;
	pid = atoi(argv[1]);			//get various ids
	MQ1 = atoi(argv[2]);
	MQ3 = atoi(argv[3]);
	
	char *tok;
	tok=strtok(argv[4],"  ");
	while(tok!=NULL)
	{		
		pages[len++] = atoi(tok); 
		tok=strtok(NULL,"  ");
	}
	
	printf("Process id = %d\n",pid);

	struct MQ1_buffer message_to_scheduler;
	message_to_scheduler.mtype=TO_SCHEDULER;
	message_to_scheduler.id=pid;
	int length=sizeof(struct MQ1_buffer)-sizeof(long);
	if (msgsnd(MQ1,&message_to_scheduler,length,0)==-1)		//Send the id to scheduler via MQ1 i.e ready queue enqueue is done
	{
		perror("Error in sending message to scheduler");
		exit(1);
	}
	
	struct MQ1_buffer message_from_scheduler;						//Receive message from scheduler (just done as a standard to wake up process)
	length=sizeof(struct MQ1_buffer)-sizeof(long);

	if (msgrcv(MQ1,&message_from_scheduler,length,FROM_SCHEDULER+pid,0)==-1)		
	{
		perror("Error in receiving message");
		exit(1);
	}

	MQ3_send_buffer message_to_mmu;
	MQ3_recv_buffer message_from_mmu;
	
	for(i=0;i<len;i++)
	{		
		printf("Sending request for Page %d.\n",pages[i]);
		message_to_mmu.mtype=TO_MMU;
		message_to_mmu.id=pid;
		message_to_mmu.pageno=pages[i];
		length=sizeof(struct MQ3_send_buffer)-sizeof(long);
		if(msgsnd(MQ3,&message_to_mmu,length,0)==-1)
			{
				perror("Error in sending message");
				exit(1);
			}

		length=sizeof(struct MQ3_recv_buffer)-sizeof(long);
		if(msgrcv(MQ3,&message_from_mmu,length,FROM_MMU+pid,0)==-1)		
		{
			perror("Error in receiving message");
			exit(1);
		}

		if(message_from_mmu.frameno>=0)					
		{
			printf("MMU responded with frame number for process %d: %d\n",pid,message_from_mmu.frameno);			
			i++;
		}
		else if(message_from_mmu.frameno==PAGE_FAULT) 		
		{
			printf("Page Fault detected for process %d\n",pid);			
			length=sizeof(struct MQ1_buffer)-sizeof(long);

			if (msgrcv(MQ1,&message_from_scheduler,length,FROM_SCHEDULER+pid,0)==-1)
			{
				perror("Error in receiving message");
				exit(1);
			}
		}
		else if (message_from_mmu.frameno==INVALID_PAGE_REFERENCE)		
		{
			printf("Invalid Page Reference for Process %d. Terminating the Process...\n",pid);			
			exit(1);
		}
	}
	
	printf("Process %d completed succcesfully\n",pid);	
	message_to_mmu.pageno=PROCESS_OVER;
	message_to_mmu.id=pid;
	message_to_mmu.mtype=TO_MMU;
	length=sizeof(struct MQ3_send_buffer)-sizeof(long);
	if (msgsnd(MQ3,&message_to_mmu,length,0)==-1)	
		{
			perror("Error in sending message");
			exit(1);
		}
	return 0;
}


