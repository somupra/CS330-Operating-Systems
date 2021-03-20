#include<ulib.h>


int main(u64 arg1, u64 arg2, u64 arg3, u64 arg4, u64 arg5)
{
    long MB = 0x100000;
    char *addr1 = mmap(NULL, 4096 + 1, PROT_READ, 0);
    if((long)addr1 < 0)
    {
        printf("TEST CASE FAILED\n");
        return 1;
    }printf("1 VMA of size 8k from the start\n");
    pmap(1);

    char * addr2 = mmap(addr1 + 0x1000 * 2, 4*MB, PROT_WRITE, 1);
    addr2[0x1000*3] = 'X';
    pmap(1);
    
    char * haddr1 = make_hugepage(addr1 + 0x1000*2, 4*MB, PROT_WRITE, 0);
    if(addr2[0x1000*3] != 'X'){
        printf("Faild\n"); return 0;
    }
    addr2[2*MB] = 'X';
    pmap(1);
    munmap(addr1, 6*MB);
    return 0;
}
