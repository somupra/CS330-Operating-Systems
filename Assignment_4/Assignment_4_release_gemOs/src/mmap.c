#include<types.h>
#include<mmap.h>

// Helper function to create a new vm_area
struct vm_area* create_vm_area(u64 start_addr, u64 end_addr, u32 flags, u32 mapping_type)
{
	struct vm_area *new_vm_area = alloc_vm_area();
	new_vm_area-> vm_start = start_addr;
	new_vm_area-> vm_end = end_addr;
	new_vm_area-> access_flags = flags;
	new_vm_area->mapping_type = mapping_type;
	return new_vm_area;
}
void create_entry(struct exec_context *ctx, int error_code, u64 addr, int huge){
	// ////////printk("ce called for addr h: %x %d\n", addr, huge);
	// get base addr of pgdir
	u64 *vaddr_base = (u64 *)osmap(ctx->pgd);
	u64 *entry;
	u64 pfn;
	// set User and Present flags
	// set Write flag if specified in error_code
	u64 ac_flags = 0x5 | (error_code & 0x2);
	
	// find the entry in page directory
	entry = vaddr_base + ((addr & PGD_MASK) >> PGD_SHIFT);
	if(*entry & 0x1) {
		// PGD->PUD Present, access it
		pfn = (*entry >> PTE_SHIFT) & 0xFFFFFFFF;
		vaddr_base = (u64 *)osmap(pfn);
	}else{
		// allocate PUD
		pfn = os_pfn_alloc(OS_PT_REG);
		*entry = (pfn << PTE_SHIFT) | ac_flags;
		vaddr_base = osmap(pfn);
	}

	entry = vaddr_base + ((addr & PUD_MASK) >> PUD_SHIFT);
	if(*entry & 0x1) {
		// PUD->PMD Present, access it
		pfn = (*entry >> PTE_SHIFT) & 0xFFFFFFFF;
		vaddr_base = (u64 *)osmap(pfn);
	}else{
		// allocate PMD
		pfn = os_pfn_alloc(OS_PT_REG);
		*entry = (pfn << PTE_SHIFT) | ac_flags;
		vaddr_base = osmap(pfn);
	}

	entry = vaddr_base + ((addr & PMD_MASK) >> PMD_SHIFT);
	if(huge){
		// ////////printk("create hugepage from pf\n");
		u64* huge_vaddr = (u64*)os_hugepage_alloc();
		u64 huge_pfn = get_hugepage_pfn(huge_vaddr);

		*entry = (huge_pfn << HUGEPAGE_SHIFT) | ac_flags;
		*entry |= (0x1 << 7);
		return;
	}

	if(*entry & 0x1) {
		// PMD->PTE Present, access it
		pfn = (*entry >> PTE_SHIFT) & 0xFFFFFFFF;
		vaddr_base = (u64 *)osmap(pfn);
	}else{
		// allocate PLD 
		pfn = os_pfn_alloc(OS_PT_REG);
		*entry = (pfn << PTE_SHIFT) | ac_flags;
		vaddr_base = osmap(pfn);
	}

	entry = vaddr_base + ((addr & PTE_MASK) >> PTE_SHIFT);
	// since this fault occured as frame was not present, we don't need present check here
	pfn = os_pfn_alloc(USER_REG);
	*entry = (pfn << PTE_SHIFT) | ac_flags;
	// ////////printk("ok\n");
	return;
}
/**
 * Function will invoked whenever there is page fault. (Lazy allocation)
 * 
 * For valid access. Map the physical page 
 * Return 1
 * 
 * For invalid access, i.e Access which is not matching the vm_area access rights (Writing on ReadOnly pages)
 * return -1. 
 */
int vm_area_pagefault(struct exec_context *current, u64 addr, int error_code)
{
	// addr must be in current->vm_area else error
	// ////////printk("got pf %x %d\n", addr, error_code);
	struct vm_area* head = current->vm_area;
	int found = 0;
	while(head != NULL && !found){
		if(addr < (head->vm_end) && addr >= (head->vm_start)) {
			found = 1;
			break;
		}
		head = head->vm_next;
	}
	if(!found) return -1;
	
	// write bit 1
	if((error_code & PROT_WRITE) == PROT_WRITE){
		if((head->access_flags & PROT_WRITE) != PROT_WRITE) return -1;
	}
	int huge = (head->mapping_type == HUGE_PAGE_MAPPING) ? 1 : 0;
	
	// if(huge) ////////printk("huge pf\n");
	if((error_code & 0x1) == 0) create_entry(current, error_code, addr, huge);
	else return -1;
}

void allocate_in_between(struct vm_area* head, struct vm_area* next, int alloc_size, int prot, u64** res, int* found, u64 addr, int fixed){
	// ////////printk("debug insert: %x %x %d %x\n", head->vm_start, next->vm_start, alloc_size, addr);
	// ////////printk("asdfsda h n s: (%x, %x) (%x, %x) %d\n", head->vm_start, head->vm_end, next->vm_start, next->vm_end, alloc_size);
	if(addr){
		if(head->vm_start <= addr && addr < head->vm_end) {
			// ////////printk("exiting fail\n");
			*found = 1;
			return;
		}
		if(head->vm_start <= addr && next->vm_start>addr && next->vm_start < addr+alloc_size){
			////////printk("exiting fail\n");
			*found = 1;
			return;
		}
		if(fixed){
			if(next){
				if(head->vm_end < addr && next->vm_start > addr){
					// space is there, check if possible or not
					if(next->vm_start - addr < alloc_size){
						// ////////printk("here\n");
						*found = 1;
						return;
					}
					else{
						if(next->vm_start - addr == alloc_size){
							if(next->access_flags == prot && next->mapping_type == NORMAL_PAGE_MAPPING){
								next->vm_start -= alloc_size;
								*found = 1;
								**res = addr;
								return;
							}
						}
						struct vm_area* new_node = create_vm_area(addr, addr+alloc_size, prot, NORMAL_PAGE_MAPPING);
						head->vm_next = new_node;
						new_node->vm_next = next;
						*found = 1;
						**res = addr;
						return;
					}
				}
			}
		}
		if(next == NULL){
			if(MMAP_AREA_END - addr >= alloc_size){
				*found = 1;
				// space available, check if can be merged or not
				if(head->vm_end == addr && head->mapping_type == NORMAL_PAGE_MAPPING && head->access_flags == prot){
					u64 to_return = head->vm_end;
					head->vm_end += alloc_size;
					**res = to_return;
					return;
				}else{
					struct vm_area* new_area = create_vm_area(addr, addr+alloc_size, prot, NORMAL_PAGE_MAPPING);
					if(new_area == NULL) return;
					head->vm_next = new_area;
					new_area -> vm_next = NULL;
					**res = addr;
					return;
				}
			}else{
				return;
			}
		}else{
			//check between next and head
			if((head->vm_end <= addr) && (next->vm_start > addr) && (next->vm_start - addr) < alloc_size){
				*found = 1;
				return;
			}
			if(head->vm_end <= addr && addr+alloc_size <= next->vm_start){
				*found = 1;
				if(head->vm_end == addr && head->mapping_type == NORMAL_PAGE_MAPPING && head->access_flags == prot && next->vm_start == addr+alloc_size && next->mapping_type == NORMAL_PAGE_MAPPING && next->access_flags == prot){
					// merge all 
					u64 to_return = head->vm_end;
					head->vm_end = next->vm_end;
					head->vm_next = next->vm_next;
					dealloc_vm_area(next);
					**res = to_return;
					return;
				}
				else if(head->vm_end == addr && head->mapping_type == NORMAL_PAGE_MAPPING && head->access_flags == prot){
					u64 to_return = head->vm_end;
					head->vm_end += alloc_size;
					**res = to_return;
					return;
				}
				else if(next->vm_start == addr+alloc_size && next->mapping_type == NORMAL_PAGE_MAPPING && next->access_flags == prot){
					next->vm_start -= alloc_size;
					**res = next->vm_start;
					return;
				}
				else{
					struct vm_area* new_area = create_vm_area(addr, addr+alloc_size, prot, NORMAL_PAGE_MAPPING);
					if(new_area == NULL) return;
					head->vm_next = new_area;
					new_area->vm_next = next;
					**res = new_area->vm_start;
					return;
				}
			}else return;
		}
		return;
	}
	
	if(next == NULL){
		if(MMAP_AREA_END - (head->vm_end) >= alloc_size){
			*found = 1;
			// space available, check if can be merged or not
			if((head->mapping_type) == NORMAL_PAGE_MAPPING && (head->access_flags) == prot){
				u64 to_return = head->vm_end;
				head->vm_end += alloc_size;
				// *res = head;
				(**res) = to_return;
				// ////////printk("merge: %x %x\n", head->vm_start, head->vm_end);
				return;
			}else{
				
				struct vm_area* new_area = create_vm_area(head->vm_end, (head->vm_end)+alloc_size, prot, NORMAL_PAGE_MAPPING);
				if(new_area == NULL) return;
				head->vm_next = new_area;
				new_area->vm_next = NULL;
				**res = new_area->vm_start;
				// ////////printk("new: %x %x\n", new_area->vm_start, new_area->vm_end);
				return;
			}
		}else{
			return;
		}
	}else{
		
		//check between next and head
		if((next->vm_start) - (head->vm_end) >= alloc_size){
			*found = 1;
			if((next->vm_start) - (head->vm_end) == alloc_size){
				//check if next can be merged or not
				if(next->mapping_type == NORMAL_PAGE_MAPPING && next->access_flags == prot && head->mapping_type == NORMAL_PAGE_MAPPING && head->access_flags == prot){
					// merge all
					**res = head->vm_end;
					head->vm_next = next->vm_next;
					head->vm_end = next->vm_end;
					dealloc_vm_area(next);
					return;
				}else if(head->mapping_type == NORMAL_PAGE_MAPPING && head->access_flags == prot){
					// merge head
					**res = head->vm_end;
					head->vm_end += alloc_size;
					return;
				}else if(next->mapping_type == NORMAL_PAGE_MAPPING && next->access_flags == prot){
					// merge next
					next->vm_start -= alloc_size;
					**res = next->vm_start;
				}else{
					// no merge, new node
					struct vm_area* new_area = create_vm_area((head->vm_end), (head->vm_end)+alloc_size, prot, NORMAL_PAGE_MAPPING);
					if(new_area == NULL) return;
					head->vm_next = new_area;
					new_area->vm_next = next;
					**res = new_area->vm_start;
					return;
				}
			}
			else if(head->mapping_type == NORMAL_PAGE_MAPPING && head->access_flags == prot){
				// space available, check if can be merged or not
				u64 to_return = head->vm_end;
				head->vm_end += alloc_size;
				**res = to_return;
				return;
			}
			else{
				struct vm_area* new_area = create_vm_area((head->vm_end), (head->vm_end)+alloc_size, prot, NORMAL_PAGE_MAPPING);
				if(new_area == NULL) return;
				head->vm_next = new_area;
				new_area->vm_next = next;
				**res = new_area->vm_start;
				return;
			}
		}
	}
}
void allocate(struct vm_area* head, int alloc_size, int prot, u64** res){
	struct vm_area* next = head->vm_next;
	int found = 0;
	while(!found && head){
		// ////////printk("h n s: (%x, %x) (%x, %x) %d\n", head->vm_start, head->vm_end, next->vm_start, next->vm_end, alloc_size);
		allocate_in_between(head, next, alloc_size, prot, res, &found, (u64)NULL, 0);
		// iterate to next nodes
		head = head->vm_next;
		if(next) next = next->vm_next;
	}
	return;
}
/**
 * mmap system call implementation.
 */
long vm_area_map(struct exec_context *current, u64 addr, int length, int prot, int flags)
{
	if(length <= 0) {
		return -1;
	}
	int fixed = flags;
	int read = ((prot & PROT_READ) == PROT_READ);
	int write = ((prot & PROT_WRITE) == PROT_WRITE);
	int alloc_size = (length % 4096 == 0) ? length : ((int)(length/4096) + 1) * 4096;


	if(current->vm_area == NULL){
		struct vm_area* dummy = create_vm_area(MMAP_AREA_START, MMAP_AREA_START + 0x1000, 0x4, NORMAL_PAGE_MAPPING);
		if(dummy == NULL) {
			return -1;
		}
		else current->vm_area = dummy;
	}
	struct vm_area* head = current->vm_area;
	struct vm_area* next = head -> vm_next;
	
	if((u64*)addr == NULL){
		// no hint given, search for space of size length
		u64 check = -1;
		u64* res = &check;

		// ////////printk("alloc\n");
		allocate(head, alloc_size, prot, &res);
		if(*res == -1) {
			// ////////printk("fail\n");
			return -1;
		}
		else {
			return *res;
		}
	}
	else{
		// hint address is given
		if(addr >= MMAP_AREA_END) return -1;
		u64 pos_addr = ((addr - MMAP_AREA_START) % 4096 == 0) ? addr : ((u64)(addr/4096) + 1) * 4096;
		if(fixed == MAP_FIXED){
			////////printk("fixed addr given %x\n", addr);
			if(addr != pos_addr) return -1;
			struct vm_area* head = current->vm_area;
			struct vm_area* next = head -> vm_next;
			u64 check = -1;
			u64* res = &check;
			int found = 0;
			while(!(found) && head){
				////////printk("check head next: %x %x %x %x\n", head->vm_start, head->vm_end, next->vm_start, next->vm_end);
				allocate_in_between(head, next, alloc_size, prot, &res, &found, addr, 1);
				if(found && *res == -1) return -1;
				if(*res != -1) {
					////////printk("found, returning %x\n", *res);
					return *res;
				}
				
				head = head->vm_next;
				if(next) next = next->vm_next;
			}
			return -1;
		}else{
			struct vm_area* head = current->vm_area;
			struct vm_area* next = head -> vm_next;
			u64 check = -1;
			u64* res = &check;
			int found = 0;
			////////printk("NOT FIXED addr given %x\n", addr);
			while(!(found) && head){
				////////printk("check head next: %x %x %x %x\n", head->vm_start, head->vm_end, next->vm_start, next->vm_end);
				allocate_in_between(head, next, alloc_size, prot, &res, &found, pos_addr, 0);
				////////printk("found and res value: %d %x\n", found, *res);
				if(found && *res == -1) break;
				if(*res != -1) return *res;
				
				head = head->vm_next;
				if(next) next = next->vm_next;
			}
			// did not find a possible allocation for the address, allocate normally
			////////printk("hint fail, starting mappping here\n");
			head = current->vm_area;
			check = -1; res = &check;
			allocate(head, alloc_size, prot, &res);
			if(*res == -1) return -1;
			else return *res;
		}
	}
	return 0;
}

void physical_unmap(u64 start, u64 end, struct exec_context *ctx){
	// ////////printk("in unmap phy addr: %x %x\n", start, end);
	u64* vaddr_base = (u64 *)osmap(ctx->pgd);
	u64* entry;
	for(u64 addr = start; addr<end; addr+=4096){
		// ////////printk("iter %x\n", addr);
		// PGD level
		u64* entry = vaddr_base + ((addr & PGD_MASK) >> PGD_SHIFT);
		
		if(*entry & 0x1) {
			// PUD level
			vaddr_base = osmap((*entry >> PTE_SHIFT));
			entry = vaddr_base + ((addr & PUD_MASK) >> PUD_SHIFT);
		}else{
			continue;
		}
		
		if(*entry & 0x1) {
			// PMD level
			vaddr_base = osmap((*entry >> PTE_SHIFT));
			entry = vaddr_base + ((addr & PMD_MASK) >> PMD_SHIFT);
			if(*entry & (0x1 << 7)){
				// huge page
				
				
				u64* hp_addr = (u64*)(((*entry >> HUGEPAGE_SHIFT) & 0xFFFFFFFF) <<HUGEPAGE_SHIFT);
				
				// ////////printk("free huge page on %x\n", hp_addr);
				os_hugepage_free(hp_addr);
				
				*entry = (u64)((u64)*entry & (~ 0x1)); 

				// free tlb for addr
				// ////////printk("free\n");
				asm volatile (
				"invlpg (%0);" 
				:: "r"(addr) 
				: "memory"
				); 

				u64 mb = 1 << 20;
				// addr += (2*mb - 0x1000);
				continue;
			}
		}else{
			continue;
		}
		
		if(*entry & 0x1) {
			// PLD level 
			vaddr_base = osmap((*entry >> PTE_SHIFT));
			entry = vaddr_base + ((addr & PTE_MASK) >> PTE_SHIFT);
		}else{
			continue;
		}

		
		for(int i=0; i<512; i++){
			u64 pte_entry = *(vaddr_base+i);
			if((pte_entry & 0x1) == 0x1){
				// ////////printk("normal freeing %x", (pte_entry >> PTE_SHIFT) & 0xFFFFFFFF);
				// ////////printk("shouldn't be here\n");
				os_pfn_free(USER_REG, (pte_entry >> PTE_SHIFT) & 0xFFFFFFFF);
			}
			*(vaddr_base+i) = (*(vaddr_base+i) & (~(0x1))); 

		}
		
		// free tlb for addr
		// ////////printk("free\n");
		asm volatile (
		"invlpg (%0);" 
		:: "r"(addr) 
		: "memory"
		); 
		
	}
	return;
}
/**
 * munmap system call implemenations
 */
int vm_area_unmap(struct exec_context *current, u64 addr, int length)
{
	// ////////printk("debug unmap %x %d\n", addr, length);
	if(length <= 0) return -1;
	if(addr <= MMAP_AREA_START || addr > MMAP_AREA_END) return -1;
	if((addr - MMAP_AREA_START) % 0x1000 != 0) return -1;

	int unmap_len = (length % 4096 == 0) ? length : ((int)(length/4096) + 1) * 4096;

	struct vm_area* prev = current->vm_area;
	struct vm_area* head = prev->vm_next;
	while(prev != NULL){
		if(head)
		{
			if(head->vm_start < addr && head->vm_end < addr){
				// ////////printk("pass\n");
			}
			if(head->vm_start >= addr+length){
				// ////////printk("pass\n");
			}
			if(head->vm_start >= addr && head->vm_start < addr+length) // head is affected in this case
			{
				if(head->vm_end <= addr+unmap_len) // fully affected
				{
					// ////////printk("must be here\n");
					prev->vm_next = head->vm_next;

					physical_unmap(head->vm_start, head->vm_end, current);
					// ////////printk("phyunmap success 1!\n");
					dealloc_vm_area(head);

					head = prev->vm_next;
					continue;
					
				}
				else if(head->vm_end > addr+unmap_len) // partially affected 
				{
					// unmap from [start, addr+unmap_len)
					physical_unmap(head->vm_start, addr+unmap_len, current);
					// ////////printk("phyunmap success 2!\n");
					head->vm_start = addr+unmap_len;
				}
			}
			else if(head->vm_start < addr && head->vm_end > addr) // head is affected in this case too
			{
				if(head->vm_end <= addr+unmap_len) // partially affected
				{
					physical_unmap(addr, head->vm_end, current);
					// ////////printk("phyunmap success 3!\n");
					head->vm_end = addr;
				}
				else if(head->vm_end > addr+unmap_len) // break the node
				{
					// ////////printk("break must be done\n");
					
					physical_unmap(addr, addr+unmap_len, current);
					// ////////printk("phyunmap success 4!\n");
					struct vm_area* new_node = create_vm_area(addr+unmap_len, head->vm_end, head->access_flags, NORMAL_PAGE_MAPPING);
					head->vm_end = addr;
					new_node->vm_next = head->vm_next;
					head->vm_next = new_node;
				}
			}
		}
		
		prev = prev->vm_next;
		if(head) head = head -> vm_next;
	}
 	return 0;
}

int check_sanitization(struct exec_context* current, u64 addr, u32 len, int prot, int force_prot){
	struct vm_area* head = current->vm_area;
	int found = 0;
	while(head){
		if(head->vm_start <= addr && head->vm_end > addr){
			found = 1;
			// ////////printk("found\n");
			break;
		}
		head = head->vm_next;
	}
	if(!found) {
		// ////////printk("1\n");
		return -ENOMAPPING;
	}

	// ////printk("h: %x %x\n", head->vm_start, head->vm_end);

	u64 curr_addr = head->vm_end;
	if(head->vm_next == NULL){
		// ////printk("here must be\n");
		if(head->vm_end < addr+len){
			// ////////printk("muasdf\n");
			return -ENOMAPPING;
		}
		else if(head->mapping_type == HUGE_PAGE_MAPPING){
			return -EVMAOCCUPIED;
		}
		else if(head->access_flags != prot){
			if(!force_prot) return -EDIFFPROT;
		}
		else{
			return 0;
		}
	}
	head = head->vm_next;
	
	while(1){
		// ////////printk("head: %x %x\n", head->vm_start, head->vm_end);

		if(head == NULL && (curr_addr == addr+len)){
			// ////////printk("good\n");
			return 0;
		}

		if(head == NULL && (curr_addr != addr+len)){
			// ////////printk("2 %x %x\n", curr_addr, addr);
			return -ENOMAPPING;
		}
		if(head->vm_start < addr+len){
			if(head->vm_start != curr_addr){
				// ////////printk("3\n");
				return -ENOMAPPING;
			}
			if(head->mapping_type == HUGE_PAGE_MAPPING){
				// ////////printk("4\n");
				return -EVMAOCCUPIED;
			}
			if(head->access_flags != prot){
				// ////////printk("5\n");
				if(!force_prot) return -EDIFFPROT;
			}
			
			curr_addr = (head->vm_end >= addr+len) ? addr+len : head->vm_end;
			head = head->vm_next;
			
		}
		else break;
	}
	return 0;
}

void copy_physical_memory(struct exec_context *ctx, u64 start, u64 end, int prot){
	// //////printk("cpm: %x %x\n", start, end);
	u64* pgd_vaddr_base; 
	u64* pud_vaddr_base;
	u64* pmd_vaddr_base;
	u64* pld_vaddr_base;
	u64* pgd_entry;
	u64* pud_entry;
	u64* pmd_entry;
	u64* pld_entry;
	
	pgd_vaddr_base = (u64 *)osmap(ctx->pgd);
	// //////printk("pgd base: %x\n", pgd_vaddr_base);

	u64 ac_flags = 0x5 | (prot & 0x2);

	for(u64 addr=start; addr<end; addr+=4096){
		// //////printk("addr %x\n", addr);
		pgd_entry = pgd_vaddr_base + ((addr & PGD_MASK) >> PGD_SHIFT);
		// //////printk("pgd entry addr val: %x %x\n", pgd_entry, *pgd_entry);
		
		if(*pgd_entry & 0x1) {
			// PUD level
			pud_vaddr_base = osmap((*pgd_entry >> PTE_SHIFT) & 0xFFFFFFFF);
			pud_entry = pud_vaddr_base + ((addr & PUD_MASK) >> PUD_SHIFT);
			// //////printk("pud entry addr val: %x %x\n", pud_entry, *pud_entry);
		}else{
			continue;
		}
		
		if(*pud_entry & 0x1) {
			// PMD level
			pmd_vaddr_base = osmap((*pud_entry >> PTE_SHIFT) & 0xFFFFFFFF);
			pmd_entry = pmd_vaddr_base + ((addr & PMD_MASK) >> PMD_SHIFT);

			// //////printk("pmd entry addr val: %x %x\n", pmd_entry, *pmd_entry);
			
			// PLD exists
			if((*pmd_entry & 0x1) && !(*pmd_entry & (0x1 << 7))){
				// //////printk("will create huge page\n");
				u64* hugepg_addr = (u64*)os_hugepage_alloc();
				u64 huge_pfn = get_hugepage_pfn(hugepg_addr);

				u32 pfn = (*pmd_entry >> PTE_SHIFT) & 0xFFFFFFFF;
				pld_vaddr_base = (u64 *)osmap(pfn);
				
				// PLD level
				pld_entry = pld_vaddr_base + ((addr & PTE_MASK) >> PTE_SHIFT);
				// //////printk("pld entry addr val: %x %x\n", pld_entry, *pld_entry);

				for(int i=0; i<512; i++){
					if((*(pld_entry+i) & 0x1)){
						u64* src = (u64*) osmap(*(pld_entry+i) >> PTE_SHIFT);
						// ////printk("debug: %x %x\n", hugepg_addr, src);
						memcpy((char*)((char*)hugepg_addr+(i*4096)), (char*)src, 4096);
					}
				}
				
				*pmd_entry = ((u64)huge_pfn << HUGEPAGE_SHIFT) | ac_flags;
				*pmd_entry |= (0x1 << 7);
				// ////printk("pmd_entry after hugepage addr val: %x %x\n", pmd_entry, *pmd_entry);
				continue;
			}else{
				continue;
			}
		}else{
			continue;
		}
	}
	return;
}
/**
 * make_hugepage system call implemenation
 */
long vm_area_make_hugepage(struct exec_context *current, void *adr, u32 length, u32 prot, u32 force_prot)
{
	u32 tmb = 2*(1<<20);
	if((u64*)adr == NULL) return -EINVAL;
	if(length <= 0) return -EINVAL;
	u64 end_bound = (u64)adr +length;
	u64 addr = ((u64)adr % tmb == 0) ? (u64)adr : (u64)(((u64)((u64)adr / tmb) + 1) * tmb);
	u32 huge_len = ((end_bound-addr) % tmb == 0) ? (end_bound-addr) : ((u32)((end_bound-addr)/tmb))*tmb;
	
	// //////printk("adr addr len hlen: %x %x %x %x\n", adr, addr, length, huge_len);

	if(addr+huge_len == addr) return -EINVAL;
	
	int err = check_sanitization(current, (u64)adr, length, prot, force_prot);
	// //////printk("sanity: %d\n", err);
	if(err) return err;

	struct vm_area* prev = current->vm_area;
	struct vm_area* head = prev->vm_next;
	int found = 0;
	while(head){
		if(head->vm_start <= addr && head->vm_end > addr){
			found = 1;
			break;
		}
		prev = prev->vm_next;
		head = head->vm_next;
	}
	if(!found){
		// //////printk("problem in sanitization\n");
		return -EINVAL;
	}
	// create huge head from head
	struct vm_area* huge_head;
	

	//////printk("hm start from head: %x %x\n", head->vm_start, head->vm_end);

	if(head->vm_start == addr){
		if(head->vm_end <= addr+huge_len){
			head->mapping_type = HUGE_PAGE_MAPPING;
			head->access_flags = prot;
			huge_head = head;
		}else{
			struct vm_area* new_normal = create_vm_area(addr+huge_len, head->vm_end, head->access_flags, NORMAL_PAGE_MAPPING);
			new_normal->vm_next = head->vm_next;
			head->vm_next = new_normal;	
			head->vm_end = addr+huge_len;
			huge_head = head;
		}
		if((prev->access_flags == prot) && (prev->mapping_type == HUGE_PAGE_MAPPING) && prev->vm_end == huge_head->vm_start){
			prev->vm_end = huge_head->vm_end;
			prev->vm_next = huge_head->vm_next;
			dealloc_vm_area(huge_head);
			huge_head = prev;
		}
		
	} else {
		// break from front
		// ////////printk("must be here\n");
		huge_head = create_vm_area(addr, head->vm_end, prot, HUGE_PAGE_MAPPING);
		head->vm_end = addr;
		huge_head->vm_next = head->vm_next;
		head->vm_next = huge_head;

		// ////////printk("huge head: %x %x\n", huge_head->vm_start, huge_head->vm_end);
		// ////////printk("bound: %x\n", addr+huge_len);

		if(huge_head->vm_end > addr+huge_len){
			// break from behind
			// ////////printk("must be here too\n");
			struct vm_area* new_normal = create_vm_area(addr+huge_len, huge_head->vm_end, head->access_flags, NORMAL_PAGE_MAPPING);
			new_normal->vm_next = huge_head->vm_next;
			huge_head->vm_next = new_normal;
			huge_head->vm_end = addr+huge_len;
		}
	}

	huge_head->mapping_type = HUGE_PAGE_MAPPING;
	u64 start=huge_head->vm_start, end=huge_head->vm_end;

	// ////////printk("huge head: %x %x\n", huge_head->vm_start, huge_head->vm_end);
	// ////////printk("end: %x\n", addr+huge_len);

	struct vm_area* next;
	while((huge_head->vm_next != NULL) && (huge_head->vm_next->vm_start < addr+huge_len)){
		next = huge_head->vm_next;
		// ////////printk("pick next: %x %x\n", next->vm_start, next->vm_end);
		if(next->vm_end <= addr+huge_len){
			// full destroy
			huge_head->vm_end = next->vm_end;
			huge_head->vm_next = next->vm_next;
			end = huge_head->vm_end;
			continue;
		}
		if(next->vm_end > addr+huge_len){
			huge_head->vm_end = addr+huge_len;
			next->vm_start = addr+huge_len;
			end = huge_head->vm_end;
			break;
		}
	}
	if(huge_head->vm_next){
		if((huge_head->vm_end == huge_head->vm_next->vm_start) && (huge_head->vm_next->access_flags == prot )&& (huge_head->vm_next->mapping_type == HUGE_PAGE_MAPPING)){
			// ////////printk("did wrong\n");
			next = huge_head->vm_next;
			huge_head->vm_next = next->vm_next;
			huge_head->vm_end = next->vm_end;
			dealloc_vm_area(next);
			end = huge_head->vm_end;
		}
	}
	// ////////printk("final copy: %x %x\n", start, end);
	copy_physical_memory(current, start, end, prot);
	return addr;
}


void breakhuge(u64 start, u64 end, struct exec_context* ctx, int prot){
	//////printk("bh: %x %x\n", start, end);

	u64 TMB = 2*(1<<20);
	u64* pgd_vaddr_base; 
	u64* pud_vaddr_base;
	u64* pmd_vaddr_base;
	u64* pld_vaddr_base;
	u64* pgd_entry;
	u64* pud_entry;
	u64* pmd_entry;
	u64* pld_entry;
	pgd_vaddr_base = (u64 *)osmap(ctx->pgd);
	u64 ac_flags = 0x5 | (prot & 0x2);
	
	for(u64 addr=start; addr<end; addr+=TMB){
		u64* pgd_entry = pgd_vaddr_base + ((addr & PGD_MASK) >> PGD_SHIFT);
		
		if(*pgd_entry & 0x1) {
			pud_vaddr_base = osmap((*pgd_entry >> PTE_SHIFT) & 0xFFFFFFFF);
			pud_entry = pud_vaddr_base + ((addr & PUD_MASK) >> PUD_SHIFT);
			for(int i=0; i<512; i++)
			{
				pmd_vaddr_base = osmap((*(pud_entry+i) >> PTE_SHIFT) & 0xFFFFFFFF);
				pmd_entry = pmd_vaddr_base + ((addr & PMD_MASK) >> PMD_SHIFT);

				if(((*pmd_entry & 0x1) == 0x1) && ((*pmd_entry & (0x1 << 7)) == (0x1 << 7)))
				{
	
					u64* hp_base = (u64*) (((*pmd_entry >> HUGEPAGE_SHIFT) & 0xFFFFFFFF) << HUGEPAGE_SHIFT);
					
					u64 pfn = os_pfn_alloc(OS_PT_REG); 
					*pmd_entry = (pfn << PTE_SHIFT) | ac_flags;
					pld_vaddr_base = osmap(pfn);
					
					for(int i=0; i<512; i++)
					{	
						u64 pfn;
						
						pfn = os_pfn_alloc(USER_REG);
						
						*(pld_vaddr_base+i) = ((pfn << PTE_SHIFT) | ac_flags);
						
						u64* phy_vaddr_base = osmap(pfn);
						
						memcpy((char*)phy_vaddr_base, (char*)((char*)hp_base+(i*4096)), 4096);

					}

					os_hugepage_free(hp_base);

					asm volatile (
					"invlpg (%0);" 
					:: "r"(addr) 
					: "memory"
					); 

				}
				else continue;
			}
		}else{
			continue;
		}
	}
}
/**
 * break_system call implemenation
 */
int vm_area_break_hugepage(struct exec_context *current, void *addr, u32 length)
{
	u64 mb = 1<<20;
	addr = (u64*) addr;
	if (length % (2*mb)) return -EINVAL;
	if (((u64)addr - MMAP_AREA_START) % (2*mb)) return -EINVAL;

	u64 start = (u64)addr;
	u64 end = start + length;

	struct vm_area* prev = current->vm_area;
	struct vm_area* head = prev->vm_next;

	while (head && !(head->vm_start >= end))
	{
		// ////////printk("head: %x %x\n", head->vm_start, head->vm_end);
		if (head->mapping_type == HUGE_PAGE_MAPPING)
		{	
			if(head->vm_start <= start && head->vm_end <= end) // unmap from [start, vm_end)
			{
				head->mapping_type = NORMAL_PAGE_MAPPING;
				breakhuge(start, head->vm_end, current, head->access_flags);
				
			}
			else if(head->vm_start >= start && head->vm_end < end) // unmap from [vm_start, vm_end)
			{
				head->mapping_type = NORMAL_PAGE_MAPPING;
				breakhuge(head->vm_start, head->vm_end, current, head->access_flags);
				
				if(prev->vm_end == head->vm_start && prev->mapping_type == NORMAL_PAGE_MAPPING && prev->access_flags == head->access_flags){
					// merge prev and head
					prev->vm_next = head->vm_next;
					prev->vm_end = head->vm_end;
					dealloc_vm_area(head);
					head = prev->vm_next;
					continue;
				}
			}
			else if(head->vm_start >= start && head->vm_end >= end) // unmap from [vm_start, end)
			{
				head->mapping_type = NORMAL_PAGE_MAPPING;
				breakhuge(head->vm_start, end, current, head->access_flags);

				if(prev->vm_end == head->vm_start && prev->mapping_type == NORMAL_PAGE_MAPPING && prev->access_flags == head->access_flags){
					// merge prev and head
					prev->vm_next = head->vm_next;
					prev->vm_end = end;
					dealloc_vm_area(head);
					head = prev->vm_next;
					continue;
				}
			}
		}
		prev = prev->vm_next;
		head = head->vm_next;
	}
	if(head){
		if(head->vm_next){
			struct vm_area* next = head->vm_next;
			if(next->vm_start == head->vm_end && next->access_flags == head->access_flags && next->mapping_type == NORMAL_PAGE_MAPPING){
				// merge head with head->vm_next
				head->vm_end = next->vm_end;
				head->vm_next = next->vm_next;
				dealloc_vm_area(next);
			}
		}
	}
	return 0;
}
