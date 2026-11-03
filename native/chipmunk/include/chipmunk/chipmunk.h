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
#ifndef CHIPMUNK_H
#define CHIPMUNK_H

#ifdef _MSC_VER
    #define _USE_MATH_DEFINES
#endif

#include <stdlib.h>
#include <math.h>

#ifdef WIN32

	#include <malloc.h>
#else
	#include <alloca.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif


#ifndef CP_ALLOW_PRIVATE_ACCESS
	#define CP_ALLOW_PRIVATE_ACCESS 0
#endif

#if CP_ALLOW_PRIVATE_ACCESS == 1
	#define CP_PRIVATE(__symbol__) __symbol__
#else
	#define CP_PRIVATE(__symbol__) __symbol__##_private
#endif

void cpMessage(const char *condition, const char *file, int line, int isError, int isHardError, const char *message, ...);
#ifdef NDEBUG
	#define	cpAssertWarn(__condition__, ...)
#else
	#define cpAssertWarn(__condition__, ...) if(!(__condition__)) cpMessage(#__condition__, __FILE__, __LINE__, 0, 0, __VA_ARGS__)
#endif

#ifdef NDEBUG
	#define	cpAssertSoft(__condition__, ...)
#else
	#define cpAssertSoft(__condition__, ...) if(!(__condition__)){cpMessage(#__condition__, __FILE__, __LINE__, 1, 0, __VA_ARGS__), abort();}
#endif


#define cpAssertHard(__condition__, ...) if(!(__condition__)){cpMessage(#__condition__, __FILE__, __LINE__, 1, 1, __VA_ARGS__); abort();}

#include "chipmunk_types.h"




#ifndef CP_BUFFER_BYTES
	#define CP_BUFFER_BYTES (32*1024)
#endif

#ifndef cpcalloc

	#define cpcalloc calloc
#endif

#ifndef cprealloc

	#define cprealloc realloc
#endif

#ifndef cpfree

	#define cpfree free
#endif

typedef struct cpArray cpArray;
typedef struct cpHashSet cpHashSet;

typedef struct cpBody cpBody;

typedef struct cpShape cpShape;
typedef struct cpCircleShape cpCircleShape;
typedef struct cpSegmentShape cpSegmentShape;
typedef struct cpPolyShape cpPolyShape;

typedef struct cpConstraint cpConstraint;
typedef struct cpPinJoint cpPinJoint;
typedef struct cpSlideJoint cpSlideJoint;
typedef struct cpPivotJoint cpPivotJoint;
typedef struct cpGrooveJoint cpGrooveJoint;
typedef struct cpDampedSpring cpDampedSpring;
typedef struct cpDampedRotarySpring cpDampedRotarySpring;
typedef struct cpRotaryLimitJoint cpRotaryLimitJoint;
typedef struct cpRatchetJoint cpRatchetJoint;
typedef struct cpGearJoint cpGearJoint;
typedef struct cpSimpleMotorJoint cpSimpleMotorJoint;

typedef struct cpCollisionHandler cpCollisionHandler;
typedef struct cpContactPointSet cpContactPointSet;
typedef struct cpArbiter cpArbiter;

typedef struct cpSpace cpSpace;

#include "cpVect.h"
#include "cpBB.h"
#include "cpTransform.h"
#include "cpSpatialIndex.h"

#include "cpArbiter.h"

#include "cpBody.h"
#include "cpShape.h"
#include "cpPolyShape.h"

#include "cpConstraint.h"

#include "cpSpace.h"


#define CP_VERSION_MAJOR 7
#define CP_VERSION_MINOR 0
#define CP_VERSION_RELEASE 0


extern const char *cpVersionString;


cpFloat cpMomentForCircle(cpFloat m, cpFloat r1, cpFloat r2, cpVect offset);


cpFloat cpAreaForCircle(cpFloat r1, cpFloat r2);


cpFloat cpMomentForSegment(cpFloat m, cpVect a, cpVect b, cpFloat radius);


cpFloat cpAreaForSegment(cpVect a, cpVect b, cpFloat radius);


cpFloat cpMomentForPoly(cpFloat m, int count, const cpVect *verts, cpVect offset, cpFloat radius);


cpFloat cpAreaForPoly(const int count, const cpVect *verts, cpFloat radius);


cpVect cpCentroidForPoly(const int count, const cpVect *verts);


cpFloat cpMomentForBox(cpFloat m, cpFloat width, cpFloat height);


cpFloat cpMomentForBox2(cpFloat m, cpBB box);


int cpConvexHull(int count, const cpVect *verts, cpVect *result, int *first, cpFloat tol);

#ifdef _MSC_VER
#include "malloc.h"
#endif


#define CP_CONVEX_HULL(__count__, __verts__, __count_var__, __verts_var__) \
cpVect *__verts_var__ = (cpVect *)alloca(__count__*sizeof(cpVect)); \
int __count_var__ = cpConvexHull(__count__, __verts__, __verts_var__, NULL, 0.0); \


static inline cpVect
cpClosetPointOnSegment(const cpVect p, const cpVect a, const cpVect b)
{
	cpVect delta = cpvsub(a, b);
	cpFloat t = cpfclamp01(cpvdot(delta, cpvsub(p, b))/cpvlengthsq(delta));
	return cpvadd(b, cpvmult(delta, t));
}

#if defined(__has_extension)
#if __has_extension(blocks)


void cpSpaceEachBody_b(cpSpace *space, void (^block)(cpBody *body));
void cpSpaceEachShape_b(cpSpace *space, void (^block)(cpShape *shape));
void cpSpaceEachConstraint_b(cpSpace *space, void (^block)(cpConstraint *constraint));

void cpBodyEachShape_b(cpBody *body, void (^block)(cpShape *shape));
void cpBodyEachConstraint_b(cpBody *body, void (^block)(cpConstraint *constraint));
void cpBodyEachArbiter_b(cpBody *body, void (^block)(cpArbiter *arbiter));

typedef void (^cpSpacePointQueryBlock)(cpShape *shape, cpVect point, cpFloat distance, cpVect gradient);
void cpSpacePointQuery_b(cpSpace *space, cpVect point, cpFloat maxDistance, cpShapeFilter filter, cpSpacePointQueryBlock block);

typedef void (^cpSpaceSegmentQueryBlock)(cpShape *shape, cpVect point, cpVect normal, cpFloat alpha);
void cpSpaceSegmentQuery_b(cpSpace *space, cpVect start, cpVect end, cpFloat radius, cpShapeFilter filter, cpSpaceSegmentQueryBlock block);

typedef void (^cpSpaceBBQueryBlock)(cpShape *shape);
void cpSpaceBBQuery_b(cpSpace *space, cpBB bb, cpShapeFilter filter, cpSpaceBBQueryBlock block);

typedef void (^cpSpaceShapeQueryBlock)(cpShape *shape, cpContactPointSet *points);
cpBool cpSpaceShapeQuery_b(cpSpace *space, cpShape *shape, cpSpaceShapeQueryBlock block);

#endif
#endif




#ifdef __cplusplus
}

static inline cpVect operator *(const cpVect v, const cpFloat s){return cpvmult(v, s);}
static inline cpVect operator +(const cpVect v1, const cpVect v2){return cpvadd(v1, v2);}
static inline cpVect operator -(const cpVect v1, const cpVect v2){return cpvsub(v1, v2);}
static inline cpBool operator ==(const cpVect v1, const cpVect v2){return cpveql(v1, v2);}
static inline cpVect operator -(const cpVect v){return cpvneg(v);}

#endif
#endif
