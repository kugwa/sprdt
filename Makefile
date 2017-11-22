all: sender receiver agent

sender: sender.o sprdt.o
	gcc -o sender sender.o sprdt.o -pthread

receiver: receiver.o sprdt.o
	gcc -o receiver receiver.o sprdt.o -pthread

agent: b01902054_hw2_agent.c sprdt.h
	gcc -o agent b01902054_hw2_agent.c

sender.o: b01902054_hw2_send.c sprdt.h
	gcc -c -o sender.o b01902054_hw2_send.c

receiver.o: b01902054_hw2_recv.c sprdt.h
	gcc -c -o receiver.o b01902054_hw2_recv.c

sprdt.o: sprdt.c sprdt.h
	gcc -c -o sprdt.o sprdt.c

clean:
	rm -f sender receiver agent *.o
