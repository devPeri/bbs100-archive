/*
    bbs100 3.0 WJ105
    Copyright (C) 2005  Walter de Jong <walter@heiho.net>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/
/*
	BinAlloc.c	WJ105

	The bin allocator tries to minimize fragmentation by using collections of the
	same type.

	Short description of the memory layout:

		[ MemBin structure, slot, ... ]

		There is a MemBin for every type
		There are MemBin.free bytes free in this bin
		Minimum allocation is sizeof(type) + 1 byte marker + 1 byte type
		(The type byte is used to make BinFree() a lot faster)
		Minimum allocation for TYPE_CHAR is 16 (gets rounded)

		A slot begins with a 1-byte marker;
		The mark has a meaning:
		0     - slot is free
		1     - slot is in use
		n     - n slots in use; max is 254
		0xff  - this memory was allocated using regular malloc()

		If malloc() was used (marked 0xff), there is an unsigned long before this
		address, which is the size. The address of the unsigned long should be passed
		to free() to free this allocation.

	Note: all memory allocators in bbs100 MUST zero out the allocated block, or you will
	      segfaults all over
*/

#include "config.h"
#include "BinAlloc.h"
#include "debug.h"
#include "Memory.h"
#include "Types.h"
#include "memset.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static MemBin *bins[NUM_TYPES];
static int use_bin_alloc = 0;

static MemInfo mem_info;
static int type_balance[NUM_TYPES];

static void *get_from_bin(unsigned long, int, int);
static void *use_malloc(unsigned long, int);

int init_BinAlloc(void) {
	memset(bins, 0, NUM_TYPES * sizeof(MemBin *));
	memset(&mem_info, 0, sizeof(MemInfo));
	memset(type_balance, 0, sizeof(int) * NUM_TYPES);
	enable_BinAlloc();
	return 0;
}

void deinit_BinAlloc(void) {
	disable_BinAlloc();
}

void enable_BinAlloc(void) {
	use_bin_alloc = 1;
}

void disable_BinAlloc(void) {
	use_bin_alloc = 0;
}

MemBin *new_MemBin(int type) {
MemBin *bin;
char *mem;
int i, size;

	if (type < 0 || type >= NUM_TYPES)
		return NULL;

	if (Types_table[type].size > MAX_BIN_FREE) {
		log_warn("new_MemBin(): type %s (%d bytes) is too large to fit in a bin of %d (%d) bytes", Types_table[type].type, Types_table[type].size, BIN_SIZE, MAX_BIN_FREE);
		return NULL;
	}
/*
	just use TYPE_CHAR because there is no TYPE_MEMBIN
*/
	if ((bin = (MemBin *)BinMalloc(BIN_SIZE, TYPE_CHAR)) == NULL)
		return NULL;

	memset(bin, 0, sizeof(MemBin));
	bin->free = MAX_BIN_FREE;

/* mark all slots as free */
	mem = (char *)bin;
	size = ((type == TYPE_CHAR) ? SIZE_CHAR : Types_table[type].size) + MARKER_SIZE;
	for(i = BIN_MEM_START; i < BIN_MEM_END; i += size) {
		ST_MARK(mem + i, MARK_FREE);
		ST_TYPE(mem + i, type);
	}
	return bin;
}

void destroy_MemBin(MemBin *bin) {
	if (bin == NULL)
		return;

	if (bin->free < MAX_BIN_FREE) {
		log_err("destroy_MemBin(): attempt to destroy a bin that is still in use");
		return;
	}
	BinFree(bin);
}

void *BinMalloc(unsigned long size, int type) {
void *ptr;
MemBin *bin;
int n;

	if (!use_bin_alloc)
		return use_malloc(size, type);

	if (size <= 0UL)
		return NULL;

	if (type < 0 || type >= NUM_TYPES) {
		log_warn("BinMalloc(): unknown type %d, using malloc()", type);
		return use_malloc(size, TYPE_UNKNOWN);
	}
	if (type == TYPE_CHAR)
		ROUND_UP(size, SIZE_CHAR);

	if (size >= MAX_BIN_FREE)
		return use_malloc(size, type);

	n = size / ((type == TYPE_CHAR) ? SIZE_CHAR : Types_table[type].size);
	if (n <= 0)
		n = 1;

	if (n >= MARK_MALLOC)			/* this would clash with the special 0xff marker, so ... use malloc() */
		return use_malloc(size, type);

	if ((ptr = get_from_bin(size, n, type)) == NULL) {
		if ((bin = new_MemBin(type)) == NULL)
			return use_malloc(size, type);

		add_MemBin(&bins[type], bin);

		if ((ptr = get_from_bin(size, n, type)) == NULL) {
			log_warn("BinMalloc(): failed even after adding a bin (size = %lu, n = %d, type = %s), using malloc()", size, n, Types_table[type].type);
			return use_malloc(size, type);
		}
	}
	mem_info.balance++;
	type_balance[type]++;
	return ptr;
}

/*
	allocate 'n' slots in a bin of type 'type'
	'size' is only needed when things don't work out well and use_malloc() needs to called anyway
*/
static void *get_from_bin(unsigned long size, int n, int type) {
MemBin *bin;
int i, in_use, cont_free, type_size;
unsigned long satisfied;
char *mem, *startp;

	if (n <= 0)
		return NULL;

	if (type < 0 || type >= NUM_TYPES) {
		log_warn("get_from_bin(): unknown type %d, using malloc()", type);
		return use_malloc(size, TYPE_UNKNOWN);
	}
	type_size = (type == TYPE_CHAR) ? SIZE_CHAR : Types_table[type].size;

	for(bin = bins[type]; bin != NULL; bin = bin->next) {
/*
	find enough free bytes; or n contiguous free slots
*/
		if (bin->free >= size) {
			mem = (char *)bin;
			startp = NULL;
			cont_free = 0;
			satisfied = 0UL;

			for(i = BIN_MEM_START; i < BIN_MEM_END;) {
				LD_MARK(mem + i, in_use);
				if (in_use >= MARK_MALLOC) {
					log_err("get_from_bin(): invalid marker in bin for type %s, disabling bin", Types_table[type].type);
					bin->free = 0;			/* disable corrupted bin */
					return use_malloc(size, type);
				}
				if (in_use == MARK_FREE) {
					if (startp == NULL)
						startp = mem + i;
/*
	found a free slot
	if n >= 0xff, BinFree() would interpret this as 'malloc() was used'
	if n > 1, we are going across slot boundaries, which means we can also use the space
	occupied by the marker
*/
					cont_free++;
					if (cont_free > 1)
						satisfied += MARKER_SIZE;

					if (cont_free >= MARK_MALLOC)			/* prevent clashes with the special 'use malloc' marker */
						return use_malloc(size, type);

					satisfied += type_size;
					if (i + MARKER_SIZE + type_size > BIN_MEM_END)		/* doesn't fit */
						break;

					if (satisfied >= size) {				/* found enough space */
						bin->free -= satisfied;
						if (bin->free < 0) {
							log_err("get_from_bin(): bin->free < 0 for bin of type %s, using malloc()", Types_table[type].type);
							return use_malloc(size, type);
						}
						mem_info.in_use += satisfied;

						ST_MARK(startp, cont_free);			/* this many slots in use */
						ST_TYPE(startp, type);				/* set type for BinFree() */
						startp = startp + MARKER_SIZE;
						memset(startp, 0, size);
						return (void *)startp;
					}
					i += type_size + MARKER_SIZE;
				} else {
					cont_free = 0;
					startp = NULL;
					satisfied = 0UL;
					i += in_use * (type_size + MARKER_SIZE);	/* skip block that's in use */
				}
			}
		}
	}
	return NULL;			/* no slots available */
}

/*
	use standard malloc() to allocate memory
*/
static void *use_malloc(unsigned long size, int type) {
unsigned long *ulptr;

	if (!size || type < 0 || type >= NUM_TYPES)
		return NULL;

	size += sizeof(unsigned long) + MARKER_SIZE;
	if ((ulptr = (unsigned long *)malloc(size)) == NULL)
		return NULL;

	mem_info.malloc += size;
	mem_info.balance++;

	*ulptr = size;
	ulptr++;
	ST_MARK(ulptr, MARK_MALLOC);		/* malloc() was used */
	ST_TYPE(ulptr, type);
	memset((char *)ulptr + MARKER_SIZE, 0, size);
	return (void *)((char *)ulptr + MARKER_SIZE);
}

void BinFree(void *ptr) {
MemBin *bin;
char *mem;
int in_use, type, type_size;
unsigned long bin_start, bin_end;

	if (ptr == NULL)
		return;

	mem = (char *)ptr - MARKER_SIZE;
	LD_MARK(mem, in_use);
	if (in_use == MARK_FREE) {
		log_err("BinFree(): memory was not marked as in use");
		return;
	}
	if (in_use == MARK_MALLOC) {		/* allocated by use_malloc() */
		unsigned long *ulptr;

		ulptr = (unsigned long *)mem;
		ulptr--;
		mem_info.malloc -= *ulptr;
		mem_info.balance--;
		free(ulptr);
		return;
	}
	LD_TYPE(mem, type);
	if (type < 0 || type >= NUM_TYPES) {
		log_err("BinFree(): invalid type; the mark has been overwritten");
		return;
	}
	type_size = (type == TYPE_CHAR) ? SIZE_CHAR : Types_table[type].size;

/* find the particular bin that we're in */

	for(bin = bins[type]; bin != NULL; bin = bin->next) {
		bin_start = (unsigned long)bin + BIN_MEM_START;
		bin_end = (unsigned long)bin + BIN_MEM_END;
		if ((unsigned long)mem >= bin_start && (unsigned long)mem < bin_end) {
			if (bin->free < 0) {
				log_err("BinFree(): corrupted number of free bytes in %s bin", Types_table[type].type);
				bin->free = 0;
				return;
			}
/*
	marker space doesn't count for the first slot (confusing, I know, but it _is_ correct
*/
			bin->free += in_use * (type_size + MARKER_SIZE) - MARKER_SIZE;

			if (bin->free <= 0 || bin->free > MAX_BIN_FREE) {
				log_err("BinFree(): corrupted number of free bytes in %s bin", Types_table[type].type);
				bin->free = 0;
				return;
			}
			mem_info.in_use -= in_use * (type_size + MARKER_SIZE) - MARKER_SIZE;
			mem_info.balance--;
			type_balance[type]--;
/*
	free unneeded bin if it is not the only one, because if it is the only one,
	it is likely we will very soon need to add it again anyway
*/
			if (bin->free >= MAX_BIN_FREE && !(bins[type]->prev == NULL && bins[type]->next == NULL)) {
				remove_MemBin(&bins[type], bin);
				destroy_MemBin(bin);
			} else {
				while(in_use > 0) {					/* free all occupied slots */
					in_use--;
					ST_MARK(mem, MARK_FREE);
					mem = mem + type_size + MARKER_SIZE;
				}
			}
			return;
		}
	}
	log_err("BinFree(): originating bin does not exist");
}

int get_MemBinInfo(MemBinInfo *info, int type) {
MemBin *bin;

	if (info == NULL || type < 0 || type >= NUM_TYPES)
		return -1;

	info->bins = 0;
	info->free = 0UL;
	for(bin = bins[type]; bin != NULL; bin = bin->next) {
		info->bins++;
		info->free += bin->free;
	}
	info->balance = type_balance[type];
	return 0;
}

int get_MemInfo(MemInfo *info) {
	if (info == NULL)
		return -1;

	memcpy(info, &mem_info, sizeof(MemInfo));
	return 0;
}

/* EOB */
