all: process.cpp mmu.cpp master.cpp sched.cpp
	g++ master.cpp -o master -std=c++11
	g++ sched.cpp -o scheduler -std=c++11
	g++ mmu.cpp -o mmu -std=c++11
	g++ process.cpp -o process -std=c++11

clean:
	rm process master scheduler mmu