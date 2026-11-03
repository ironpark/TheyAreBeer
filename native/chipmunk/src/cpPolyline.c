#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "chipmunk/chipmunk_private.h"
#include "chipmunk/cpPolyline.h"


static inline int Next(int i, int count){return (i+1)%count;}



#define DEFAULT_POLYLINE_CAPACITY 16

static int
cpPolylineSizeForCapacity(int capacity)
{
	return sizeof(cpPolyline) + capacity*sizeof(cpVect);
}

static cpPolyline *
cpPolylineMake(int capacity)
{
	capacity = (capacity > DEFAULT_POLYLINE_CAPACITY ? capacity : DEFAULT_POLYLINE_CAPACITY);

	cpPolyline *line = (cpPolyline *)cpcalloc(1, cpPolylineSizeForCapacity(capacity));
	line->count = 0;
	line->capacity = capacity;

	return line;
}

static cpPolyline *
cpPolylineMake2(int capacity, cpVect a, cpVect b)
{
	cpPolyline *line = cpPolylineMake(capacity);
	line->count = 2;
	line->verts[0] = a;
	line->verts[1] = b;

	return line;
}

static cpPolyline *
cpPolylineShrink(cpPolyline *line)
{
	line->capacity = line->count;
	return cprealloc(line, cpPolylineSizeForCapacity(line->count));
}

void
cpPolylineFree(cpPolyline *line)
{
	cpfree(line);
}


static cpPolyline *
cpPolylineGrow(cpPolyline *line, int count)
{
  line->count += count;

  int capacity = line->capacity;
  while(line->count > capacity) capacity *= 2;

  if(line->capacity < capacity){
    line->capacity = capacity;
		line = cprealloc(line, cpPolylineSizeForCapacity(capacity));
  }

	return line;
}


static cpPolyline *
cpPolylinePush(cpPolyline *line, cpVect v)
{
  int count = line->count;
  line = cpPolylineGrow(line, 1);
  line->verts[count] = v;

	return line;
}


static cpPolyline *
cpPolylineEnqueue(cpPolyline *line, cpVect v)
{

  int count = line->count;
  line = cpPolylineGrow(line, 1);
  memmove(line->verts + 1, line->verts, count*sizeof(cpVect));
  line->verts[0] = v;

	return line;
}


cpBool
cpPolylineIsClosed(cpPolyline *line)
{
	return (line->count > 1 && cpveql(line->verts[0], line->verts[line->count-1]));
}


static cpBool
cpPolylineIsShort(cpVect *points, int count, int start, int end, cpFloat min)
{
  cpFloat length = 0.0f;
	for(int i=start; i!=end; i=Next(i, count)){
		length += cpvdist(points[i], points[Next(i, count)]);
		if(length > min) return cpFalse;
	}

  return cpTrue;
}



static inline cpFloat
Sharpness(cpVect a, cpVect b, cpVect c)
{

  return cpvdot(cpvnormalize(cpvsub(a, b)), cpvnormalize(cpvsub(c, b)));
}


cpPolyline *
cpPolylineSimplifyVertexes(cpPolyline *line, cpFloat tol)
{
	cpPolyline *reduced = cpPolylineMake2(0, line->verts[0], line->verts[1]);

	cpFloat minSharp = -cpfcos(tol);

	for(int i=2; i<line->count; i++){
		cpVect vert = line->verts[i];
		cpFloat sharp = Sharpness(reduced->verts[reduced->count - 2], reduced->verts[reduced->count - 1], vert);

		if(sharp <= minSharp){
			reduced->verts[reduced->count - 1] = vert;
		} else {
			reduced = cpPolylinePush(reduced, vert);
		}
	}

	if(
		cpPolylineIsClosed(line) &&
		Sharpness(reduced->verts[reduced->count - 2], reduced->verts[0], reduced->verts[1]) < minSharp
	){
		reduced->verts[0] = reduced->verts[reduced->count - 2];
		reduced->count--;
	}


	return reduced;
}


static cpPolyline *
DouglasPeucker(
	cpVect *verts, cpPolyline *reduced,
	int length, int start, int end,
	cpFloat min, cpFloat tol
){

  if((end - start + length)%length < 2) return reduced;

	cpVect a = verts[start];
	cpVect b = verts[end];


	if(cpvnear(a, b, min) && cpPolylineIsShort(verts, length, start, end, min)) return reduced;


	cpFloat max = 0.0;
	int maxi = start;

	cpVect n = cpvnormalize(cpvperp(cpvsub(b, a)));
	cpFloat d = cpvdot(n, a);

	for(int i=Next(start, length); i!=end; i=Next(i, length)){
		cpFloat dist = fabs(cpvdot(n, verts[i]) - d);

		if(dist > max){
			max = dist;
			maxi = i;
		}
	}

	if(max > tol){
    reduced = DouglasPeucker(verts, reduced, length, start, maxi, min, tol);
		reduced = cpPolylinePush(reduced, verts[maxi]);
    reduced = DouglasPeucker(verts, reduced, length, maxi, end, min, tol);
	}

	return reduced;
}


cpPolyline *
cpPolylineSimplifyCurves(cpPolyline *line, cpFloat tol)
{
	cpPolyline *reduced = cpPolylineMake(line->count);

	cpFloat min = tol/2.0f;

  if(cpPolylineIsClosed(line)){
		int start, end;
    cpLoopIndexes(line->verts, line->count - 1, &start, &end);

		reduced = cpPolylinePush(reduced, line->verts[start]);
		reduced = DouglasPeucker(line->verts, reduced, line->count - 1, start, end, min, tol);
		reduced = cpPolylinePush(reduced, line->verts[end]);
		reduced = DouglasPeucker(line->verts, reduced, line->count - 1, end, start, min, tol);
		reduced = cpPolylinePush(reduced, line->verts[start]);
  } else {
		reduced = cpPolylinePush(reduced, line->verts[0]);
		reduced = DouglasPeucker(line->verts, reduced, line->count, 0, line->count - 1, min, tol);
		reduced = cpPolylinePush(reduced, line->verts[line->count - 1]);
  }

	return cpPolylineShrink(reduced);
}



cpPolylineSet *
cpPolylineSetAlloc(void)
{
	return (cpPolylineSet *)cpcalloc(1, sizeof(cpPolylineSet));
}

cpPolylineSet *
cpPolylineSetInit(cpPolylineSet *set)
{
	set->count = 0;
	set->capacity = 8;
	set->lines = cpcalloc(set->capacity, sizeof(cpPolyline));

  return set;
}


cpPolylineSet *
cpPolylineSetNew(void)
{
	return cpPolylineSetInit(cpPolylineSetAlloc());
}

void
cpPolylineSetDestroy(cpPolylineSet *set, cpBool freePolylines)
{
	if(freePolylines){
		for(int i=0; i<set->count; i++){
			cpPolylineFree(set->lines[i]);
		}
	}

	cpfree(set->lines);
}


void
cpPolylineSetFree(cpPolylineSet *set, cpBool freePolylines)
{
	if(set){
		cpPolylineSetDestroy(set, freePolylines);
		cpfree(set);
	}
}


static int
cpPolylineSetFindEnds(cpPolylineSet *set, cpVect v){
	int count = set->count;
	cpPolyline **lines = set->lines;

  for(int i=0; i<count; i++){
		cpPolyline *line = lines[i];
    if(cpveql(line->verts[line->count - 1], v)) return i;
  }

  return -1;
}


static int
cpPolylineSetFindStarts(cpPolylineSet *set, cpVect v){
	int count = set->count;
	cpPolyline **lines = set->lines;

  for(int i=0; i<count; i++){
    if(cpveql(lines[i]->verts[0], v)) return i;
  }

  return -1;
}


static void
cpPolylineSetPush(cpPolylineSet *set, cpPolyline *line)
{

  set->count++;
  if(set->count > set->capacity){
    set->capacity *= 2;
    set->lines = cprealloc(set->lines, set->capacity*sizeof(cpPolyline));
  }

	set->lines[set->count - 1] = line;
}


static void
cpPolylineSetAdd(cpPolylineSet *set, cpVect v0, cpVect v1)
{
	cpPolylineSetPush(set, cpPolylineMake2(DEFAULT_POLYLINE_CAPACITY, v0, v1));
}


static void
cpPolylineSetJoin(cpPolylineSet *set, int before, int after)
{
  cpPolyline *lbefore = set->lines[before];
  cpPolyline *lafter = set->lines[after];


  int count = lbefore->count;
  lbefore = cpPolylineGrow(lbefore, lafter->count);
  memmove(lbefore->verts + count, lafter->verts, lafter->count*sizeof(cpVect));
	set->lines[before] = lbefore;


  set->count--;
	cpPolylineFree(set->lines[after]);
  set->lines[after] = set->lines[set->count];
}


void
cpPolylineSetCollectSegment(cpVect v0, cpVect v1, cpPolylineSet *lines)
{
  int before = cpPolylineSetFindEnds(lines, v0);
  int after = cpPolylineSetFindStarts(lines, v1);

  if(before >= 0 && after >= 0){
    if(before == after){

      lines->lines[before] = cpPolylinePush(lines->lines[before], v1);
    } else {

      cpPolylineSetJoin(lines, before, after);
    }
  } else if(before >= 0){

    lines->lines[before] = cpPolylinePush(lines->lines[before], v1);
  } else if(after >= 0){

    lines->lines[after] = cpPolylineEnqueue(lines->lines[after], v0);
  } else {

    cpPolylineSetAdd(lines, v0, v1);
  }
}



cpPolyline *
cpPolylineToConvexHull(cpPolyline *line, cpFloat tol)
{
	cpPolyline *hull = cpPolylineMake(line->count + 1);
	hull->count = cpConvexHull(line->count, line->verts, hull->verts, NULL, tol);
	hull = cpPolylinePush(hull, hull->verts[0]);

	return cpPolylineShrink(hull);
}



struct Notch {
	int i;
	cpFloat d;
	cpVect v;
	cpVect n;
};

static cpFloat
FindSteiner(int count, cpVect *verts, struct Notch notch)
{
	cpFloat min = INFINITY;
	cpFloat feature = -1.0;

	for(int i=1; i<count-1; i++){
		int index = (notch.i + i)%count;

		cpVect seg_a = verts[index];
		cpVect seg_b = verts[Next(index, count)];

		cpFloat thing_a = cpvcross(notch.n, cpvsub(seg_a, notch.v));
		cpFloat thing_b = cpvcross(notch.n, cpvsub(seg_b, notch.v));
		if(thing_a*thing_b <= 0.0){
			cpFloat t = thing_a/(thing_a - thing_b);
			cpFloat dist = cpvdot(notch.n, cpvsub(cpvlerp(seg_a, seg_b, t), notch.v));

			if(dist >= 0.0 && dist <= min){
				min = dist;
				feature = index + t;
			}
		}
	}

	return feature;
}







static struct Notch
DeepestNotch(int count, cpVect *verts, int hullCount, cpVect *hullVerts, int first, cpFloat tol)
{
	struct Notch notch = {};
	int j = Next(first, count);

	for(int i=0; i<hullCount; i++){
		cpVect a = hullVerts[i];
		cpVect b = hullVerts[Next(i, hullCount)];


		cpVect n = cpvnormalize(cpvrperp(cpvsub(a, b)));
		cpFloat d = cpvdot(n, a);

		cpVect v = verts[j];
		while(!cpveql(v, b)){
			cpFloat depth = cpvdot(n, v) - d;

			if(depth > notch.d){
				notch.d = depth;
				notch.i = j;
				notch.v = v;
				notch.n = n;
			}

			j = Next(j, count);
			v = verts[j];
		}

		j = Next(j, count);
	}

	return notch;
}

static inline int IMAX(int a, int b){return (a > b ? a : b);}

static void
ApproximateConcaveDecomposition(cpVect *verts, int count, cpFloat tol, cpPolylineSet *set)
{
	int first;
	cpVect *hullVerts = alloca(count*sizeof(cpVect));
	int hullCount = cpConvexHull(count, verts, hullVerts, &first, 0.0);

	if(hullCount != count){
		struct Notch notch = DeepestNotch(count, verts, hullCount, hullVerts, first, tol);

		if(notch.d > tol){
			cpFloat steiner_it = FindSteiner(count, verts, notch);

			if(steiner_it >= 0.0){
				int steiner_i = (int)steiner_it;
				cpVect steiner = cpvlerp(verts[steiner_i], verts[Next(steiner_i, count)], steiner_it - steiner_i);


				int sub1_count = (steiner_i - notch.i + count)%count + 1;
				int sub2_count = count - (steiner_i - notch.i + count)%count;
				cpVect *scratch = alloca((IMAX(sub1_count, sub2_count) + 1)*sizeof(cpVect));

				for(int i=0; i<sub1_count; i++) scratch[i] = verts[(notch.i + i)%count];
				scratch[sub1_count] = steiner;
				ApproximateConcaveDecomposition(scratch, sub1_count + 1, tol, set);

				for(int i=0; i<sub2_count; i++) scratch[i] = verts[(steiner_i + 1 + i)%count];
				scratch[sub2_count] = steiner;
				ApproximateConcaveDecomposition(scratch, sub2_count + 1, tol, set);

				return;
			}
		}
	}

	cpPolyline *hull = cpPolylineMake(hullCount + 1);

	memcpy(hull->verts, hullVerts, hullCount*sizeof(cpVect));
	hull->verts[hullCount] = hullVerts[0];
	hull->count = hullCount + 1;

	cpPolylineSetPush(set, hull);
}

cpPolylineSet *
cpPolylineConvexDecomposition_BETA(cpPolyline *line, cpFloat tol)
{
	cpAssertSoft(cpPolylineIsClosed(line), "Cannot decompose an open polygon.");
	cpAssertSoft(cpAreaForPoly(line->count, line->verts, 0.0) >= 0.0, "Winding is backwards. (Are you passing a hole?)");

	cpPolylineSet *set = cpPolylineSetNew();
	ApproximateConcaveDecomposition(line->verts, line->count - 1, tol, set);

	return set;
}
