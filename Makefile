all: process.c mmu.c master.c sched.c
	g++ master.c -o master
	gcc sched.c -o scheduler
	gcc mmu.c -o mmu
	g++ process.c -o process

clean:
	rm process master scheduler mmu