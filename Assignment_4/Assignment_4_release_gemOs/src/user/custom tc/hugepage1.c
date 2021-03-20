#include<ulib.h>

// munmap with huge pages


int main(u64 arg1, u64 arg2, u64 arg3, u64 arg4, u64 arg5)
{
	u32 MB = 1 << 20;
    u64 aligned = 0x180400000; // 2MB aligned address
	
    char* paddr = mmap((void *)aligned+2*MB, 10*MB, PROT_READ|PROT_WRITE, 0);
    pmap(1); // vm area 1
    // paddr[0] = 'c';
    // // paddr[1] = 'a';
    // paddr[2*MB] = 'd';
    // paddr[2*MB+1] = 'e';
    // char* hpaddr1 = (char *) make_hugepage(paddr, 10*MB , PROT_READ|PROT_WRITE, 0);
    // pmap(1); // vm area 1
    
    char* paddr2 = mmap((void*)aligned, 2*MB, PROT_READ|PROT_WRITE, 0);
    // pmap(1); // vm area 2

    char* hpaddr1 = (char*)make_hugepage(paddr, 10*MB , 3, 1);
    // char* h = mmap(NULL, 2*MB-0x1000, 2, 0);
    // for(int i = 0; i<100; i++){
    //     hpaddr1[i] = (char)('a' + i%26);
    // }
    // hpaddr1[100] = '\0';

    for(int i = 0; i<1000; i++){
        hpaddr1[i] = (char)('a' + i%26);
    }
    hpaddr1[1000] = '\0';
    // hpaddr1[0x1001] = 'z';
    pmap(1); // vm area 2

    // char b= hpaddr2[2*MB];
    // hpaddr1[16] = 'c';
    // printf("VAL %c\n", hpaddr2[0]);
    // pmap(1);
    // munmap((void *)aligned+5*MB+4*0x1000, 0x1000);
    // char* jj = mmap((void*)aligned+10*MB, 2*MB, 3, 0);
    // pmap(1); // vm area 0

    // // int ret = munmap((void *)aligned+6*MB+4*0x1000, 0x1000);
    // // pmap(1);
    // // char* tt = mmap((void*)aligned+2*MB, 2*MB, PROT_READ|PROT_WRITE, 0);
    // // char h1 = (char)make_hugepage(tt, 2*MB, PROT_WRITE|PROT_READ, 1);
    // munmap(h+2*MB-2*0x1000, 10*MB+0x1000+1);
    // mmap(NULL, 0x1000, 3, 0);
    int ret = break_hugepage((void*)hpaddr1, 2*MB);
    // printf("VALR = %s\n", hpaddr1);
    printf("SECOND STR %s\n", (hpaddr1));
    munmap((void*)hpaddr1, 10*MB);
    pmap(1);


    // printf("YAHA PE %d", ret);
}