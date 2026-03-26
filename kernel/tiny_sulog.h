#ifndef _TINY_SULOG_H
#define _TINY_SULOG_H

#include <linux/types.h>
#include <linux/uaccess.h>   /* void __user * */

void sulog_init_heap(void);
void write_sulog(uint8_t sym);
int send_sulog_dump(void __user *uptr);

#endif /* _TINY_SULOG_H */
