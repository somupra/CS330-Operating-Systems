#include<ulib.h>


int main(u64 arg1, u64 arg2, u64 arg3, u64 arg4, u64 arg5)
{
  
    char *addr1 = mmap(NULL, 4096 + 1, PROT_READ, 0);
    if((long)addr1 < 0)
    {
        printf("TEST CASE FAILED\n");
        return 1;
    }printf("1 VMA of size 8k from the start\n");
    pmap(1);

    char * addr2 = mmap(addr1 + 0x1000 * 6, 0x1000*2, PROT_WRITE, 1);
    pmap(1);

    char * addr3 = mmap(addr1 + 0x1000 * 6 - 500, 0x1000*4, PROT_READ, 0);
    pmap(1);
    
	int ret = munmap(addr1 + 0x1000, 0x1000*2);
	pmap(1);
	ret = munmap(addr1 + 0x1000 * 5, 0x1000*2);
	pmap(1);
	mmap(addr1 + 0x1000*8, 0x1000, PROT_WRITE, 1);
	ret = munmap(addr1 + 0x1000*4, 0x1000*4);
	pmap(1);
	// *addr1 = '\0';
	return 0;
}
