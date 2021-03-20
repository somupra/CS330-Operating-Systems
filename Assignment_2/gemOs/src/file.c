#include<types.h>
#include<context.h>
#include<file.h>
#include<lib.h>
#include<serial.h>
#include<entry.h>
#include<memory.h>
#include<fs.h>
#include<kbd.h>


/************************************************************************************/
/***************************Do Not Modify below Functions****************************/
/************************************************************************************/

void free_file_object(struct file *filep)
{
	if(filep)
	{
		os_page_free(OS_DS_REG ,filep);
		stats->file_objects--;
	}
}

struct file *alloc_file()
{
	struct file *file = (struct file *) os_page_alloc(OS_DS_REG); 
	file->fops = (struct fileops *) (file + sizeof(struct file)); 
	bzero((char *)file->fops, sizeof(struct fileops));
	file->ref_count = 1;
	file->offp = 0;
	stats->file_objects++;
	return file; 
}

void *alloc_memory_buffer()
{
	return os_page_alloc(OS_DS_REG); 
}

void free_memory_buffer(void *ptr)
{
	os_page_free(OS_DS_REG, ptr);
}

/* STDIN,STDOUT and STDERR Handlers */

/* read call corresponding to stdin */

static int do_read_kbd(struct file* filep, char * buff, u32 count)
{
	kbd_read(buff);
	return 1;
}

/* write call corresponding to stdout */

static int do_write_console(struct file* filep, char * buff, u32 count)
{
	struct exec_context *current = get_current_ctx();
	return do_write(current, (u64)buff, (u64)count);
}

long std_close(struct file *filep)
{
	filep->ref_count--;
	if(!filep->ref_count)
		free_file_object(filep);
	return 0;
}
struct file *create_standard_IO(int type)
{
	struct file *filep = alloc_file();
	filep->type = type;
	if(type == STDIN)
		filep->mode = O_READ;
	else
		filep->mode = O_WRITE;
	if(type == STDIN){
		filep->fops->read = do_read_kbd;
	}else{
		filep->fops->write = do_write_console;
	}
	filep->fops->close = std_close;
	return filep;
}

int open_standard_IO(struct exec_context *ctx, int type)
{
	int fd = type;
	struct file *filep = ctx->files[type];
	if(!filep){
		filep = create_standard_IO(type);
	}else{
		filep->ref_count++;
		fd = 3;
		while(ctx->files[fd])
			fd++; 
	}
	ctx->files[fd] = filep;
	return fd;
}
/**********************************************************************************/
/**********************************************************************************/
/**********************************************************************************/
/**********************************************************************************/

/* File exit handler */
void do_file_exit(struct exec_context *ctx)
{
	/*TODO the process is exiting. Adjust the refcount
	of files*/
	struct file* filep;
	for(int i=0; i<MAX_OPEN_FILES; i++){
		filep = (ctx->files)[i];
		(filep->ref_count)--;
		if(filep->ref_count == 0){
			// drop the object, this was the last connection
			free_file_object(filep);
			(ctx->files)[i] = NULL;
		}
	}
}

/*Regular file handlers to be written as part of the assignmemnt*/


static int do_read_regular(struct file *filep, char * buff, u32 count)
{
	struct inode* cur_inode = filep -> inode;
	if((cur_inode -> is_valid) == 0) {
		// printk("inode isn't valid, error...\n");
		return -EINVAL;
	}
	if((filep->mode & O_READ) != O_READ) {
		// printk("no read mode, error...\n");
		return -EACCES;
	}
	unsigned int bytes_to_read = count;
	if(count > (cur_inode->max_pos)) bytes_to_read = (cur_inode->max_pos);
	int bytes = flat_read(cur_inode, buff, bytes_to_read, &(filep->offp));
	if(bytes < 0) return -EINVAL;
	(filep->offp) += bytes;
	// printk("read %d bytes in buff: %s\n", bytes, buff);
	return bytes;
}

/*write call corresponding to regular file */

static int do_write_regular(struct file *filep, char * buff, u32 count)
{
	struct inode* cur_inode = filep -> inode;
	if((cur_inode -> is_valid) == 0) {
		return -EINVAL;
	}
	if((filep->mode & O_WRITE) != O_WRITE) {
		return -EACCES;
	}
	unsigned int bytes_to_write = count;
	int offset = cur_inode->max_pos;
	int bytes = flat_write(cur_inode, buff, bytes_to_write, &(filep->offp));

	(filep->offp) += bytes;
	return bytes;
}

long do_file_close(struct file *filep)
{

	if(((filep->inode)->is_valid) == 0) return -EINVAL;

	(filep->ref_count)--;
	if(filep->ref_count == 0){
		// drop the object, this was the last connection
		free_file_object(filep);
	}
	return 0;
}

static long do_lseek_regular(struct file *filep, long offset, int whence)
{
	switch(whence){
		case SEEK_SET: {
			// from start of the file
			if(offset > ((filep->inode)->file_size) || offset < 0) return -EINVAL;
			filep->offp = offset;
			return filep->offp;
		}
		case SEEK_CUR: {
			// from the current offp
			if(((filep->offp) + offset) > ((filep->inode)->file_size) || (((filep->offp) + offset) < 0)) return -EINVAL;
			(filep->offp) += offset;
			return filep->offp;
		}
		case SEEK_END: {
			// from the end of the file
			// DOUBT
			// printk("filesize: %d\n", ((filep->inode)->file_size));
			if(offset > 0) return -EINVAL;
			if((-offset) > ((filep->inode)->file_size)) return -EINVAL;
			if(((filep->inode)->file_size) + offset < 0) return -EINVAL;
			filep->offp = ((filep->inode)->file_size) + offset;
			return filep->offp;
		}
		default: return -EINVAL;
	}
}
void print_info(struct file* f){
	struct inode* i = f->inode;
	printk("Inode valid: %d\n", i->is_valid);
	printk("File length: %d\n", i->e_pos);
	printk("File offset: %d\n", f->offp);
}
// extern int do_regular_file_open(struct exec_context *ctx, char* filename, u64 flags, u64 mode)
// {

// 	int flag[3];
// 	flag[0] = (flags & O_READ == O_READ) ? 1 : 0;
// 	flag[1] = (flags & O_WRITE == O_WRITE) ? 1 : 0;
// 	flag[2] = (flags & O_EXEC == O_EXEC) ? 1 : 0;
// 	// printk("flags: %d %d %d\n", flag[0], flag[1], flag[2]);
// 	struct inode* cur_inode = lookup_inode(filename);

// 	if((flags & O_CREAT) == O_CREAT){
// 		// O_CREAT is there
// 		if(cur_inode == NULL){
// 			// create inode but check the modes first
			int perm[3];
			int inode_mode = mode;
			perm[0] = (inode_mode & O_READ == O_READ) ? 1 : 0;
			perm[1] = (inode_mode & O_WRITE == O_WRITE) ? 1 : 0;
			perm[2] = (inode_mode & O_EXEC == O_EXEC) ? 1 : 0;
			for(int i=0; i<2; i++){
				if(flag[i] == 1 ){
					if(perm[i] != 1){
						// printk("Bad permission for mode %d\n", i);
						return -EACCES;
					}
				}
			}
// 			cur_inode = create_inode(filename, mode);

// 			// inode cannot be created
// 			if(cur_inode == NULL) return -EINVAL;
// 		}
// 	}
// 	if(cur_inode == NULL) return -EINVAL;
	
// 	if(cur_inode -> is_valid == 0){
// 		// printk("Invalid Inode\n");
// 		return -EINVAL;
// 	}

// 	int permissions[3];
// 	int inode_mode = cur_inode -> mode;
// 	permissions[0] = (inode_mode & O_READ == O_READ) ? 1 : 0;
// 	permissions[1] = (inode_mode & O_WRITE == O_WRITE) ? 1 : 0;
// 	permissions[2] = (inode_mode & O_EXEC == O_EXEC) ? 1 : 0;
// 	// printk("perm: %d %d %d\n", permissions[0], permissions[1], permissions[2]);
// 	// printk("permissions(rwx): %d %d %d\n", permissions[0], permissions[1], permissions[2]);

// 	// check the permissions
// 	for(int i=0; i<2; i++){
// 		if(flag[i] == 1 ){
// 			if(permissions[i] != 1){
// 				// printk("Bad permission for mode %d\n", i);
// 				return -EACCES;
// 			}
// 		}
// 	}

// 	int fmode=0;
// 	if((flags & O_READ) == O_READ) fmode |= O_READ;
// 	if((flags & O_WRITE) == O_WRITE) fmode |= O_WRITE;
// 	if((flags & O_EXEC) == O_EXEC) fmode |= O_EXEC;

// 	struct file* cur_file = alloc_file();
// 	if(cur_file == NULL) return -EINVAL;

// 	cur_file -> inode = cur_inode;
// 	cur_file -> type = cur_inode -> type;
// 	cur_file -> mode = fmode;
// 	cur_file -> ref_count = 1;
// 	// assign functions to file
// 	cur_file->fops->read = &do_read_regular;
// 	cur_file->fops->write = &do_write_regular;
// 	cur_file->fops->lseek = &do_lseek_regular;
// 	cur_file->fops->close = &do_file_close;
	
// 	int fd=3;
// 	while(ctx->files[fd] && fd < MAX_OPEN_FILES) fd++;
// 	if(fd == MAX_OPEN_FILES) return -EINVAL;

// 	ctx->files[fd] = cur_file;
// 	return fd;
// }
extern int do_regular_file_open(struct exec_context ctx, char filename, u64 flags, u64 mode)
{

	/**  
	*  TODO Implementation of file open, 
	*  You should be creating file(use the alloc_file function to creat file), 
	*  To create or Get inode use File system function calls, 
	*  Handle mode and flags 
	*  Validate file existence, Max File count is 16, Max Size is 4KB, etc
	*  Incase of Error return valid Error code 
	* */

	int ret_fd = -EINVAL; 
    
    struct inode* fil;
    fil = lookup_inode(filename);
    if(!fil) //no existing file
	{
      if (flags & O_CREAT ){ //o_create flag is there
		int perm[3];
		int inode_mode = mode;
		perm[0] = (inode_mode & O_READ == O_READ) ? 1 : 0;
		perm[1] = (inode_mode & O_WRITE == O_WRITE) ? 1 : 0;
		perm[2] = (inode_mode & O_EXEC == O_EXEC) ? 1 : 0;
		for(int i=0; i<2; i++){
			if(flag[i] == 1 ){
				if(perm[i] != 1){
					// printk("Bad permission for mode %d\n", i);
					return -EACCES;
				}
			}
		}
        fil = create_inode(filename, mode); //open the existing file
        if (!fil) //fil is NULL
          return -ENOMEM;
        flags = flags ^ O_CREAT; //exclude o_creat flag from flags

      }
      else
          return ret_fd;
	}

	//fil strores the file_fd at this point (either existing or newly created)

	if (flags & O_CREAT )
		flags = flags ^ O_CREAT; //removing O_CREAT if it exists

	if ( (!(fil->mode & O_READ)) &&(flags & O_READ) )
		return -EACCES;
	else if( (!(fil->mode & O_WRITE)) &&(flags & O_WRITE) )
		return -EACCES;
	else if ( (!(fil->mode & O_EXEC)) &&(flags & O_EXEC) )
		return -EACCES;
		

    if (fil->file_size > 4096)
      return -EOTHERS;
   
    struct file* newfile = alloc_file();
    newfile->type = REGULAR;
    newfile->mode = flags;
    newfile->inode = fil;
    newfile->offp = 0;

    int free_fd = 3; //starting from 3
    while((ctx->files[free_fd]) && (free_fd < MAX_OPEN_FILES))
    	free_fd++; //find first unassigned fd

    if(free_fd>=MAX_OPEN_FILES)
	{
    	return -EOTHERS;
	}
    else
    {
        ctx->files[free_fd] = newfile; //first unassigned fd
        newfile->ref_count = 1;
    }

    fil->ref_count++;
    newfile->fops->read = do_read_regular;
    newfile->fops->write = do_write_regular;
    newfile->fops->lseek = do_lseek_regular;
	newfile->fops->close = do_file_close;
  return free_fd;
}

/**
 * Implementation dup 2 system call;
 */
int fd_dup2(struct exec_context *current, int oldfd, int newfd)
{
	if(newfd >= MAX_OPEN_FILES || newfd < 0) return -EINVAL;

	struct file* filep = current -> files[oldfd];
	if(filep == NULL){
		return -EINVAL;
	}
	if(((filep->inode)->is_valid) == 0) return -EINVAL;

	if(current->files[newfd]){
		do_file_close(current->files[newfd]);
	}
	
	// now copy the oldfd to newfd place and increment the count
	(filep->ref_count)++;
	current->files[newfd] = filep;
	return newfd;
}

int do_sendfile(struct exec_context *ctx, int outfd, int infd, long *offset, int count) {
	struct file* infile = ctx->files[infd]; 
	struct file* outfile = ctx->files[outfd];

	if(infile == NULL || outfile == NULL) return -EINVAL;
	if(infile->inode->is_valid != 1 || outfile->inode->is_valid != 1) return -EINVAL;
	if((infile->inode->mode & O_READ) == 0 || (outfile->inode->mode & O_WRITE) == 0) return -EACCES;

	struct inode* in_inode = infile->inode;
	struct inode* out_inode = outfile->inode;
	char* buff = (char*)alloc_memory_buffer();
	if(offset){
		// start reading from offset
		// change this offset to offset + bytes_read
		int bytes_read = flat_read(in_inode, buff, count, (int*)offset);
		if(bytes_read < 0) return -EINVAL;

		int bytes_write = flat_write(out_inode, buff, bytes_read, &(outfile->offp));
		if(bytes_write < 0) return -EINVAL;

		// change the offset
		*offset = *offset + bytes_write;
		(outfile->offp) += bytes_write;
		free_memory_buffer((void*)buff);
		return bytes_write;

	}else{
		// start reading from file offset
		// change file offset to offset + read
		int bytes_read = flat_read(in_inode, buff, count, &(infile->offp));
		if(bytes_read < 0) return -EINVAL;

		int bytes_write = flat_write(out_inode, buff, bytes_read, &(outfile->offp));
		if(bytes_write < 0) return -EINVAL;

		// change the offset
		(infile->offp) += bytes_write;
		(outfile->offp) += bytes_write;
		free_memory_buffer((void*)buff);
		return bytes_write;
	}
	return -EINVAL;
}

