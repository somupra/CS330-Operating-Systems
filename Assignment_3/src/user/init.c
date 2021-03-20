#include <ulib.h>


void fn_1(){
	printf("fn_1\n");
}
void print_regs(struct registers reg_info){
    printf("Registers:\n");
    printf("r15: %x\n", reg_info.r15);
    printf("r14: %x\n", reg_info.r14);
    printf("r13: %x\n", reg_info.r13);
    printf("r12: %x\n", reg_info.r12);
    printf("r11: %x\n", reg_info.r11);
    printf("r10: %x\n", reg_info.r10);
    printf("r9: %x\n", reg_info.r9);
    printf("r8: %x\n", reg_info.r8);
    printf("rbp: %x\n", reg_info.rbp);
    printf("rdi: %x\n", reg_info.rdi);
    printf("rsi: %x\n", reg_info.rsi);
    printf("rdx: %x\n", reg_info.rdx);
    printf("rcx: %x\n", reg_info.rcx);
    printf("rbx: %x\n", reg_info.rbx);
    printf("rax: %x\n", reg_info.rax);
    printf("entry_rip: %x\n", reg_info.entry_rip);
    printf("entry_cs: %x\n", reg_info.entry_cs);
    printf("entry_rflags: %x\n", reg_info.entry_rflags);
    printf("entry_rsp: %x\n", reg_info.entry_rsp);
    printf("entry_ss: %x\n", reg_info.entry_ss);
}

int main(u64 arg1, u64 arg2, u64 arg3, u64 arg4, u64 arg5)
{
    int cpid;
    long ret = 0;
    int i, bt_count;
    unsigned long long bt_info[MAX_BACKTRACE];

    ret = become_debugger();

    cpid = fork();

    if (cpid < 0)
    {
        printf("Error in fork\n");
    }
    else if (cpid == 0)
    {
		fn_1();
    }
    else
    {
        set_breakpoint(fn_1);
		s64 ret1;
		while(ret1 = wait_and_continue()){
			printf("breakpoint at %x\n", ret1);
			printf("remove breakpoint ret %d\n", remove_breakpoint(fn_1));
			printf("remove breakpoint ret %d\n", set_breakpoint(fn_1));
		}
    }

    return 0;
}