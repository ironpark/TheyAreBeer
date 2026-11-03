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
typedef struct cpPointQueryInfo {

	const cpShape *shape;

	cpVect point;

	cpFloat distance;

	cpVect gradient;
} cpPointQueryInfo;


typedef struct cpSegmentQueryInfo {

	const cpShape *shape;

	cpVect point;

	cpVect normal;

	cpFloat alpha;
} cpSegmentQueryInfo;


typedef struct cpShapeFilter {

	cpGroup group;

	cpBitmask categories;

	cpBitmask mask;
} cpShapeFilter;


static const cpShapeFilter CP_SHAPE_FILTER_ALL = {CP_NO_GROUP, CP_ALL_CATEGORIES, CP_ALL_CATEGORIES};

static const cpShapeFilter CP_SHAPE_FILTER_NONE = {CP_NO_GROUP, ~CP_ALL_CATEGORIES, ~CP_ALL_CATEGORIES};


static inline cpShapeFilter
cpShapeFilterNew(cpGroup group, cpBitmask categories, cpBitmask mask)
{
	cpShapeFilter filter = {group, categories, mask};
	return filter;
}


void cpShapeDestroy(cpShape *shape);

void cpShapeFree(cpShape *shape);


cpBB cpShapeCacheBB(cpShape *shape);

cpBB cpShapeUpdate(cpShape *shape, cpTransform transform);


cpFloat cpShapePointQuery(const cpShape *shape, cpVect p, cpPointQueryInfo *out);


cpBool cpShapeSegmentQuery(const cpShape *shape, cpVect a, cpVect b, cpFloat radius, cpSegmentQueryInfo *info);


cpContactPointSet cpShapesCollide(const cpShape *a, const cpShape *b);


cpSpace* cpShapeGetSpace(const cpShape *shape);


cpBody* cpShapeGetBody(const cpShape *shape);

void cpShapeSetBody(cpShape *shape, cpBody *body);


cpFloat cpShapeGetMass(cpShape *shape);

void cpShapeSetMass(cpShape *shape, cpFloat mass);


cpFloat cpShapeGetDensity(cpShape *shape);

void cpShapeSetDensity(cpShape *shape, cpFloat density);


cpFloat cpShapeGetMoment(cpShape *shape);

cpFloat cpShapeGetArea(cpShape *shape);

cpVect cpShapeGetCenterOfGravity(cpShape *shape);


cpBB cpShapeGetBB(const cpShape *shape);


cpBool cpShapeGetSensor(const cpShape *shape);

void cpShapeSetSensor(cpShape *shape, cpBool sensor);


cpFloat cpShapeGetElasticity(const cpShape *shape);

void cpShapeSetElasticity(cpShape *shape, cpFloat elasticity);


cpFloat cpShapeGetFriction(const cpShape *shape);

void cpShapeSetFriction(cpShape *shape, cpFloat friction);


cpVect cpShapeGetSurfaceVelocity(const cpShape *shape);

void cpShapeSetSurfaceVelocity(cpShape *shape, cpVect surfaceVelocity);


cpDataPointer cpShapeGetUserData(const cpShape *shape);

void cpShapeSetUserData(cpShape *shape, cpDataPointer userData);


cpCollisionType cpShapeGetCollisionType(const cpShape *shape);

void cpShapeSetCollisionType(cpShape *shape, cpCollisionType collisionType);


cpShapeFilter cpShapeGetFilter(const cpShape *shape);

void cpShapeSetFilter(cpShape *shape, cpShapeFilter filter);





cpCircleShape* cpCircleShapeAlloc(void);

cpCircleShape* cpCircleShapeInit(cpCircleShape *circle, cpBody *body, cpFloat radius, cpVect offset);

cpShape* cpCircleShapeNew(cpBody *body, cpFloat radius, cpVect offset);


cpVect cpCircleShapeGetOffset(const cpShape *shape);

cpFloat cpCircleShapeGetRadius(const cpShape *shape);




cpSegmentShape* cpSegmentShapeAlloc(void);

cpSegmentShape* cpSegmentShapeInit(cpSegmentShape *seg, cpBody *body, cpVect a, cpVect b, cpFloat radius);

cpShape* cpSegmentShapeNew(cpBody *body, cpVect a, cpVect b, cpFloat radius);


void cpSegmentShapeSetNeighbors(cpShape *shape, cpVect prev, cpVect next);


cpVect cpSegmentShapeGetA(const cpShape *shape);

cpVect cpSegmentShapeGetB(const cpShape *shape);

cpVect cpSegmentShapeGetNormal(const cpShape *shape);

cpFloat cpSegmentShapeGetRadius(const cpShape *shape);


