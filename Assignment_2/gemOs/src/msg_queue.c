#include <msg_queue.h>
#include <context.h>
#include <memory.h>
#include <file.h>
#include <lib.h>
#include <entry.h>


/************************************************************************************/
/***************************Do Not Modify below Functions****************************/
/************************************************************************************/

struct msg_queue_info *alloc_msg_queue_info()
{
	struct msg_queue_info *info;
	info = (struct msg_queue_info *)os_page_alloc(OS_DS_REG);
	
	if(!info){
		return NULL;
	}
	return info;
}

void free_msg_queue_info(struct msg_queue_info *q)
{
	os_page_free(OS_DS_REG, q);
}

struct message *alloc_buffer()
{
	struct message *buff;
	buff = (struct message *)os_page_alloc(OS_DS_REG);
	if(!buff)
		return NULL;
	return buff;	
}

void free_msg_queue_buffer(struct message *b)
{
	os_page_free(OS_DS_REG, b);
}

/**********************************************************************************/
/**********************************************************************************/
/**********************************************************************************/
/**********************************************************************************/


int do_create_msg_queue(struct exec_context *ctx)
{
	int fd=0; 
	while(ctx->files[fd] && fd < MAX_OPEN_FILES) fd++;
	if(fd == MAX_OPEN_FILES) return -ENOMEM;

	struct file* mfile = alloc_file();
	mfile->fops = NULL;
	mfile->ref_count = 1;
	struct msg_queue_info* minfo = alloc_msg_queue_info();
	if(minfo == NULL) return -ENOMEM;

	struct message* msg_buff = alloc_buffer();
	minfo -> msg_buffer = msg_buff;
	minfo -> members[0] = ctx->pid;
	for(int i=1; i<MAX_MEMBERS; i++) minfo -> members[i] = -99;
	for(int i=0; i<9; i++) for(int j=0; j<9; j++) minfo->block_log [i][j] = 0;

	mfile->msg_queue = minfo;
	ctx->files[fd] = mfile;
	return fd;
}


int do_msg_queue_rcv(struct exec_context *ctx, struct file *filep, struct message *msg)
{
	if(filep->msg_queue == NULL) return -EINVAL;
	struct msg_queue_info* minfo = filep->msg_queue;
	int is_member = 0;
	for(int i=0; i<MAX_MEMBERS; i++){
		if(minfo->members[i] == ctx->pid) is_member = 1;
	}
	if(!is_member) return -EINVAL;

	int buff_len = 4096/sizeof(minfo->msg_buffer); 

	int found_first = 0;
	for(int i=0; i<buff_len && !found_first; i++){
		struct message m = minfo->msg_buffer[i];
		if(m.to_pid == ctx->pid){
			found_first = 1;
			msg->from_pid = m.from_pid;
			msg->to_pid = m.to_pid;
			for(int i=0; i<MAX_TXT_SIZE; i++){
				msg->msg_txt[i] = m.msg_txt[i];
			}
			for(int j=i+1; !((minfo->msg_buffer[j]).from_pid == 0 && (minfo->msg_buffer[j]).to_pid == 0); j++){
				minfo->msg_buffer[j-1] = minfo->msg_buffer[j];
				minfo->last = j+1;
			}
			return 1;
		}else continue;
	}
	return 0;
}


int do_msg_queue_send(struct exec_context *ctx, struct file *filep, struct message *msg)
{

	if(filep->msg_queue == NULL) return -EINVAL;
	if(msg->from_pid != ctx->pid) return -EINVAL;

	struct msg_queue_info* minfo = filep->msg_queue;
	int is_member = 0;
	int mem_exists = (msg->to_pid == BROADCAST_PID) ? 1 : 0;

	for(int i=0; i<MAX_MEMBERS; i++){
		if(minfo->members[i] == ctx->pid) is_member = 1;
		if(minfo->members[i] == msg->to_pid) mem_exists = 1;
	}
	if(!(is_member == 1 && mem_exists == 1)) return -EINVAL;
	if(msg->to_pid == BROADCAST_PID){
		int num_rcv=0;
		for(int i=0; i<MAX_MEMBERS; i++){
			if(minfo->members[i] == ctx->pid || minfo->members[i] == -99) continue;
			if(minfo->members[i] && !(minfo->block_log[minfo->members[i]][ctx->pid])){
				num_rcv++;
				struct message curr_msg;
				curr_msg.from_pid = msg->from_pid;
				curr_msg.to_pid = minfo->members[i];
				for(int i=0; i<MAX_TXT_SIZE; i++){
					curr_msg.msg_txt[i] = msg->msg_txt[i];
				}
				minfo->msg_buffer[minfo->last++] = curr_msg;
			}
		}
		return num_rcv;
	}else{
		if(minfo->block_log[msg->to_pid][ctx->pid]) return -EINVAL;
		for(int i=0; i<MAX_MEMBERS; i++){
			if(minfo->members[i] == msg->to_pid){
				minfo->msg_buffer[minfo->last++] = *msg;
				break;
			}
		}
		return 1;
	}

	return -EINVAL;
}

void do_add_child_to_msg_queue(struct exec_context *child_ctx)
{
	struct file* f;
	for(int i=0; i<MAX_OPEN_FILES; i++){
		f = child_ctx -> files[i];
		if(f->msg_queue){
			// update the ref_count
			f->ref_count++;

			struct msg_queue_info* minfo = f->msg_queue;
			for(int i=0; i<MAX_MEMBERS; i++){
				if(minfo->members[i] == -99){
					minfo->members[i] = child_ctx->pid;
					break;
				}
			}
		}
	}
}

void do_msg_queue_cleanup(struct exec_context *ctx)
{
	struct msg_queue_info* minfo;
	for(int i=0; i<MAX_OPEN_FILES; i++){
		if(ctx->files[i] != NULL && ((ctx->files[i]->msg_queue) != NULL)){
			// update ref_count for the mq
			(ctx->files[i])->ref_count--;

			// get the current mq
			minfo = (ctx->files[i])->msg_queue;

			// delete all the messages for this process
			int buff_len = 4096/sizeof(minfo->msg_buffer); 
			for(int j=0; j<buff_len; j++){
				struct message m = minfo->msg_buffer[j];
				if(m.to_pid == ctx->pid){
					// shift the queue accordingly
					for(int k=j+1; !((minfo->msg_buffer[k]).from_pid == 0 && (minfo->msg_buffer[k]).to_pid == 0); k++){
						minfo->msg_buffer[k-1] = minfo->msg_buffer[k];
						minfo->last = k+1;
					}
				}
			}
			int rem = 0;
			for(int j=0; j<MAX_MEMBERS; j++){
				if(minfo->members[i] == ctx->pid) minfo->members[i] = -99;
				else if(minfo->members[i] != -99) rem++;
			}
			for(int i=0; i<9; i++){
				minfo->block_log[ctx->pid][i] = 0;
			}
			if(!rem){
				// deallocate the structs
				free_msg_queue_buffer(minfo->msg_buffer);
				free_msg_queue_info(minfo);
				free_file_object(ctx->files[i]);
			}
			ctx->files[i] = NULL;
		}
	}
}

int do_msg_queue_get_member_info(struct exec_context *ctx, struct file *filep, struct msg_queue_member_info *info)
{
	if(filep->msg_queue == NULL) return -EINVAL;
	struct msg_queue_info* minfo = filep->msg_queue;
	int is_member = 0;
	for(int i=0; i<MAX_MEMBERS; i++){
		if(minfo->members[i] == ctx->pid) is_member = 1;
	}
	if(!is_member) return -EINVAL;

	int mem=0, curr=0;
	for(int i=0; i<MAX_MEMBERS; i++){
		if(minfo->members[i] != -99){
			mem++;
			info->member_pid[curr++] = minfo->members[i];
		}
	}
	info->member_count = mem;
	return 0;
}


int do_get_msg_count(struct exec_context *ctx, struct file *filep)
{
	if(filep->msg_queue == NULL) return -EINVAL;
	struct msg_queue_info* minfo = filep->msg_queue;
	int is_member = 0;
	for(int i=0; i<MAX_MEMBERS; i++){
		if(minfo->members[i] == ctx->pid) is_member = 1;
	}
	if(!is_member) return -EINVAL;

	int count=0;

	for(int i=0; i<(minfo->last); i++){
		struct message m = minfo->msg_buffer[i];
		if(m.to_pid == ctx->pid){
			count++;
		}
	}
	return count;
}

int do_msg_queue_block(struct exec_context *ctx, struct file *filep, int pid)
{
	if(filep->msg_queue == NULL) return -EINVAL;
	struct msg_queue_info* minfo = filep->msg_queue;
	int is_member = 0;
	int other_mem = 0;
	for(int i=0; i<MAX_MEMBERS; i++){
		if(minfo->members[i] == ctx->pid) is_member = 1;
		if(minfo->members[i] == pid) other_mem = 1;
	}
	if(!is_member || !other_mem) return -EINVAL;
	minfo->block_log[ctx->pid][pid] = 1;
	return 0;
}

int do_msg_queue_close(struct exec_context *ctx, int fd)
{
	if((ctx->files[fd])->msg_queue == NULL) return -EINVAL;
	struct msg_queue_info* minfo = (ctx->files[fd])->msg_queue;
	int is_member = 0;
	for(int i=0; i<MAX_MEMBERS; i++){
		if(minfo->members[i] == ctx->pid) is_member = 1;
	}
	if(!is_member) return -EINVAL;
	
	// delete all the messages for this process
	int buff_len = 4096/sizeof(minfo->msg_buffer); 
	for(int j=0; j<buff_len; j++){
		struct message m = minfo->msg_buffer[j];
		if(m.to_pid == ctx->pid){
			// shift the queue accordingly
			for(int k=j+1; !((minfo->msg_buffer[k]).from_pid == 0 && (minfo->msg_buffer[k]).to_pid == 0); k++){
				minfo->msg_buffer[k-1] = minfo->msg_buffer[k];
				minfo->last = k+1;
			}
		}
	}

	int rem=0;
	for(int i=0; i<MAX_MEMBERS; i++){
		if(minfo->members[i] == ctx->pid) minfo->members[i] = -99;
		else if(minfo->members[i] != -99) rem++;
	}

	for(int i=0; i<9; i++){
		minfo->block_log[ctx->pid][i] = 0;
	}
	// manage the ref_count for this process
	(ctx->files[fd])->ref_count--;
	if(!rem){
		// deallocate the structs
		free_msg_queue_buffer(minfo->msg_buffer);
		free_msg_queue_info(minfo);
		free_file_object(ctx->files[fd]);
	}
	(ctx->files[fd]) = NULL;
	return 0;
}
