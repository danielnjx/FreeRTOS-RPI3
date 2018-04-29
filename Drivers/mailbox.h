#ifndef _MAILBOX_H_
#define _MAILBOX_H_

void mailboxWrite(int data_addr, int channel);
int mailboxRead(int channel);

#endif
