#include <debug.h>
#include <context.h>
#include <entry.h>
#include <lib.h>
#include <memory.h>


/*****************************HELPERS******************************************/

/* 
 * allocate the struct which contains information about debugger
 *
 */
struct debug_info *alloc_debug_info()
{
	struct debug_info *info = (struct debug_info *) os_alloc(sizeof(struct debug_info));
	if(info)
		bzero((char *)info, sizeof(struct debug_info));
	return info;
}

/*
 * frees a debug_info struct 
 */
void free_debug_info(struct debug_info *ptr)
{
	if(ptr)
		os_free((void *)ptr, sizeof(struct debug_info));
}

/*
 * allocates memory to store registers structure
 */
struct registers *alloc_regs()
{
	struct registers *info = (struct registers*) os_alloc(sizeof(struct registers)); 
	if(info)
		bzero((char *)info, sizeof(struct registers));
	return info;
}

/*
 * frees an allocated registers struct
 */
void free_regs(struct registers *ptr)
{
	if(ptr)
		os_free((void *)ptr, sizeof(struct registers));
}

/* 
 * allocate a node for breakpoint list 
 * which contains information about breakpoint
 */
struct breakpoint_info *alloc_breakpoint_info()
{
	struct breakpoint_info *info = (struct breakpoint_info *)os_alloc(
		sizeof(struct breakpoint_info));
	if(info)
		bzero((char *)info, sizeof(struct breakpoint_info));
	return info;
}

/*
 * frees a node of breakpoint list
 */
void free_breakpoint_info(struct breakpoint_info *ptr)
{
	if(ptr)
		os_free((void *)ptr, sizeof(struct breakpoint_info));
}

/*
 * Fork handler.
 * The child context doesnt need the debug info
 * Set it to NULL
 * The child must go to sleep( ie move to WAIT state)
 * It will be made ready when the debugger calls wait_and_continue
 */
void debugger_on_fork(struct exec_context *child_ctx)
{
	child_ctx->dbg = NULL;	
	child_ctx->state = WAITING;	
}


/******************************************************************************/

/* This is the int 0x3 handler
 * Hit from the childs context
 */
long int3_handler(struct exec_context *ctx)
{
	if(ctx == NULL) return -1;
	if(ctx->dbg != NULL) return -1;
	
	u32 ppid = ctx->ppid;
	struct exec_context* parent_ctx = get_ctx_by_pid(ppid);
	if(parent_ctx == NULL) return -1;

	u64 current_addr = (ctx->regs.entry_rip)-1;
	struct breakpoint_info* head=NULL;
	head = parent_ctx->dbg->head;
	int found = 0;
	while(head != NULL){
		if((head->addr) == current_addr){
			found = 1;
			break;
		}
		head = head->next;
	}
	
	if(found){
		// [instr a] set it to PUSHRBP_OPCODE and b to INT3_OPCODE, store b in extra_bp_info
		*((u8*)current_addr) = PUSHRBP_OPCODE;
		u8 b_code = *((u8*)(ctx->regs.entry_rip));
		*((u8*)(ctx->regs.entry_rip)) = INT3_OPCODE;

		// get the custom_bp
		struct custom_bp* extra_bp_info=NULL;
		struct debug_info* dbg_info = parent_ctx->dbg;
		for(int i=0; i<MAX_BREAKPOINTS; i++){
			if(dbg_info->custom_bp_info[i].addr == current_addr){
				extra_bp_info = &(dbg_info->custom_bp_info[i]);
				break;
			}
		}
		// error check
		if(extra_bp_info == NULL) return -1;

		// set b_code
		extra_bp_info->op_code_b = b_code;

		// before scheduling the process, get rip to point to a
		ctx->regs.entry_rip -= 1;

		// put value of addr in rax
		parent_ctx->regs.rax = (u64)current_addr;

		// set backtrace info
		u64* bt_arr = dbg_info->backtrace; 
		dbg_info->bt_overflow=0;
		dbg_info->num_bt=0;

		if(MAX_BACKTRACE >= 2){
			bt_arr[0] = ctx->regs.entry_rip;
			bt_arr[1] = *((u64*)ctx->regs.entry_rsp);
			u64* head = (u64*)ctx->regs.rbp;
			int i=2;
			u64 curr_addr;
			do{
				curr_addr = *(head+1);
				if(i == MAX_BACKTRACE){
					if(curr_addr != END_ADDR) dbg_info->bt_overflow = 1;
					else dbg_info->num_bt = i;
					break;
				};
				if(END_ADDR != curr_addr){
					bt_arr[i] = curr_addr; 
					head = (u64*)(*head);
					i++;
				}
			} while(END_ADDR != curr_addr);
			if(dbg_info->bt_overflow == 0) dbg_info->num_bt = i;

		}else{
			dbg_info->bt_overflow=1;
		}
		
		// after all this setting, schedule the debugger
		parent_ctx->state = READY;
		ctx->state = WAITING;
		schedule(parent_ctx);
		return 0;

	}else{
		// [instr b] set it to b_code and a to INT3_OPCODE again
		// get the custom_bp
		struct custom_bp* extra_bp_info=NULL;
		struct debug_info* dbg_info = parent_ctx->dbg;
		for(int i=0; i<MAX_BREAKPOINTS; i++){
			if(dbg_info->custom_bp_info[i].addr == (current_addr - (u64)1) && (dbg_info->removed == 0)){
				extra_bp_info = &(dbg_info->custom_bp_info[i]);
				dbg_info->removed =0;
				break;
			}
		}
		// error check
		if(extra_bp_info == NULL) return -1;

		u8 b_code = extra_bp_info->op_code_b;
		u8* target = (u8*)current_addr;
		*target = b_code;	// set bcode for current addr
		target -= 1;	

		struct breakpoint_info* bp_ptr=dbg_info->head;
		while(bp_ptr != NULL){
			if(bp_ptr->addr == (u64)target && bp_ptr->status == 1){
				// printk("setting opcode\n");
				*target = INT3_OPCODE; // set a to int again
			}
			bp_ptr = bp_ptr->next;
		}
		
		
		// before scheduling the process, get rip to point to a
		ctx->regs.entry_rip -= 1;
		
		// after all this setting, schedule the child
		parent_ctx->state = WAITING;
		ctx->state = READY;
		schedule(ctx);
	}	
	return 0;
}

/*
 * Exit handler.
 * Called on exit of Debugger and Debuggee 
 */
void debugger_on_exit(struct exec_context *ctx)
{
	// set flags in the other struct
	if(ctx->dbg == NULL){
		// child
		u32 ppid = ctx->ppid;
		struct exec_context* debugger = get_ctx_by_pid(ppid);
		debugger->dbg->child_exited = 1;
		debugger->regs.rax = CHILD_EXIT;
		debugger->state = READY;

	}else{
		// destroy the structs
		struct breakpoint_info* bp_ptr = ctx->dbg->head;
		struct breakpoint_info* to_remove;

		while(bp_ptr!=NULL){
			to_remove = bp_ptr;
			bp_ptr = bp_ptr->next;
			free_breakpoint_info(to_remove);	
		}
		free_debug_info(ctx->dbg);
	}
}

/*
 * called from debuggers context
 * initializes debugger state
 */
int do_become_debugger(struct exec_context *ctx)
{
	struct debug_info* dbg_info = alloc_debug_info();
	if(dbg_info == NULL) {
		// printk("error in initializing debug_info\n");
		return -1;
	}
	dbg_info -> last_bp = 0;
	dbg_info -> child_exited = 0;
	dbg_info->bt_overflow = 0;
	
	dbg_info->num_bt = 0;
	for(int j=0; j<MAX_BACKTRACE; j++){
		dbg_info->backtrace[j]=0;	
	}

	for(int i=0; i<MAX_BREAKPOINTS; i++){
		// initializing all infos to special 0, 0, 0
		dbg_info->custom_bp_info[i].num=0;
		dbg_info->custom_bp_info[i].op_code_a=0;
		dbg_info->custom_bp_info[i].op_code_b=0;
		dbg_info->custom_bp_info[i].addr = 0;
	}
	ctx -> dbg = dbg_info;
	return 0;
}

/*
 * called from debuggers context
 */
int do_set_breakpoint(struct exec_context *ctx, void *addr)
{
	if(ctx==NULL) return -1;
	if(ctx->dbg == NULL) return -1;
	struct debug_info* dbg_info = ctx -> dbg;
	struct breakpoint_info * bp_ptr = dbg_info->head;
	struct breakpoint_info * last_bp_node = NULL;

	// if bps exceeded or not
	int num_bp = 0;
	while(bp_ptr != NULL) {
		num_bp++;
		last_bp_node = bp_ptr;
		bp_ptr = bp_ptr -> next;
	}
	if(num_bp >= MAX_BREAKPOINTS) return -1;
	
	// check if bp for addr already exists
	bp_ptr = dbg_info->head;
	while(bp_ptr != NULL) {
		if(bp_ptr->addr == (u64)addr){
			bp_ptr -> status = 1;
			return 0;
		}
		bp_ptr = bp_ptr -> next;
	}

	// create bp
	struct breakpoint_info * bp_info = alloc_breakpoint_info();
	if(bp_info == NULL) return -1;
	
	bp_info -> addr = (u64)addr;
	bp_info -> next = NULL;
	bp_info -> num = ++(dbg_info->last_bp);
	bp_info -> status = 1;

	if(last_bp_node != NULL) last_bp_node -> next = bp_info;
	else dbg_info->head = bp_info;

	// set info for this bp
	for(int i=0; i<MAX_BREAKPOINTS; i++){
		if(dbg_info->custom_bp_info[i].num == 0){
			dbg_info->custom_bp_info[i].num = dbg_info->last_bp;
			dbg_info->custom_bp_info[i].addr = bp_info->addr;
			dbg_info->custom_bp_info[i].op_code_a = *((u8*)addr);
			dbg_info->custom_bp_info[i].op_code_b = *((u8*)addr + 1);
			break;
		}
	}
	
	// set the addr value to 0xcc
	u8* target = (u8*) addr;
	if(*(target + 1) != INT3_OPCODE){
		// printk("added\n");
		*target = INT3_OPCODE;
		dbg_info->removed = 1;
		return 0;
	}
	// printk("not added\n");
	return 0;
}

/*
 * called from debuggers context
 */
int do_remove_breakpoint(struct exec_context *ctx, void *addr)
{
	if(ctx==NULL) return -1;
	if(ctx->dbg == NULL) return -1;

	struct debug_info* dbg_info = ctx->dbg;
	if(dbg_info == NULL) return -1;
	struct breakpoint_info* bp_ptr = dbg_info->head;
	
	// check if head is to be removed
	if(bp_ptr->addr == (u64)addr){
		u32 num = bp_ptr->num;
		*((u8*)addr) = PUSHRBP_OPCODE;

		// get the custom_bp for this bp num and initialize it to zero
		for(int i=0; i<MAX_BREAKPOINTS; i++){
			if(dbg_info->custom_bp_info[i].num == num){
				dbg_info->custom_bp_info[i].num = 0;
				dbg_info->custom_bp_info[i].addr = 0;
				dbg_info->custom_bp_info[i].op_code_a = 0;
				dbg_info->custom_bp_info[i].op_code_b = 0;
				break;
			}
		}
		// manage links
		dbg_info->head = bp_ptr->next;

		// free the breakpoint info
		free_breakpoint_info(bp_ptr);
		return 0;
	}

	// check if bp for addr already exists
	bp_ptr = dbg_info->head;
	while(bp_ptr->next != NULL) {
		if(bp_ptr->next->addr == (u64)addr){
			// set instruction
			*((u8*)addr) = PUSHRBP_OPCODE;

			// remove this bp
			struct breakpoint_info * to_remove = bp_ptr->next;
			bp_ptr->next = to_remove->next; 	// manage links
			
			// get the custom_bp for this bp num and initialize it to zero
			u32 num = to_remove->num;
			for(int i=0; i<MAX_BREAKPOINTS; i++){
				if(dbg_info->custom_bp_info[i].num == num){
					dbg_info->custom_bp_info[i].num = 0;
					dbg_info->custom_bp_info[i].addr = 0;
					dbg_info->custom_bp_info[i].op_code_a = 0;
					dbg_info->custom_bp_info[i].op_code_b = 0;
					break;
				}
			}

			// free structs
			free_breakpoint_info(to_remove);
			printk("removed\n");	
			return 0;
		}
		bp_ptr = bp_ptr -> next;
	}	
	return -1;
}

/*
 * called from debuggers context
 */
int do_enable_breakpoint(struct exec_context *ctx, void *addr)
{
	if(ctx==NULL) return -1;
	if(ctx->dbg == NULL) return -1;

	// set value of status
	struct debug_info* dbg_info = ctx->dbg;
	struct breakpoint_info* bp_ptr = dbg_info->head;
	while(bp_ptr != NULL){
		if(bp_ptr->addr == (u64)addr){
			// bp found
			// set instruction
			*((u8*)addr) = INT3_OPCODE;
			bp_ptr->status = 1;
			return 0;
		}
		bp_ptr = bp_ptr->next;
	}
	return -1;
}

/*
 * called from debuggers context
 */
int do_disable_breakpoint(struct exec_context *ctx, void *addr)
{
	if(ctx==NULL) return -1;
	if(ctx->dbg == NULL) return -1;

	// set value of status
	struct debug_info* dbg_info = ctx->dbg;
	struct breakpoint_info* bp_ptr = dbg_info->head;
	while(bp_ptr != NULL){
		if(bp_ptr->addr == (u64)addr){
			// bp found
			// set instruction
			*((u8*)addr) = PUSHRBP_OPCODE;
			bp_ptr->status = 0;
			return 0;
		}
		bp_ptr = bp_ptr->next;
	}
	return -1;
}

/*
 * called from debuggers context
 */ 
int do_info_breakpoints(struct exec_context *ctx, struct breakpoint *ubp)
{
	if(ctx==NULL) return -1;
	if(ctx->dbg == NULL) return -1;
	struct debug_info* dbg_info = ctx->dbg;
	struct breakpoint_info* bp_ptr = dbg_info->head;
	int count=0;
	while(bp_ptr != NULL){
		ubp[count].addr = bp_ptr->addr;
		ubp[count].num = bp_ptr->num;
		ubp[count].status = bp_ptr->status;
		count++;
		bp_ptr = bp_ptr->next;
	}
	return count;
}

/*
 * called from debuggers context
 */
int do_info_registers(struct exec_context *ctx, struct registers *regs)
{
	if(ctx==NULL) return -1;
	if(ctx->dbg == NULL) return -1;
	struct exec_context* child_ctx = NULL;
	for(u32 i=1; i<=MAX_PROCESSES; i++){
		child_ctx = get_ctx_by_pid(i);
		if(child_ctx != NULL){
			if(child_ctx->ppid == ctx->pid){
				// found the child
				regs->entry_cs = child_ctx->regs.entry_cs;
				regs->entry_rflags = child_ctx->regs.entry_rflags;
				regs->entry_rip = child_ctx->regs.entry_rip;
				regs->entry_rsp = child_ctx->regs.entry_rsp;
				regs->entry_ss = child_ctx->regs.entry_ss;
				regs->r10 = child_ctx->regs.r10;
				regs->r11 = child_ctx->regs.r11;
				regs->r12 = child_ctx->regs.r12;
				regs->r13 = child_ctx->regs.r13;
				regs->r14 = child_ctx->regs.r14;
				regs->r15 = child_ctx->regs.r15;
				regs->r8 = child_ctx->regs.r8;
				regs->r9 = child_ctx->regs.r9;
				regs->rax = child_ctx->regs.rax;
				regs->rbp = child_ctx->regs.rbp;
				regs->rbx = child_ctx->regs.rbx;
				regs->rcx = child_ctx->regs.rcx;
				regs->rdi = child_ctx->regs.rdi;
				regs->rdx = child_ctx->regs.rdx;
				regs->rsi = child_ctx->regs.rsi;


				return 0;
			}
		}
	}
	
	return -1;
}

/* 
 * Called from debuggers context
 */
int do_backtrace(struct exec_context *ctx, u64 bt_buf)
{
	if(ctx==NULL) return -1;
	if(ctx->dbg == NULL) return -1;
	struct exec_context* child_ctx = NULL;
	
	for(u32 i=1; i<=MAX_PROCESSES; i++){
		child_ctx = get_ctx_by_pid(i);
		if(child_ctx != NULL){
			if(child_ctx->ppid == ctx->pid){
				break;
			}
		}
	}
	if(child_ctx == NULL) return -1;

	u64* bt_arr = (u64*)bt_buf; 
	
	if(ctx->dbg->bt_overflow == 1) return -1;
	for(int i=0; i<(ctx->dbg->num_bt); i++){
		bt_arr[i] = ctx->dbg->backtrace[i];
	}

	return ctx->dbg->num_bt;
}


/*
 * When the debugger calls wait
 * it must move to WAITING state 
 * and its child must move to READY state
 */
s64 do_wait_and_continue(struct exec_context *ctx)
{
	printk("came\n");
	if(ctx==NULL) return -1;
	if(ctx->dbg == NULL) return -1;
	// schedule out the debugger and schedule in the debugee
	if(ctx->dbg->child_exited){
		// printk("returning child exit\n");
		return CHILD_EXIT;
	}
	struct exec_context* child_ctx = NULL;
	for(u32 i=1; i<=MAX_PROCESSES; i++){
		child_ctx = get_ctx_by_pid(i);
		if(child_ctx != NULL){
			if(child_ctx->ppid == ctx->pid){
				// found the child
				// *((u8*)(child_ctx->regs.entry_rip)) = PUSHRBP_OPCODE;
				ctx->state = WAITING;
				child_ctx->state = READY;
				printk("sch\n");
				schedule(child_ctx);
			}
		}
	}
	return CHILD_EXIT;
}

