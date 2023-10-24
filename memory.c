#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <fcntl.h>
#include <assert.h>
#include "memory.h"
#include <math.h>

#define PAGE_SIZE 4096

long long bucket_sizes[] = {16, 32, 64, 128, 256, 512, 1024, 2048, 4080};

typedef struct bucket_node {
	struct bucket_node* prev;
	struct bucket_node* next;
}bucket_node;

bucket_node* Buckets[9];

typedef struct page_metadata {
	long long bucket_size;
	long long available_bytes;
}page_metadata;

page_metadata* construct_metadata(void* addr, long long _bucket_size, long long _available_bytes)
{
	page_metadata* ret = (page_metadata*) addr;
	ret->bucket_size = _bucket_size;
	ret->available_bytes = _available_bytes;

	return ret;
}

static void *alloc_from_ram(size_t size)
{
	assert((size % PAGE_SIZE) == 0 && "size must be multiples of 4096");
	void* base = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_ANON|MAP_PRIVATE, -1, 0);
	if (base == MAP_FAILED)
	{
		printf("Unable to allocate RAM space\n");
		exit(0);
	}
	return base;
}

static void free_ram(void *addr, size_t size)
{
	munmap(addr, size);
}



int findAptBucket(long long _size)
{
	
	for (int i = 0; i < 9; i++)
	{
		if (_size/bucket_sizes[i] <= 1)
		{
			return i;
		}
	}

	return -1;
}

void addNodeToBucket(int bucket_idx, bucket_node* node)
{

	if (Buckets[bucket_idx] == NULL)
	{
		Buckets[bucket_idx] = node;
		node->prev = NULL;
		node->next = NULL;
	}
	else
	{
		node->prev = Buckets[bucket_idx];
		node->next = NULL;
		Buckets[bucket_idx]->next = node;
	}
}

void removeNodeFromBucket(int bucket_idx, bucket_node* node)
{
	bucket_node* head = Buckets[bucket_idx];

	if (head == NULL) return;

	bucket_node* curr = head;

	if (head == node)
	{
		Buckets[bucket_idx] = head->next;
		return;
	}

	curr = curr->next;
	while(curr != NULL)
	{

		if (curr == node)
		{
			bucket_node* prev_node = curr->prev;
			bucket_node* next_node = curr->next;

			prev_node->next = next_node;
			if(next_node == NULL) return;
			next_node->prev = prev_node;

			return;

			
		}
		curr = curr->next;
	}


}

void initialiseBucket(int bucket_idx)
{
	void* base_addr = alloc_from_ram(4096);

	long long b_size = bucket_sizes[bucket_idx];
	long long num_buckets = 4080/b_size;
	long long avail_bytes = num_buckets*b_size;

	page_metadata* pg_md = construct_metadata(base_addr, b_size, avail_bytes);

	void* pg_data = base_addr + 16;

	for (int i = 0; i < num_buckets; i++)
	{
		addNodeToBucket(bucket_idx, (bucket_node*)pg_data);
		pg_data += bucket_sizes[bucket_idx];
	}



}

void *mymalloc(size_t size)
{
	if (size <= 4080)
	{
		long long bucket_idx = findAptBucket(size);

		// Check for empty bucket
		if (Buckets[bucket_idx] == NULL)
		{
			initialiseBucket(bucket_idx);
		}
		void* allocated_addr = Buckets[bucket_idx];

		removeNodeFromBucket(bucket_idx, (bucket_node*) allocated_addr);

		page_metadata* pg_md = (page_metadata*)((long)allocated_addr & 0xFFFFFFFFFFFFF000);

		pg_md->available_bytes -= pg_md->bucket_size;

		return allocated_addr;

	}

	else // size > 4080
	{
		size_t extraPagesSize = size - 4080;

		long long num_extra_pages = ceil(extraPagesSize/4096);

		long long total_size = (num_extra_pages + 1)*4096;

		void* base_addr = alloc_from_ram(total_size);

		page_metadata* pg_md = construct_metadata(base_addr, total_size, total_size - 16);

		return (base_addr + 16);
	}

	return NULL;
}

int findBucket(int bucket_idx, bucket_node* node)
{
	bucket_node* head = Buckets[bucket_idx];
	if (head == NULL) return 0;

	bucket_node* curr = head;

	while(curr != NULL)
	{
		if (curr == node) return 1;
	}

	return 0;
}
void myfree(void *ptr)
{
	page_metadata* pg_md = (page_metadata*)((long)ptr & 0xFFFFFFFFFFFFF000);

	long long size = pg_md->bucket_size;
	

	if (size > 4080)
	{
		long long total_size =  (pg_md->available_bytes + 16);
		long long num_extra_pages = total_size/4096 - 1;
		free_ram((void*)pg_md, 4096);

		for (int i = 1; i <= num_extra_pages; i++)
		{
			free_ram((void*)(pg_md) + 4096*i, 4096);
		}

		return;

	}
	else
	{
		long long available_bytes_after_free = pg_md->bucket_size + pg_md->available_bytes;
		long long num_buckets = 4080/pg_md->bucket_size;
		long long usable_page_size = num_buckets * pg_md->bucket_size;

		long long bucket_idx = findAptBucket(size);
		// Free the page
		if (available_bytes_after_free == usable_page_size)
		{
			void* curr_addr = (void*) pg_md + 16;
			for (int i = 0; i < num_buckets; i++)
			{
				if (findBucket(bucket_idx, curr_addr))
				{
					removeNodeFromBucket(bucket_idx, curr_addr);
				}
			}

			free_ram((void*)pg_md, 4096);
			return;
		}
		else
		{
			addNodeToBucket(bucket_idx, ptr);
			pg_md->available_bytes += pg_md->bucket_size;
			return;
		}
	}

}