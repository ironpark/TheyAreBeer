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
#include "dxpool.h"
#ifdef DX_PROFILE
extern double dx_prof[32]; double dx_now(void);
#endif

cpPostStepCallback *
cpSpaceGetPostStepCallback(cpSpace *space, void *key)
{
	cpArray *arr = space->postStepCallbacks;
	for(int i=0; i<arr->num; i++){
		cpPostStepCallback *callback = (cpPostStepCallback *)arr->arr[i];
		if(callback && callback->key == key) return callback;
	}

	return NULL;
}

static void PostStepDoNothing(cpSpace *space, void *obj, void *data){}

cpBool
cpSpaceAddPostStepCallback(cpSpace *space, cpPostStepFunc func, void *key, void *data)
{
	cpAssertWarn(space->locked,
		"Adding a post-step callback when the space is not locked is unnecessary. "
		"Post-step callbacks will not called until the end of the next call to cpSpaceStep() or the next query.");

	if(!cpSpaceGetPostStepCallback(space, key)){
		cpPostStepCallback *callback = (cpPostStepCallback *)cpcalloc(1, sizeof(cpPostStepCallback));
		callback->func = (func ? func : PostStepDoNothing);
		callback->key = key;
		callback->data = data;

		cpArrayPush(space->postStepCallbacks, callback);
		return cpTrue;
	} else {
		return cpFalse;
	}
}

void
cpSpaceLock(cpSpace *space)
{
	space->locked++;
}

void
cpSpaceUnlock(cpSpace *space, cpBool runPostStep)
{
	space->locked--;
	cpAssertHard(space->locked >= 0, "Internal Error: Space lock underflow.");

	if(space->locked == 0){
		cpArray *waking = space->rousedBodies;

		for(int i=0, count=waking->num; i<count; i++){
			cpSpaceActivateBody(space, (cpBody *)waking->arr[i]);
			waking->arr[i] = NULL;
		}

		waking->num = 0;

		if(space->locked == 0 && runPostStep && !space->skipPostStep){
			space->skipPostStep = cpTrue;

			cpArray *arr = space->postStepCallbacks;
			for(int i=0; i<arr->num; i++){
				cpPostStepCallback *callback = (cpPostStepCallback *)arr->arr[i];
				cpPostStepFunc func = callback->func;

				callback->func = NULL;
				if(func) func(space, callback->key, callback->data);

				arr->arr[i] = NULL;
				cpfree(callback);
			}

			arr->num = 0;
			space->skipPostStep = cpFalse;
		}
	}
}

struct cpContactBufferHeader {
	cpTimestamp stamp;
	cpContactBufferHeader *next;
	unsigned int numContacts;
};

#define CP_CONTACTS_BUFFER_SIZE ((CP_BUFFER_BYTES - sizeof(cpContactBufferHeader))/sizeof(struct cpContact))
typedef struct cpContactBuffer {
	cpContactBufferHeader header;
	struct cpContact contacts[CP_CONTACTS_BUFFER_SIZE];
} cpContactBuffer;

static cpContactBufferHeader *
cpSpaceAllocContactBuffer(cpSpace *space)
{
	cpContactBuffer *buffer = (cpContactBuffer *)cpcalloc(1, sizeof(cpContactBuffer));
	cpArrayPush(space->allocatedBuffers, buffer);
	return (cpContactBufferHeader *)buffer;
}

static cpContactBufferHeader *
cpContactBufferHeaderInit(cpContactBufferHeader *header, cpTimestamp stamp, cpContactBufferHeader *splice)
{
	header->stamp = stamp;
	header->next = (splice ? splice->next : header);
	header->numContacts = 0;

	return header;
}

void
cpSpacePushFreshContactBuffer(cpSpace *space)
{
	cpTimestamp stamp = space->stamp;

	cpContactBufferHeader *head = space->contactBuffersHead;

	if(!head){

		space->contactBuffersHead = cpContactBufferHeaderInit(cpSpaceAllocContactBuffer(space), stamp, NULL);
	} else if(stamp - head->next->stamp > space->collisionPersistence){

	cpContactBufferHeader *tail = head->next;
		space->contactBuffersHead = cpContactBufferHeaderInit(tail, stamp, tail);
	} else {

		cpContactBufferHeader *buffer = cpContactBufferHeaderInit(cpSpaceAllocContactBuffer(space), stamp, head);
		space->contactBuffersHead = head->next = buffer;
	}
}

struct cpContact *
cpContactBufferGetArray(cpSpace *space)
{
	if(space->contactBuffersHead->numContacts + CP_MAX_CONTACTS_PER_ARBITER > CP_CONTACTS_BUFFER_SIZE){

		cpSpacePushFreshContactBuffer(space);
	}

	cpContactBufferHeader *head = space->contactBuffersHead;
	return ((cpContactBuffer *)head)->contacts + head->numContacts;
}

void
cpSpacePushContacts(cpSpace *space, int count)
{
	cpAssertHard(count <= CP_MAX_CONTACTS_PER_ARBITER, "Internal Error: Contact buffer overflow!");
	space->contactBuffersHead->numContacts += count;
}

static void
cpSpacePopContacts(cpSpace *space, int count){
	space->contactBuffersHead->numContacts -= count;
}

static void *
cpSpaceArbiterSetTrans(cpShape **shapes, cpSpace *space)
{
	if(space->pooledArbiters->num == 0){

		int count = CP_BUFFER_BYTES/sizeof(cpArbiter);
		cpAssertHard(count, "Internal Error: Buffer size too small.");

		cpArbiter *buffer = (cpArbiter *)cpcalloc(1, CP_BUFFER_BYTES);
		cpArrayPush(space->allocatedBuffers, buffer);

		for(int i=0; i<count; i++) cpArrayPush(space->pooledArbiters, buffer + i);
	}

	return cpArbiterInit((cpArbiter *)cpArrayPop(space->pooledArbiters), shapes[0], shapes[1]);
}

static inline cpBool
QueryRejectConstraint(cpBody *a, cpBody *b)
{
	CP_BODY_FOREACH_CONSTRAINT(a, constraint){
		if(
			!constraint->collideBodies && (
				(constraint->a == a && constraint->b == b) ||
				(constraint->a == b && constraint->b == a)
			)
		) return cpTrue;
	}

	return cpFalse;
}

static inline cpBool
QueryReject(cpShape *a, cpShape *b)
{
	return (

		!cpBBIntersects(a->bb, b->bb)

		|| a->body == b->body

		|| cpShapeFilterReject(a->filter, b->filter)

		|| QueryRejectConstraint(a->body, b->body)
	);
}

cpCollisionID
cpSpaceCollideShapes(cpShape *a, cpShape *b, cpCollisionID id, cpSpace *space)
{

	if(QueryReject(a,b)) return id;
#ifdef DX_PROFILE
	double _n0 = dx_now(); dx_prof[18] += 1;
#endif

	struct cpCollisionInfo info = cpCollide(a, b, id, cpContactBufferGetArray(space));
#ifdef DX_PROFILE
	double _n1 = dx_now(); dx_prof[16] += _n1 - _n0;
	if(info.count == 0) return info.id;
#endif

	if(info.count == 0) return info.id;
	cpSpacePushContacts(space, info.count);

	const cpShape *shape_pair[] = {info.a, info.b};
	cpHashValue arbHashID = CP_HASH_PAIR((cpHashValue)info.a, (cpHashValue)info.b);
	cpArbiter *arb = (cpArbiter *)cpHashSetInsert(space->cachedArbiters, arbHashID, shape_pair, (cpHashSetTransFunc)cpSpaceArbiterSetTrans, space);
	cpArbiterUpdate(arb, &info, space);

	cpCollisionHandler *handler = arb->handler;

	if(arb->state == CP_ARBITER_STATE_FIRST_COLLISION && !handler->beginFunc(arb, space, handler->userData)){
		cpArbiterIgnore(arb);
	}

	if(

		(arb->state != CP_ARBITER_STATE_IGNORE) &&

		handler->preSolveFunc(arb, space, handler->userData) &&

		arb->state != CP_ARBITER_STATE_IGNORE &&

		!(a->sensor || b->sensor) &&

		!(a->body->m == INFINITY && b->body->m == INFINITY)
	){
		cpArrayPush(space->arbiters, arb);
	} else {
		cpSpacePopContacts(space, info.count);

		arb->contacts = NULL;
		arb->count = 0;

		if(arb->state != CP_ARBITER_STATE_IGNORE) arb->state = CP_ARBITER_STATE_NORMAL;
	}

	arb->stamp = space->stamp;
#ifdef DX_PROFILE
	dx_prof[17] += dx_now() - _n1;
#endif
	return info.id;
}

int dx_par_collide = 1;
void cpBBTreeDXUpdateLeaves(cpSpatialIndex *index);
void cpHashSetDXPrefetch(cpHashSet *set, cpHashValue hash, int level);

struct dx_rec { cpShape *a, *b; struct cpCollisionInfo info; struct cpContact contacts[CP_MAX_CONTACTS_PER_ARBITER]; };
struct dx_chunk_out { struct dx_rec *recs; int num, cap; };

static cpShape **dx_cshapes; static int dx_ccap;
static int *dx_cx0, *dx_cy0, *dx_cx1, *dx_cy1;
static int *dx_cell_start, dx_cell_cap; static int *dx_cell_items, dx_items_cap;
static int dx_gx0, dx_gy0, dx_gw, dx_gh;
static struct dx_chunk_out *dx_outs; static int dx_outs_cap;
#define DX_COLLIDE_CHUNK 64
#define DX_CELL 1.0f

static void dx_out_push(struct dx_chunk_out *o, cpShape *a, cpShape *b, struct cpCollisionInfo *info)
{
	if(o->num == o->cap){ o->cap = o->cap ? o->cap*2 : 256; o->recs = (struct dx_rec *)cprealloc(o->recs, o->cap*sizeof(struct dx_rec)); }
	struct dx_rec *r = &o->recs[o->num++];
	r->a = a; r->b = b; r->info = *info;
	for(int k=0; k<info->count; k++) r->contacts[k] = info->arr[k];
}

struct dx_static_ctx { cpShape *a; struct dx_chunk_out *out; };
static cpCollisionID dx_static_pair(cpShape *a, cpShape *b, cpCollisionID id, struct dx_static_ctx *ctx)
{
	if(QueryReject(a, b)) return id;
	struct cpContact tmp[CP_MAX_CONTACTS_PER_ARBITER];
	struct cpCollisionInfo info = cpCollide(a, b, id, tmp);
	if(info.count) dx_out_push(ctx->out, a, b, &info);
	return info.id;
}

struct dx_collide_ctx { cpSpace *space; int n; };
static void dx_collide_range(void *c, int begin, int end, int w)
{
	struct dx_collide_ctx *x = (struct dx_collide_ctx *)c;
	struct dx_chunk_out *out = &dx_outs[begin / DX_COLLIDE_CHUNK];
	out->num = 0;
	struct cpContact tmp[CP_MAX_CONTACTS_PER_ARBITER];
	for(int i=begin; i<end; i++){
		cpShape *a = dx_cshapes[i];

		struct dx_static_ctx sc = {a, out};
		cpSpatialIndexQuery(x->space->staticShapes, a, a->bb, (cpSpatialIndexQueryFunc)dx_static_pair, &sc);

		int ax0 = dx_cx0[i], ay0 = dx_cy0[i], ax1 = dx_cx1[i], ay1 = dx_cy1[i];
		for(int gx=ax0; gx<=ax1; gx++) for(int gy=ay0; gy<=ay1; gy++){
			int cell = (gx - dx_gx0)*dx_gh + (gy - dx_gy0);
			for(int k=dx_cell_start[cell]; k<dx_cell_start[cell+1]; k++){
				int j = dx_cell_items[k]; if(j <= i) continue;
				if(gx != (ax0 > dx_cx0[j] ? ax0 : dx_cx0[j]) || gy != (ay0 > dx_cy0[j] ? ay0 : dx_cy0[j])) continue;
				cpShape *b = dx_cshapes[j];
				if(QueryReject(a, b)) continue;
				struct cpCollisionInfo info = cpCollide(a, b, 0, tmp);
				if(info.count) dx_out_push(out, a, b, &info);
			}
		}
	}
}

static void dx_record(cpSpace *space, cpShape *a, cpShape *b, struct cpCollisionInfo info, struct cpContact *contacts)
{
	struct cpContact *dst = cpContactBufferGetArray(space);
	for(int k=0; k<info.count; k++) dst[k] = contacts[k];
	info.arr = dst;
	cpSpacePushContacts(space, info.count);

	const cpShape *shape_pair[] = {info.a, info.b};
	cpHashValue arbHashID = CP_HASH_PAIR((cpHashValue)info.a, (cpHashValue)info.b);
	cpArbiter *arb = (cpArbiter *)cpHashSetInsert(space->cachedArbiters, arbHashID, shape_pair, (cpHashSetTransFunc)cpSpaceArbiterSetTrans, space);
	cpArbiterUpdate(arb, &info, space);

	cpCollisionHandler *handler = arb->handler;
	if(arb->state == CP_ARBITER_STATE_FIRST_COLLISION && !handler->beginFunc(arb, space, handler->userData)){
		cpArbiterIgnore(arb);
	}
	if(
		(arb->state != CP_ARBITER_STATE_IGNORE) &&
		handler->preSolveFunc(arb, space, handler->userData) &&
		arb->state != CP_ARBITER_STATE_IGNORE &&
		!(a->sensor || b->sensor) &&
		!(a->body->m == INFINITY && b->body->m == INFINITY)
	){
		cpArrayPush(space->arbiters, arb);
	} else {
		cpSpacePopContacts(space, info.count);
		arb->contacts = NULL;
		arb->count = 0;
		if(arb->state != CP_ARBITER_STATE_IGNORE) arb->state = CP_ARBITER_STATE_NORMAL;
	}
	arb->stamp = space->stamp;
}

void cpHashSetDXReserve(cpHashSet *set, unsigned int extra);
void cpHashSetDXInsertNew(cpHashSet *set, cpHashValue hash, void *elt);
int dx_par_record = 1;
static struct dx_rec **dx_R; static cpArbiter **dx_A; static unsigned char *dx_flag; static int dx_Rcap;
static struct cpContact *dx_cbuf[4]; static int dx_cbcap[4];

static void dx_rp_find(void *c, int b, int e, int w)
{
	cpSpace *space = (cpSpace *)c;
	for(int k=b; k<e; k++){
		struct dx_rec *r = dx_R[k];
		const cpShape *pair[] = {r->info.a, r->info.b};
		dx_A[k] = (cpArbiter *)cpHashSetFind(space->cachedArbiters, CP_HASH_PAIR((cpHashValue)r->info.a, (cpHashValue)r->info.b), (void *)pair);
	}
}
static void dx_rp_update(void *c, int b, int e, int w)
{
	cpSpace *space = (cpSpace *)c;
	struct cpContact *buf = dx_cbuf[space->stamp & 3];
	for(int k=b; k<e; k++){
		struct dx_rec *r = dx_R[k]; cpArbiter *arb = dx_A[k];
		struct cpCollisionInfo info = r->info; struct cpContact *dst = buf + 2*k;
		for(int i=0; i<info.count; i++) dst[i] = r->contacts[i];
		info.arr = dst;
		cpArbiterUpdate(arb, &info, space);
		if(arb->handler == &space->defaultHandler){
			cpShape *a = r->a, *bb = r->b;
			int push = (arb->state != CP_ARBITER_STATE_IGNORE) && !(a->sensor || bb->sensor) && !(a->body->m == INFINITY && bb->body->m == INFINITY);
			if(!push){ arb->contacts = NULL; arb->count = 0; if(arb->state != CP_ARBITER_STATE_IGNORE) arb->state = CP_ARBITER_STATE_NORMAL; }
			arb->stamp = space->stamp;
			dx_flag[k] = (unsigned char)push;
		} else dx_flag[k] = 2;
	}
}
static void dx_record_parallel(cpSpace *space, int chunks, int m)
{
	if(m > dx_Rcap){ dx_Rcap = m*2; dx_R = (struct dx_rec **)cprealloc(dx_R, dx_Rcap*sizeof(*dx_R)); dx_A = (cpArbiter **)cprealloc(dx_A, dx_Rcap*sizeof(*dx_A)); dx_flag = (unsigned char *)cprealloc(dx_flag, dx_Rcap); }
	int k = 0;
	for(int ch=0; ch<chunks; ch++){ struct dx_chunk_out *o = &dx_outs[ch]; for(int i=0; i<o->num; i++) dx_R[k++] = &o->recs[i]; }
	int g = (int)(space->stamp & 3);
	if(2*m > dx_cbcap[g]){ dx_cbcap[g] = 4*m; dx_cbuf[g] = (struct cpContact *)cprealloc(dx_cbuf[g], dx_cbcap[g]*sizeof(struct cpContact)); }
	dx_parallel_for(m, 256, 0, dx_rp_find, space);
	int newc = 0; for(int i=0; i<m; i++) if(!dx_A[i]) newc++;
	if(newc){
		cpHashSetDXReserve(space->cachedArbiters, (unsigned int)newc);
		for(int i=0; i<m; i++){
			if(dx_A[i]) continue;
			struct dx_rec *r = dx_R[i];
			cpShape *pair[] = {(cpShape *)r->info.a, (cpShape *)r->info.b};
			cpArbiter *arb = (cpArbiter *)cpSpaceArbiterSetTrans(pair, space);
			cpHashSetDXInsertNew(space->cachedArbiters, CP_HASH_PAIR((cpHashValue)r->info.a, (cpHashValue)r->info.b), arb);
			dx_A[i] = arb;
		}
	}
	dx_parallel_for(m, 256, 0, dx_rp_update, space);
	for(int i=0; i<m; i++){
		cpArbiter *arb = dx_A[i];
		if(dx_flag[i] == 1){ cpArrayPush(space->arbiters, arb); continue; }
		if(dx_flag[i] == 0) continue;

		struct dx_rec *r = dx_R[i]; cpShape *a = r->a, *b = r->b;
		cpCollisionHandler *handler = arb->handler;
		if(arb->state == CP_ARBITER_STATE_FIRST_COLLISION && !handler->beginFunc(arb, space, handler->userData)) cpArbiterIgnore(arb);
		if((arb->state != CP_ARBITER_STATE_IGNORE) && handler->preSolveFunc(arb, space, handler->userData) && arb->state != CP_ARBITER_STATE_IGNORE &&
			!(a->sensor || b->sensor) && !(a->body->m == INFINITY && b->body->m == INFINITY)){
			cpArrayPush(space->arbiters, arb);
		} else {
			arb->contacts = NULL; arb->count = 0;
			if(arb->state != CP_ARBITER_STATE_IGNORE) arb->state = CP_ARBITER_STATE_NORMAL;
		}
		arb->stamp = space->stamp;
	}
}

static int dx_collide(cpSpace *space)
{
	cpArray *bodies = space->dynamicBodies;
	int n = 0;
	for(int i=0; i<bodies->num; i++){ cpBody *body = (cpBody *)bodies->arr[i]; CP_BODY_FOREACH_SHAPE(body, shape) n++; }
	if(!dx_par_collide || dx_pool_threads() == 0 || n < 2048) return 0;

#ifdef DX_PROFILE
	double _c0 = dx_now();
#endif

	cpBBTreeDXUpdateLeaves(space->dynamicShapes);
#ifdef DX_PROFILE
	double _c1 = dx_now(); dx_prof[14] += _c1 - _c0;
#endif

	if(n > dx_ccap){
		dx_ccap = n*2;
		dx_cshapes = (cpShape **)cprealloc(dx_cshapes, dx_ccap*sizeof(cpShape *));
		dx_cx0 = (int *)cprealloc(dx_cx0, dx_ccap*sizeof(int)); dx_cy0 = (int *)cprealloc(dx_cy0, dx_ccap*sizeof(int));
		dx_cx1 = (int *)cprealloc(dx_cx1, dx_ccap*sizeof(int)); dx_cy1 = (int *)cprealloc(dx_cy1, dx_ccap*sizeof(int));
	}
	n = 0;
	float minx = INFINITY, miny = INFINITY, maxx = -INFINITY, maxy = -INFINITY;
	for(int i=0; i<bodies->num; i++){
		cpBody *body = (cpBody *)bodies->arr[i];
		CP_BODY_FOREACH_SHAPE(body, shape){
			cpBB bb = shape->bb;

			if(!(isfinite(bb.l) && isfinite(bb.b) && isfinite(bb.r) && isfinite(bb.t))) continue;
			if(bb.l < minx) minx = bb.l; if(bb.b < miny) miny = bb.b; if(bb.r > maxx) maxx = bb.r; if(bb.t > maxy) maxy = bb.t;
			dx_cshapes[n++] = shape;
		}
	}

	if(!(maxx - minx < 4096.0f && maxy - miny < 4096.0f)) return 0;
	dx_gx0 = (int)floorf(minx / DX_CELL); dx_gy0 = (int)floorf(miny / DX_CELL);
	dx_gw = (int)floorf(maxx / DX_CELL) - dx_gx0 + 1; dx_gh = (int)floorf(maxy / DX_CELL) - dx_gy0 + 1;
	int cells = dx_gw*dx_gh;
	if(cells + 1 > dx_cell_cap){ dx_cell_cap = (cells + 1)*2; dx_cell_start = (int *)cprealloc(dx_cell_start, dx_cell_cap*sizeof(int)); }
	memset(dx_cell_start, 0, (cells + 1)*sizeof(int));
	int items = 0;
	for(int i=0; i<n; i++){
		cpBB bb = dx_cshapes[i]->bb;
		int x0 = (int)floorf(bb.l / DX_CELL) - dx_gx0, y0 = (int)floorf(bb.b / DX_CELL) - dx_gy0, x1 = (int)floorf(bb.r / DX_CELL) - dx_gx0, y1 = (int)floorf(bb.t / DX_CELL) - dx_gy0;
		dx_cx0[i] = x0 + dx_gx0; dx_cy0[i] = y0 + dx_gy0; dx_cx1[i] = x1 + dx_gx0; dx_cy1[i] = y1 + dx_gy0;
		for(int gx=x0; gx<=x1; gx++) for(int gy=y0; gy<=y1; gy++){ dx_cell_start[gx*dx_gh + gy + 1]++; items++; }
	}
	for(int c=0; c<cells; c++) dx_cell_start[c+1] += dx_cell_start[c];
	if(items > dx_items_cap){ dx_items_cap = items*2; dx_cell_items = (int *)cprealloc(dx_cell_items, dx_items_cap*sizeof(int)); }
	{
		int *fill = (int *)cpcalloc(cells, sizeof(int));
		for(int i=0; i<n; i++){
			int x0 = dx_cx0[i] - dx_gx0, y0 = dx_cy0[i] - dx_gy0, x1 = dx_cx1[i] - dx_gx0, y1 = dx_cy1[i] - dx_gy0;
			for(int gx=x0; gx<=x1; gx++) for(int gy=y0; gy<=y1; gy++){ int c = gx*dx_gh + gy; dx_cell_items[dx_cell_start[c] + fill[c]++] = i; }
		}
		cpfree(fill);
	}

	int chunks = (n + DX_COLLIDE_CHUNK - 1) / DX_COLLIDE_CHUNK;
	if(chunks > dx_outs_cap){ int old = dx_outs_cap; dx_outs_cap = chunks*2; dx_outs = (struct dx_chunk_out *)cprealloc(dx_outs, dx_outs_cap*sizeof(struct dx_chunk_out)); memset(dx_outs + old, 0, (dx_outs_cap - old)*sizeof(struct dx_chunk_out)); }
#ifdef DX_PROFILE
	double _c2 = dx_now(); dx_prof[19] += _c2 - _c1;
#endif
	struct dx_collide_ctx cc = {space, n};
	dx_parallel_for(n, DX_COLLIDE_CHUNK, 0, dx_collide_range, &cc);
#ifdef DX_PROFILE
	double _c3 = dx_now(); dx_prof[16] += _c3 - _c2;
#endif

	int mrec = 0; for(int ch=0; ch<chunks; ch++) mrec += dx_outs[ch].num;
	if(dx_par_record && mrec >= 4096) dx_record_parallel(space, chunks, mrec); else
	for(int ch=0; ch<chunks; ch++){
		struct dx_chunk_out *o = &dx_outs[ch];
		for(int k=0; k<o->num; k++){
			if(k + 8 < o->num){ struct dx_rec *p = &o->recs[k + 8]; cpHashSetDXPrefetch(space->cachedArbiters, CP_HASH_PAIR((cpHashValue)p->info.a, (cpHashValue)p->info.b), 1); }
			if(k + 4 < o->num){ struct dx_rec *p = &o->recs[k + 4]; cpHashSetDXPrefetch(space->cachedArbiters, CP_HASH_PAIR((cpHashValue)p->info.a, (cpHashValue)p->info.b), 2); }
			struct dx_rec *r = &o->recs[k];
			dx_record(space, r->a, r->b, r->info, r->contacts);
		}
#ifdef DX_PROFILE
		dx_prof[18] += o->num;
#endif
	}
#ifdef DX_PROFILE
	dx_prof[17] += dx_now() - _c3;
#endif
	return 1;
}

typedef int (*cpHashSetDXClassifyFunc)(void *elt, void *data);
typedef void (*cpHashSetDXPostFunc)(void *elt, int act, void *data);
void cpHashSetDXFilterParallel(cpHashSet *set, cpHashSetDXClassifyFunc classify, cpHashSetDXPostFunc post, void *data);
int dx_par_filter = 1;
int dx_fat_enabled = 1;
static int dx_arb_classify(cpArbiter *arb, cpSpace *space)
{
	if(arb->a->space == NULL || arb->b->space == NULL){ int sep = (arb->state != CP_ARBITER_STATE_CACHED); if(sep) arb->state = CP_ARBITER_STATE_INVALIDATED; return 1 | 4 | (sep ? 2 : 0); }
	cpTimestamp ticks = space->stamp - arb->stamp;
	cpBody *a = arb->body_a, *b = arb->body_b;
	if((cpBodyGetType(a) == CP_BODY_TYPE_STATIC || cpBodyIsSleeping(a)) && (cpBodyGetType(b) == CP_BODY_TYPE_STATIC || cpBodyIsSleeping(b))) return 0;
	int act = 0;
	if(ticks >= 1 && arb->state != CP_ARBITER_STATE_CACHED){ arb->state = CP_ARBITER_STATE_CACHED; act |= 2; }
	if(ticks >= space->collisionPersistence){ arb->contacts = NULL; arb->count = 0; act |= 1; }
	return act;
}
static void dx_arb_post(cpArbiter *arb, int act, cpSpace *space)
{
	if(act & 2){ cpCollisionHandler *handler = arb->handler; handler->separateFunc(arb, space, handler->userData); }
	if(act & 4) cpArbiterUnthread(arb);
	if(act & 1) cpArrayPush(space->pooledArbiters, arb);
}

cpBool
cpSpaceArbiterSetFilter(cpArbiter *arb, cpSpace *space)
{

	if(arb->a->space == NULL || arb->b->space == NULL){
		if(arb->state != CP_ARBITER_STATE_CACHED){
			arb->state = CP_ARBITER_STATE_INVALIDATED;
			cpCollisionHandler *handler = arb->handler;
			handler->separateFunc(arb, space, handler->userData);
		}
		cpArbiterUnthread(arb);
		cpArrayPush(space->pooledArbiters, arb);
		return cpFalse;
	}

	cpTimestamp ticks = space->stamp - arb->stamp;

	cpBody *a = arb->body_a, *b = arb->body_b;

	if(
		(cpBodyGetType(a) == CP_BODY_TYPE_STATIC || cpBodyIsSleeping(a)) &&
		(cpBodyGetType(b) == CP_BODY_TYPE_STATIC || cpBodyIsSleeping(b))
	){
		return cpTrue;
	}

	if(ticks >= 1 && arb->state != CP_ARBITER_STATE_CACHED){
		arb->state = CP_ARBITER_STATE_CACHED;
		cpCollisionHandler *handler = arb->handler;
		handler->separateFunc(arb, space, handler->userData);
	}

	if(ticks >= space->collisionPersistence){
		arb->contacts = NULL;
		arb->count = 0;

		cpArrayPush(space->pooledArbiters, arb);
		return cpFalse;
	}

	return cpTrue;
}

 void
cpShapeUpdateFunc(cpShape *shape, void *unused)
{
	cpShapeCacheBB(shape);
}

#include "dxpool.h"
#include <intrin.h>
int dx_par_independent = 1;
#define DX_CHUNK 256
#define DX_MINPAR 2048
struct dx_step_ctx { cpSpace *space; cpFloat dt, slop, biasCoef, damping; cpVect gravity; };
static void dx_positions(void *c, int b, int e, int w){ struct dx_step_ctx *x = (struct dx_step_ctx *)c; cpArray *bodies = x->space->dynamicBodies; for(int i=b; i<e; i++){ cpBody *body = (cpBody *)bodies->arr[i]; body->position_func(body, x->dt); } }
static void dx_shapes(void *c, int b, int e, int w){ struct dx_step_ctx *x = (struct dx_step_ctx *)c; cpArray *bodies = x->space->dynamicBodies; for(int i=b; i<e; i++){ cpBody *body = (cpBody *)bodies->arr[i]; CP_BODY_FOREACH_SHAPE(body, shape) cpShapeCacheBB(shape); } }
static void dx_prestep(void *c, int b, int e, int w){ struct dx_step_ctx *x = (struct dx_step_ctx *)c; cpArray *arbiters = x->space->arbiters; for(int i=b; i<e; i++) cpArbiterPreStep((cpArbiter *)arbiters->arr[i], x->dt, x->slop, x->biasCoef); }
static void dx_velocities(void *c, int b, int e, int w){ struct dx_step_ctx *x = (struct dx_step_ctx *)c; cpArray *bodies = x->space->dynamicBodies; for(int i=b; i<e; i++){ cpBody *body = (cpBody *)bodies->arr[i]; body->velocity_func(body, x->gravity, x->damping, x->dt); } }

int dx_par_solver = 1;
static int *dx_col; static cpArbiter **dx_sorted; static int dx_cap; static int dx_cstart[66];
int dx_last_colors;
static int dx_dynamic(cpBody *b){ return b->sleeping.idleTime != INFINITY && b->m != INFINITY; }
static void dx_color(cpSpace *space, cpArray *arbiters)
{
	int n = arbiters->num, cnt[65] = {0}, fill[65], used = 0;
	if(n > dx_cap){ dx_cap = n*2; dx_col = (int *)cprealloc(dx_col, dx_cap*sizeof(int)); dx_sorted = (cpArbiter **)cprealloc(dx_sorted, dx_cap*sizeof(cpArbiter *)); }
	cpTimestamp st = space->stamp;
	for(int j=0; j<n; j++){
		cpArbiter *arb = (cpArbiter *)arbiters->arr[j]; cpBody *a = arb->body_a, *b = arb->body_b;
		int da = dx_dynamic(a), db = dx_dynamic(b); unsigned long long m = 0;
		if(da){ if(a->dx_stamp != st){ a->dx_stamp = st; a->dx_colors = 0; } m |= a->dx_colors; }
		if(db){ if(b->dx_stamp != st){ b->dx_stamp = st; b->dx_colors = 0; } m |= b->dx_colors; }
		unsigned long idx; int c = _BitScanForward64(&idx, ~m) ? (int)idx : 64;
		if(c < 64){ unsigned long long bit = 1ULL << c; if(da) a->dx_colors |= bit; if(db) b->dx_colors |= bit; if(c >= used) used = c + 1; }
		dx_col[j] = c; cnt[c]++;
	}
	dx_cstart[0] = 0; for(int c=0; c<65; c++) dx_cstart[c+1] = dx_cstart[c] + cnt[c];
	for(int c=0; c<65; c++) fill[c] = dx_cstart[c];
	for(int j=0; j<n; j++) dx_sorted[fill[dx_col[j]]++] = (cpArbiter *)arbiters->arr[j];
	dx_last_colors = used + (cnt[64] ? 1 : 0);
}

int dx_block_solver = 1;

static float dx_block_size = 4.0f;
static int *dx_bkey; static int dx_bkcap;
static int *dx_bstart; static int dx_bscap;
static int *dx_phase_blocks[4]; static int dx_pbn[4], dx_pbcap[4];
static cpArbiter **dx_bsorted; static int dx_bsortcap;
struct dx_block_ctx { cpSpace *space; float minx, miny, bs; int bw; };
static void dx_bkeys(void *c, int b, int e, int w)
{
	struct dx_block_ctx *x = (struct dx_block_ctx *)c; cpArray *arbiters = x->space->arbiters;
	for(int i=b; i<e; i++){
		cpArbiter *arb = (cpArbiter *)arbiters->arr[i];
		cpBody *body = dx_dynamic(arb->body_a) ? arb->body_a : arb->body_b;
		int bx = (int)((body->p.x - x->minx) / x->bs), by = (int)((body->p.y - x->miny) / x->bs);
		if(bx < 0) bx = 0; if(by < 0) by = 0;
		dx_bkey[i] = by*x->bw + bx;
	}
}
static int dx_build_blocks(cpSpace *space, cpArray *arbiters)
{
	int n = arbiters->num;
	cpArray *bodies = space->dynamicBodies;
	float minx = INFINITY, miny = INFINITY, maxx = -INFINITY, maxy = -INFINITY;
	float maxd = 0.0f;
	for(int i=0; i<bodies->num; i++){
		cpBody *body = (cpBody *)bodies->arr[i]; cpVect p = body->p;
		if(p.x < minx) minx = p.x; if(p.y < miny) miny = p.y; if(p.x > maxx) maxx = p.x; if(p.y > maxy) maxy = p.y;
		if(dx_dynamic(body)) CP_BODY_FOREACH_SHAPE(body, shape){ if(shape->sensor) continue; float d = shape->bb.r - shape->bb.l, d2 = shape->bb.t - shape->bb.b; if(d2 > d) d = d2; if(d > maxd) maxd = d; }
	}
	if(!(maxx - minx < 1e5f && maxy - miny < 1e5f)) return 0;
	float bs = 2.0f*maxd + 0.5f; if(bs < 4.0f) bs = 4.0f; dx_block_size = bs;
	int bw = (int)((maxx - minx) / bs) + 2, bh = (int)((maxy - miny) / bs) + 2, nb = bw*bh;
	if(nb > 4000000) return 0;
	if(n > dx_bkcap){ dx_bkcap = n*2; dx_bkey = (int *)cprealloc(dx_bkey, dx_bkcap*sizeof(int)); }
	if(n > dx_bsortcap){ dx_bsortcap = n*2; dx_bsorted = (cpArbiter **)cprealloc(dx_bsorted, dx_bsortcap*sizeof(cpArbiter *)); }
	if(nb + 1 > dx_bscap){ dx_bscap = (nb + 1)*2; dx_bstart = (int *)cprealloc(dx_bstart, dx_bscap*sizeof(int)); }
	struct dx_block_ctx x = {space, minx, miny, bs, bw};
	dx_parallel_for(n, 1024, 0, dx_bkeys, &x);
	memset(dx_bstart, 0, (nb + 1)*sizeof(int));
	for(int i=0; i<n; i++){ int k = dx_bkey[i]; if(k >= nb) k = dx_bkey[i] = nb - 1; dx_bstart[k + 1]++; }
	for(int k=0; k<nb; k++) dx_bstart[k + 1] += dx_bstart[k];
	{ int *fill = (int *)cpcalloc(nb, sizeof(int)); for(int i=0; i<n; i++){ int k = dx_bkey[i]; dx_bsorted[dx_bstart[k] + fill[k]++] = (cpArbiter *)arbiters->arr[i]; } cpfree(fill); }
	for(int p=0; p<4; p++) dx_pbn[p] = 0;
	for(int k=0; k<nb; k++){
		if(dx_bstart[k + 1] == dx_bstart[k]) continue;
		int bx = k % bw, by = k / bw, p = (bx & 1) | ((by & 1) << 1);
		if(dx_pbn[p] == dx_pbcap[p]){ dx_pbcap[p] = dx_pbcap[p] ? dx_pbcap[p]*2 : 256; dx_phase_blocks[p] = (int *)cprealloc(dx_phase_blocks[p], dx_pbcap[p]*sizeof(int)); }
		dx_phase_blocks[p][dx_pbn[p]++] = k;
	}
	dx_last_colors = 4;
	return 1;
}
struct dx_bsolve_ctx { int phase; cpFloat dt_coef; int cached; };
static void dx_bsolve(void *c, int b, int e, int w)
{
	struct dx_bsolve_ctx *x = (struct dx_bsolve_ctx *)c;
	for(int j=b; j<e; j++){
		int k = dx_phase_blocks[x->phase][j];
		for(int i=dx_bstart[k]; i<dx_bstart[k + 1]; i++){ if(x->cached) cpArbiterApplyCachedImpulse(dx_bsorted[i], x->dt_coef); else cpArbiterApplyImpulse(dx_bsorted[i]); }
	}
}
int dx_block_alternate = 1;
static void dx_blocked(int cached, cpFloat dt_coef, int iter)
{
	struct dx_bsolve_ctx x; x.dt_coef = dt_coef; x.cached = cached;
	int rev = dx_block_alternate && (iter & 1);
	for(int q=0; q<4; q++){ int p = rev ? 3 - q : q; x.phase = p; dx_parallel_for(dx_pbn[p], 1, 16, dx_bsolve, &x); }
}

struct dx_solve_ctx { cpArbiter **arr; cpFloat dt_coef; };
static void dx_cached(void *c, int b, int e, int w){ struct dx_solve_ctx *x = (struct dx_solve_ctx *)c; for(int i=b; i<e; i++) cpArbiterApplyCachedImpulse(x->arr[i], x->dt_coef); }
static void dx_impulse(void *c, int b, int e, int w){ struct dx_solve_ctx *x = (struct dx_solve_ctx *)c; for(int i=b; i<e; i++) cpArbiterApplyImpulse(x->arr[i]); }
static void dx_colored(dx_range_fn fn, cpFloat dt_coef)
{
	struct dx_solve_ctx x; x.dt_coef = dt_coef;
	for(int c=0; c<64; c++){ int k = dx_cstart[c+1] - dx_cstart[c]; if(!k) continue; x.arr = dx_sorted + dx_cstart[c]; dx_parallel_for(k, 64, 512, fn, &x); }
	int k = dx_cstart[65] - dx_cstart[64]; if(k){ x.arr = dx_sorted + dx_cstart[64]; fn(&x, 0, k, 0); }
}

#ifdef DX_PROFILE
__declspec(dllimport) int __stdcall QueryPerformanceCounter(long long *c);
__declspec(dllimport) int __stdcall QueryPerformanceFrequency(long long *f);
double dx_prof[32]; long long dx_prof_steps, dx_prof_arbiters, dx_prof_bodies;
double dx_now(void){ static double inv = 0; long long c; if(inv == 0){ long long f; QueryPerformanceFrequency(&f); inv = 1.0/(double)f; } QueryPerformanceCounter(&c); return (double)c*inv; }
#define DXP(i) do { double _t = dx_now(); dx_prof[i] += _t - _dxp_last; _dxp_last = _t; } while(0)
#else
#define DXP(i) do {} while(0)
#endif

void
cpSpaceStep(cpSpace *space, cpFloat dt)
{

	if(dt == 0.0f) return;
#ifdef DX_PROFILE
	double _dxp_last = dx_now(); dx_prof_steps++; dx_prof_bodies += space->dynamicBodies->num;
#endif

	space->stamp++;

	cpFloat prev_dt = space->curr_dt;
	space->curr_dt = dt;

	cpArray *bodies = space->dynamicBodies;
	cpArray *constraints = space->constraints;
	cpArray *arbiters = space->arbiters;

	if(space->dx_unthreaded){
		for(int i=0; i<arbiters->num; i++) ((cpArbiter *)arbiters->arr[i])->state = CP_ARBITER_STATE_NORMAL;
	} else
	for(int i=0; i<arbiters->num; i++){
		cpArbiter *arb = (cpArbiter *)arbiters->arr[i];
		arb->state = CP_ARBITER_STATE_NORMAL;

		if(!cpBodyIsSleeping(arb->body_a) && !cpBodyIsSleeping(arb->body_b)){
			cpArbiterUnthread(arb);
		}
	}
	arbiters->num = 0;
	DXP(0);

	cpSpaceLock(space); {

		struct dx_step_ctx dxc; dxc.space = space; dxc.dt = dt;
		if(dx_par_independent) dx_parallel_for(bodies->num, DX_CHUNK, DX_MINPAR, dx_positions, &dxc); else
		for(int i=0; i<bodies->num; i++){
			cpBody *body = (cpBody *)bodies->arr[i];
			body->position_func(body, dt);
		}
		DXP(1);

		cpSpacePushFreshContactBuffer(space);

		if(dx_par_independent) dx_parallel_for(bodies->num, DX_CHUNK, DX_MINPAR, dx_shapes, &dxc); else
		cpSpatialIndexEach(space->dynamicShapes, (cpSpatialIndexIteratorFunc)cpShapeUpdateFunc, NULL);
		DXP(2);
		if(!dx_collide(space))
		cpSpatialIndexReindexQuery(space->dynamicShapes, (cpSpatialIndexQueryFunc)cpSpaceCollideShapes, space);
		DXP(3);
	} cpSpaceUnlock(space, cpFalse);

	cpSpaceProcessComponents(space, dt);
	DXP(4);

	cpSpaceLock(space); {

		if(dx_par_filter && dx_pool_threads() > 0 && cpHashSetCount(space->cachedArbiters) >= 8192) cpHashSetDXFilterParallel(space->cachedArbiters, (cpHashSetDXClassifyFunc)dx_arb_classify, (cpHashSetDXPostFunc)dx_arb_post, space); else
		cpHashSetFilter(space->cachedArbiters, (cpHashSetFilterFunc)cpSpaceArbiterSetFilter, space);
		DXP(5);
#ifdef DX_PROFILE
		dx_prof_arbiters += arbiters->num;
#endif

		cpFloat slop = space->collisionSlop;
		cpFloat biasCoef = 1.0f - cpfpow(space->collisionBias, dt);
		struct dx_step_ctx dxc2; dxc2.space = space; dxc2.dt = dt; dxc2.slop = slop; dxc2.biasCoef = biasCoef;
		if(dx_par_independent) dx_parallel_for(arbiters->num, DX_CHUNK, DX_MINPAR, dx_prestep, &dxc2); else
		for(int i=0; i<arbiters->num; i++){
			cpArbiterPreStep((cpArbiter *)arbiters->arr[i], dt, slop, biasCoef);
		}
		DXP(6);

		for(int i=0; i<constraints->num; i++){
			cpConstraint *constraint = (cpConstraint *)constraints->arr[i];

			cpConstraintPreSolveFunc preSolve = constraint->preSolve;
			if(preSolve) preSolve(constraint, space);

			constraint->klass->preStep(constraint, dt);
		}

		cpFloat damping = cpfpow(space->damping, dt);
		cpVect gravity = space->gravity;
		dxc2.damping = damping; dxc2.gravity = gravity;
		if(dx_par_independent) dx_parallel_for(bodies->num, DX_CHUNK, DX_MINPAR, dx_velocities, &dxc2); else
		for(int i=0; i<bodies->num; i++){
			cpBody *body = (cpBody *)bodies->arr[i];
			body->velocity_func(body, gravity, damping, dt);
		}

		cpFloat dt_coef = (prev_dt == 0.0f ? 0.0f : dt/prev_dt);
		int dx_solve = dx_par_solver && dx_pool_threads() > 0 && arbiters->num >= 2048;
#ifdef DX_PROFILE
		double _k0 = dx_now();
#endif
		int dx_blk = 0;
		if(dx_solve){ dx_blk = dx_block_solver && dx_build_blocks(space, arbiters); if(!dx_blk) dx_color(space, arbiters);
#ifdef DX_PROFILE
			dx_prof[20] += dx_now() - _k0;
#endif
			if(dx_blk) dx_blocked(1, dt_coef, 0); else dx_colored(dx_cached, dt_coef); } else
		for(int i=0; i<arbiters->num; i++){
			cpArbiterApplyCachedImpulse((cpArbiter *)arbiters->arr[i], dt_coef);
		}
		DXP(7);

		for(int i=0; i<constraints->num; i++){
			cpConstraint *constraint = (cpConstraint *)constraints->arr[i];
			constraint->klass->applyCachedImpulse(constraint, dt_coef);
		}

		for(int i=0; i<space->iterations; i++){
			if(dx_solve){ if(dx_blk) dx_blocked(0, 0.0f, i); else dx_colored(dx_impulse, 0.0f); } else
			for(int j=0; j<arbiters->num; j++){
				cpArbiterApplyImpulse((cpArbiter *)arbiters->arr[j]);
			}

			for(int j=0; j<constraints->num; j++){
				cpConstraint *constraint = (cpConstraint *)constraints->arr[j];
				constraint->klass->applyImpulse(constraint, dt);
			}
		}

		DXP(8);

		for(int i=0; i<constraints->num; i++){
			cpConstraint *constraint = (cpConstraint *)constraints->arr[i];

			cpConstraintPostSolveFunc postSolve = constraint->postSolve;
			if(postSolve) postSolve(constraint, space);
		}

		for(int i=0; i<arbiters->num; i++){
			cpArbiter *arb = (cpArbiter *) arbiters->arr[i];

			cpCollisionHandler *handler = arb->handler;
			handler->postSolveFunc(arb, space, handler->userData);
		}
	} cpSpaceUnlock(space, cpTrue);
	DXP(9);
}
