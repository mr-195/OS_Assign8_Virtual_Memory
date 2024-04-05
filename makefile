
all: 
	gcc  master.c -o master
	gcc  mmu.c -o mmu
	gcc  sched.c -o sched
	gcc  process.c -o process
	./master

clean:
	rm master mmu sched process