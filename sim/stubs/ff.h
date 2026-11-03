#ifndef TEST_FF_H
#define TEST_FF_H
#include <stddef.h>
typedef unsigned UINT;
typedef int FRESULT;
typedef struct {unsigned slot,pos;} FIL;
typedef struct {int dummy;} FATFS;
#define FR_OK 0
#define FR_NO_FILE 4
#define FA_READ 1
#define FA_WRITE 2
#define FA_CREATE_ALWAYS 4
FRESULT f_mount(FATFS *,const char *,int);
FRESULT f_open(FIL *,const char *,unsigned);
FRESULT f_close(FIL *);
FRESULT f_read(FIL *,void *,unsigned,UINT *);
FRESULT f_write(FIL *,const void *,unsigned,UINT *);
FRESULT f_sync(FIL *);
unsigned test_size(FIL *);
#define f_size(f) test_size(f)
#endif
