/* Copyright (c) 2013 Scott Lembcke and Howling Moon Software
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#include "chipmunk/chipmunk_private.h"
#include <string.h>
#include "prime.h"

typedef struct cpHashSetBin {
	void *elt;
	cpHashValue hash;
	struct cpHashSetBin *next;
} cpHashSetBin;

struct cpHashSet {
	unsigned int entries, size;

	cpHashSetEqlFunc eql;
	void *default_value;

	cpHashSetBin **table;
	cpHashSetBin *pooledBins;

	cpArray *allocatedBuffers;
};

void
cpHashSetFree(cpHashSet *set)
{
	if(set){
		cpfree(set->table);

		cpArrayFreeEach(set->allocatedBuffers, cpfree);
		cpArrayFree(set->allocatedBuffers);

		cpfree(set);
	}
}

cpHashSet *
cpHashSetNew(int size, cpHashSetEqlFunc eqlFunc)
{
	cpHashSet *set = (cpHashSet *)cpcalloc(1, sizeof(cpHashSet));

	set->size = next_prime(size);
	set->entries = 0;

	set->eql = eqlFunc;
	set->default_value = NULL;

	set->table = (cpHashSetBin **)cpcalloc(set->size, sizeof(cpHashSetBin *));
	set->pooledBins = NULL;

	set->allocatedBuffers = cpArrayNew(0);

	return set;
}

void
cpHashSetSetDefaultValue(cpHashSet *set, void *default_value)
{
	set->default_value = default_value;
}

static int
setIsFull(cpHashSet *set)
{
	return (set->entries >= set->size);
}

static void
cpHashSetResize(cpHashSet *set)
{

	unsigned int newSize = next_prime(set->size + 1);

	cpHashSetBin **newTable = (cpHashSetBin **)cpcalloc(newSize, sizeof(cpHashSetBin *));


	for(unsigned int i=0; i<set->size; i++){

		cpHashSetBin *bin = set->table[i];
		while(bin){
			cpHashSetBin *next = bin->next;

			cpHashValue idx = bin->hash%newSize;
			bin->next = newTable[idx];
			newTable[idx] = bin;

			bin = next;
		}
	}

	cpfree(set->table);

	set->table = newTable;
	set->size = newSize;
}

static inline void
recycleBin(cpHashSet *set, cpHashSetBin *bin)
{
	bin->next = set->pooledBins;
	set->pooledBins = bin;
	bin->elt = NULL;
}

static cpHashSetBin *
getUnusedBin(cpHashSet *set)
{
	cpHashSetBin *bin = set->pooledBins;

	if(bin){
		set->pooledBins = bin->next;
		return bin;
	} else {

		int count = CP_BUFFER_BYTES/sizeof(cpHashSetBin);
		cpAssertHard(count, "Internal Error: Buffer size is too small.");

		cpHashSetBin *buffer = (cpHashSetBin *)cpcalloc(1, CP_BUFFER_BYTES);
		cpArrayPush(set->allocatedBuffers, buffer);


		for(int i=1; i<count; i++) recycleBin(set, buffer + i);
		return buffer;
	}
}

int
cpHashSetCount(cpHashSet *set)
{
	return set->entries;
}

void *
cpHashSetInsert(cpHashSet *set, cpHashValue hash, void *ptr, cpHashSetTransFunc trans, void *data)
{
	cpHashValue idx = hash%set->size;


	cpHashSetBin *bin = set->table[idx];

	while(bin && (bin->hash != hash || !set->eql(ptr, bin->elt)))
		bin = bin->next;


	if(!bin){
		bin = getUnusedBin(set);
		bin->hash = hash;
		bin->elt = (trans ? trans(ptr, data) : data);

		bin->next = set->table[idx];
		set->table[idx] = bin;

		set->entries++;
		if(setIsFull(set)) cpHashSetResize(set);
	}

	return bin->elt;
}

void *
cpHashSetRemove(cpHashSet *set, cpHashValue hash, void *ptr)
{
	cpHashValue idx = hash%set->size;

	cpHashSetBin **prev_ptr = &set->table[idx];
	cpHashSetBin *bin = set->table[idx];


	while(bin && !set->eql(ptr, bin->elt)){
		prev_ptr = &bin->next;
		bin = bin->next;
	}


	if(bin){

		(*prev_ptr) = bin->next;
		set->entries--;

		void *elt = bin->elt;
		recycleBin(set, bin);

		return elt;
	}

	return NULL;
}

void *
cpHashSetFind(cpHashSet *set, cpHashValue hash, void *ptr)
{
	cpHashValue idx = hash%set->size;
	cpHashSetBin *bin = set->table[idx];
	while(bin && (bin->hash != hash || !set->eql(ptr, bin->elt)))
		bin = bin->next;

	return (bin ? bin->elt : set->default_value);
}

void
cpHashSetEach(cpHashSet *set, cpHashSetIteratorFunc func, void *data)
{
	for(unsigned int i=0; i<set->size; i++){
		cpHashSetBin *bin = set->table[i];
		while(bin){
			cpHashSetBin *next = bin->next;
			func(bin->elt, data);
			bin = next;
		}
	}
}

void
cpHashSetFilter(cpHashSet *set, cpHashSetFilterFunc func, void *data)
{
	for(unsigned int i=0; i<set->size; i++){

		cpHashSetBin **prev_ptr = &set->table[i];
		cpHashSetBin *bin = set->table[i];
		while(bin){
			cpHashSetBin *next = bin->next;

			if(func(bin->elt, data)){
				prev_ptr = &bin->next;
			} else {
				(*prev_ptr) = next;

				set->entries--;
				recycleBin(set, bin);
			}

			bin = next;
		}
	}
}


#include <xmmintrin.h>
void cpHashSetDXPrefetch(cpHashSet *set, cpHashValue hash, int level)
{
	cpHashValue idx = hash%set->size;
	if(level == 1){ _mm_prefetch((const char *)&set->table[idx], _MM_HINT_T0); return; }
	cpHashSetBin *bin = set->table[idx];
	if(bin){ _mm_prefetch((const char *)bin, _MM_HINT_T0); if(bin->elt) _mm_prefetch((const char *)bin->elt, _MM_HINT_T0); }
}


void cpHashSetDXReserve(cpHashSet *set, unsigned int extra)
{
	while(set->entries + extra >= set->size) cpHashSetResize(set);
}
void cpHashSetDXInsertNew(cpHashSet *set, cpHashValue hash, void *elt)
{
	cpHashValue idx = hash%set->size;
	cpHashSetBin *bin = getUnusedBin(set);
	bin->hash = hash; bin->elt = elt;
	bin->next = set->table[idx]; set->table[idx] = bin;
	set->entries++;
}


#include "dxpool.h"
typedef int (*cpHashSetDXClassifyFunc)(void *elt, void *data);
typedef void (*cpHashSetDXPostFunc)(void *elt, int act, void *data);
struct dx_hs_chunk { cpHashSetBin *removed; int nremoved; void **elts; int *acts; int n, cap; };
static struct dx_hs_chunk *dx_hs_chunks; static int dx_hs_cap;
struct dx_hs_ctx { cpHashSet *set; cpHashSetDXClassifyFunc classify; void *data; int chunk; };
static void dx_hs_range(void *c, int b, int e, int w)
{
	struct dx_hs_ctx *x = (struct dx_hs_ctx *)c; cpHashSet *set = x->set;
	struct dx_hs_chunk *ch = &dx_hs_chunks[b / x->chunk]; ch->removed = NULL; ch->nremoved = 0; ch->n = 0;
	for(int i=b; i<e; i++){
		cpHashSetBin **prev_ptr = &set->table[i]; cpHashSetBin *bin = set->table[i];
		while(bin){
			cpHashSetBin *next = bin->next;
			int act = x->classify(bin->elt, x->data);
			if(act & ~1){ if(ch->n == ch->cap){ ch->cap = ch->cap ? ch->cap*2 : 64; ch->elts = (void **)cprealloc(ch->elts, ch->cap*sizeof(void *)); ch->acts = (int *)cprealloc(ch->acts, ch->cap*sizeof(int)); } ch->elts[ch->n] = bin->elt; ch->acts[ch->n++] = act; }
			else if(act & 1){ if(ch->n == ch->cap){ ch->cap = ch->cap ? ch->cap*2 : 64; ch->elts = (void **)cprealloc(ch->elts, ch->cap*sizeof(void *)); ch->acts = (int *)cprealloc(ch->acts, ch->cap*sizeof(int)); } ch->elts[ch->n] = bin->elt; ch->acts[ch->n++] = act; }
			if(act & 1){ (*prev_ptr) = next; bin->next = ch->removed; ch->removed = bin; ch->nremoved++; }
			else prev_ptr = &bin->next;
			bin = next;
		}
	}
}
void cpHashSetDXFilterParallel(cpHashSet *set, cpHashSetDXClassifyFunc classify, cpHashSetDXPostFunc post, void *data)
{
	const int chunk = 512; int n = (int)set->size, chunks = (n + chunk - 1) / chunk;
	if(chunks > dx_hs_cap){ int old = dx_hs_cap; dx_hs_cap = chunks*2; dx_hs_chunks = (struct dx_hs_chunk *)cprealloc(dx_hs_chunks, dx_hs_cap*sizeof(struct dx_hs_chunk)); memset(dx_hs_chunks + old, 0, (dx_hs_cap - old)*sizeof(struct dx_hs_chunk)); }
	struct dx_hs_ctx x = {set, classify, data, chunk};
	dx_parallel_for(n, chunk, 0, dx_hs_range, &x);
	for(int c=0; c<chunks; c++){
		struct dx_hs_chunk *ch = &dx_hs_chunks[c];
		for(int k=0; k<ch->n; k++) post(ch->elts[k], ch->acts[k], data);
		cpHashSetBin *bin = ch->removed; while(bin){ cpHashSetBin *next = bin->next; recycleBin(set, bin); bin = next; }
		set->entries -= ch->nremoved;
	}
}
