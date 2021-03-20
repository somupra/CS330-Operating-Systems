#include<ulib.h>

// munmap with normal and huge pages


int main(u64 arg1, u64 arg2, u64 arg3, u64 arg4, u64 arg5)
{
	u32 MB = 1 << 20;
    u32 KB4 = 1 << 12; 
    u64 aligned = 0x180400000; // 2MB aligned address
	
    char* paddr = mmap((void*)aligned + KB4, 2*KB4, PROT_READ|PROT_WRITE, 0);
    pmap(1); // vm area 1

    char* paddr1 = mmap((void*)aligned + 3*KB4, 5*KB4, PROT_WRITE, 0);
    pmap(1); // vm area 1

    munmap((void*)aligned , 4*KB4);

    pmap(1); // vm area 0


}