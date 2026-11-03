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
#ifndef CHIPMUNK_BB_H
#define CHIPMUNK_BB_H

#include "chipmunk_types.h"
#include "cpVect.h"




typedef struct cpBB{
	cpFloat l, b, r ,t;
} cpBB;


static inline cpBB cpBBNew(const cpFloat l, const cpFloat b, const cpFloat r, const cpFloat t)
{
	cpBB bb = {l, b, r, t};
	return bb;
}


static inline cpBB
cpBBNewForExtents(const cpVect c, const cpFloat hw, const cpFloat hh)
{
	return cpBBNew(c.x - hw, c.y - hh, c.x + hw, c.y + hh);
}


static inline cpBB cpBBNewForCircle(const cpVect p, const cpFloat r)
{
	return cpBBNewForExtents(p, r, r);
}


static inline cpBool cpBBIntersects(const cpBB a, const cpBB b)
{
	return (a.l <= b.r && b.l <= a.r && a.b <= b.t && b.b <= a.t);
}


static inline cpBool cpBBContainsBB(const cpBB bb, const cpBB other)
{
	return (bb.l <= other.l && bb.r >= other.r && bb.b <= other.b && bb.t >= other.t);
}


static inline cpBool cpBBContainsVect(const cpBB bb, const cpVect v)
{
	return (bb.l <= v.x && bb.r >= v.x && bb.b <= v.y && bb.t >= v.y);
}


static inline cpBB cpBBMerge(const cpBB a, const cpBB b){
	return cpBBNew(
		cpfmin(a.l, b.l),
		cpfmin(a.b, b.b),
		cpfmax(a.r, b.r),
		cpfmax(a.t, b.t)
	);
}


static inline cpBB cpBBExpand(const cpBB bb, const cpVect v){
	return cpBBNew(
		cpfmin(bb.l, v.x),
		cpfmin(bb.b, v.y),
		cpfmax(bb.r, v.x),
		cpfmax(bb.t, v.y)
	);
}


static inline cpVect
cpBBCenter(cpBB bb)
{
	return cpvlerp(cpv(bb.l, bb.b), cpv(bb.r, bb.t), 0.5f);
}


static inline cpFloat cpBBArea(cpBB bb)
{
	return (bb.r - bb.l)*(bb.t - bb.b);
}


static inline cpFloat cpBBMergedArea(cpBB a, cpBB b)
{
	return (cpfmax(a.r, b.r) - cpfmin(a.l, b.l))*(cpfmax(a.t, b.t) - cpfmin(a.b, b.b));
}


static inline cpFloat cpBBSegmentQuery(cpBB bb, cpVect a, cpVect b)
{
	cpFloat idx = 1.0f/(b.x - a.x);
#ifdef _MSC_VER
#pragma warning(disable: 4056)
#endif
	cpFloat tx1 = (bb.l == a.x ? -INFINITY : (bb.l - a.x)*idx);
	cpFloat tx2 = (bb.r == a.x ?  INFINITY : (bb.r - a.x)*idx);
	cpFloat txmin = cpfmin(tx1, tx2);
	cpFloat txmax = cpfmax(tx1, tx2);

	cpFloat idy = 1.0f/(b.y - a.y);
	cpFloat ty1 = (bb.b == a.y ? -INFINITY : (bb.b - a.y)*idy);
	cpFloat ty2 = (bb.t == a.y ?  INFINITY : (bb.t - a.y)*idy);
#ifdef _MSC_VER
#pragma warning(default: 4056)
#endif
	cpFloat tymin = cpfmin(ty1, ty2);
	cpFloat tymax = cpfmax(ty1, ty2);

	if(tymin <= txmax && txmin <= tymax){
		cpFloat min = cpfmax(txmin, tymin);
		cpFloat max = cpfmin(txmax, tymax);

		if(0.0 <= max && min <= 1.0) return cpfmax(min, 0.0);
	}

	return INFINITY;
}


static inline cpBool cpBBIntersectsSegment(cpBB bb, cpVect a, cpVect b)
{
	return (cpBBSegmentQuery(bb, a, b) != INFINITY);
}


static inline cpVect
cpBBClampVect(const cpBB bb, const cpVect v)
{
	return cpv(cpfclamp(v.x, bb.l, bb.r), cpfclamp(v.y, bb.b, bb.t));
}


static inline cpVect
cpBBWrapVect(const cpBB bb, const cpVect v)
{
	cpFloat dx = cpfabs(bb.r - bb.l);
	cpFloat modx = cpfmod(v.x - bb.l, dx);
	cpFloat x = (modx > 0.0f) ? modx : modx + dx;

	cpFloat dy = cpfabs(bb.t - bb.b);
	cpFloat mody = cpfmod(v.y - bb.b, dy);
	cpFloat y = (mody > 0.0f) ? mody : mody + dy;

	return cpv(x + bb.l, y + bb.b);
}


static inline cpBB
cpBBOffset(const cpBB bb, const cpVect v)
{
	return cpBBNew(
		bb.l + v.x,
		bb.b + v.y,
		bb.r + v.x,
		bb.t + v.y
	);
}



#endif
