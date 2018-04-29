//mailbox.c
//authored by Jared Hull
//
//These functions are used for communications between the CPU and GPU

#include <mailbox.h>

//direct memory get and set
extern void PUT32(int dest, int src);
extern int GET32(int src);

//Docuentation on the mailbox functions
//mailbuffer should probably be 16 byte aligned (for gpu at least):
//unsigned int mailbuffer[22] __attribute__((aligned (16)));
//https://github.com/raspberrypi/firmware/wiki/Mailbox-property-interface
void mailboxWrite(int data_addr, int channel){
	int mailbox = 0x3f00B880;
	while(1){
		if((GET32(mailbox + 0x18)&0x80000000) == 0) break;
	}
	PUT32(mailbox + 0x20, data_addr + channel);
	return;
}

int mailboxRead(int channel){
	int ra;
	int mailbox = 0x3f00B880;
	while(1){
		while(1){
			ra = GET32(mailbox + 0x18);
			if((ra&0x40000000) == 0) break;
		}
		ra = GET32(mailbox + 0x00);
		if((ra&0xF) == channel) break;
	}
	return(ra);
}