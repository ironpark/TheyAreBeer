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
typedef cpBool (*cpCollisionBeginFunc)(cpArbiter *arb, cpSpace *space, cpDataPointer userData);

typedef cpBool (*cpCollisionPreSolveFunc)(cpArbiter *arb, cpSpace *space, cpDataPointer userData);

typedef void (*cpCollisionPostSolveFunc)(cpArbiter *arb, cpSpace *space, cpDataPointer userData);

typedef void (*cpCollisionSeparateFunc)(cpArbiter *arb, cpSpace *space, cpDataPointer userData);


struct cpCollisionHandler {

	const cpCollisionType typeA;

	const cpCollisionType typeB;

	cpCollisionBeginFunc beginFunc;

	cpCollisionPreSolveFunc preSolveFunc;

	cpCollisionPostSolveFunc postSolveFunc;

	cpCollisionSeparateFunc separateFunc;

	cpDataPointer userData;
};







cpSpace* cpSpaceAlloc(void);

cpSpace* cpSpaceInit(cpSpace *space);

cpSpace* cpSpaceNew(void);


void cpSpaceDestroy(cpSpace *space);

void cpSpaceFree(cpSpace *space);





int cpSpaceGetIterations(const cpSpace *space);
void cpSpaceSetIterations(cpSpace *space, int iterations);


cpVect cpSpaceGetGravity(const cpSpace *space);
void cpSpaceSetGravity(cpSpace *space, cpVect gravity);


cpFloat cpSpaceGetDamping(const cpSpace *space);
void cpSpaceSetDamping(cpSpace *space, cpFloat damping);


cpFloat cpSpaceGetIdleSpeedThreshold(const cpSpace *space);
void cpSpaceSetIdleSpeedThreshold(cpSpace *space, cpFloat idleSpeedThreshold);


cpFloat cpSpaceGetSleepTimeThreshold(const cpSpace *space);
void cpSpaceSetSleepTimeThreshold(cpSpace *space, cpFloat sleepTimeThreshold);


cpFloat cpSpaceGetCollisionSlop(const cpSpace *space);
void cpSpaceSetCollisionSlop(cpSpace *space, cpFloat collisionSlop);


cpFloat cpSpaceGetCollisionBias(const cpSpace *space);
void cpSpaceSetCollisionBias(cpSpace *space, cpFloat collisionBias);


cpTimestamp cpSpaceGetCollisionPersistence(const cpSpace *space);
void cpSpaceSetCollisionPersistence(cpSpace *space, cpTimestamp collisionPersistence);


cpDataPointer cpSpaceGetUserData(const cpSpace *space);
void cpSpaceSetUserData(cpSpace *space, cpDataPointer userData);


cpBody* cpSpaceGetStaticBody(const cpSpace *space);


cpFloat cpSpaceGetCurrentTimeStep(const cpSpace *space);


cpBool cpSpaceIsLocked(cpSpace *space);





cpCollisionHandler *cpSpaceAddDefaultCollisionHandler(cpSpace *space);

cpCollisionHandler *cpSpaceAddCollisionHandler(cpSpace *space, cpCollisionType a, cpCollisionType b);

cpCollisionHandler *cpSpaceAddWildcardHandler(cpSpace *space, cpCollisionType type);





cpShape* cpSpaceAddShape(cpSpace *space, cpShape *shape);

cpBody* cpSpaceAddBody(cpSpace *space, cpBody *body);

cpConstraint* cpSpaceAddConstraint(cpSpace *space, cpConstraint *constraint);


void cpSpaceRemoveShape(cpSpace *space, cpShape *shape);

void cpSpaceRemoveBody(cpSpace *space, cpBody *body);

void cpSpaceRemoveConstraint(cpSpace *space, cpConstraint *constraint);


cpBool cpSpaceContainsShape(cpSpace *space, cpShape *shape);

cpBool cpSpaceContainsBody(cpSpace *space, cpBody *body);

cpBool cpSpaceContainsConstraint(cpSpace *space, cpConstraint *constraint);




typedef void (*cpPostStepFunc)(cpSpace *space, void *key, void *data);

cpBool cpSpaceAddPostStepCallback(cpSpace *space, cpPostStepFunc func, void *key, void *data);







typedef void (*cpSpacePointQueryFunc)(cpShape *shape, cpVect point, cpFloat distance, cpVect gradient, void *data);

void cpSpacePointQuery(cpSpace *space, cpVect point, cpFloat maxDistance, cpShapeFilter filter, cpSpacePointQueryFunc func, void *data);

cpShape *cpSpacePointQueryNearest(cpSpace *space, cpVect point, cpFloat maxDistance, cpShapeFilter filter, cpPointQueryInfo *out);


typedef void (*cpSpaceSegmentQueryFunc)(cpShape *shape, cpVect point, cpVect normal, cpFloat alpha, void *data);

void cpSpaceSegmentQuery(cpSpace *space, cpVect start, cpVect end, cpFloat radius, cpShapeFilter filter, cpSpaceSegmentQueryFunc func, void *data);

cpShape *cpSpaceSegmentQueryFirst(cpSpace *space, cpVect start, cpVect end, cpFloat radius, cpShapeFilter filter, cpSegmentQueryInfo *out);


typedef void (*cpSpaceBBQueryFunc)(cpShape *shape, void *data);

void cpSpaceBBQuery(cpSpace *space, cpBB bb, cpShapeFilter filter, cpSpaceBBQueryFunc func, void *data);


typedef void (*cpSpaceShapeQueryFunc)(cpShape *shape, cpContactPointSet *points, void *data);

cpBool cpSpaceShapeQuery(cpSpace *space, cpShape *shape, cpSpaceShapeQueryFunc func, void *data);





typedef void (*cpSpaceBodyIteratorFunc)(cpBody *body, void *data);

void cpSpaceEachBody(cpSpace *space, cpSpaceBodyIteratorFunc func, void *data);


typedef void (*cpSpaceShapeIteratorFunc)(cpShape *shape, void *data);

void cpSpaceEachShape(cpSpace *space, cpSpaceShapeIteratorFunc func, void *data);


typedef void (*cpSpaceConstraintIteratorFunc)(cpConstraint *constraint, void *data);

void cpSpaceEachConstraint(cpSpace *space, cpSpaceConstraintIteratorFunc func, void *data);





void cpSpaceReindexStatic(cpSpace *space);

void cpSpaceReindexShape(cpSpace *space, cpShape *shape);

void cpSpaceReindexShapesForBody(cpSpace *space, cpBody *body);


void cpSpaceUseSpatialHash(cpSpace *space, cpFloat dim, int count);





void cpSpaceStep(cpSpace *space, cpFloat dt);




#ifndef CP_SPACE_DISABLE_DEBUG_API


typedef struct cpSpaceDebugColor {
	float r, g, b, a;
} cpSpaceDebugColor;


typedef void (*cpSpaceDebugDrawCircleImpl)(cpVect pos, cpFloat angle, cpFloat radius, cpSpaceDebugColor outlineColor, cpSpaceDebugColor fillColor, cpDataPointer data);

typedef void (*cpSpaceDebugDrawSegmentImpl)(cpVect a, cpVect b, cpSpaceDebugColor color, cpDataPointer data);

typedef void (*cpSpaceDebugDrawFatSegmentImpl)(cpVect a, cpVect b, cpFloat radius, cpSpaceDebugColor outlineColor, cpSpaceDebugColor fillColor, cpDataPointer data);

typedef void (*cpSpaceDebugDrawPolygonImpl)(int count, const cpVect *verts, cpFloat radius, cpSpaceDebugColor outlineColor, cpSpaceDebugColor fillColor, cpDataPointer data);

typedef void (*cpSpaceDebugDrawDotImpl)(cpFloat size, cpVect pos, cpSpaceDebugColor color, cpDataPointer data);

typedef cpSpaceDebugColor (*cpSpaceDebugDrawColorForShapeImpl)(cpShape *shape, cpDataPointer data);

typedef enum cpSpaceDebugDrawFlags {
	CP_SPACE_DEBUG_DRAW_SHAPES = 1<<0,
	CP_SPACE_DEBUG_DRAW_CONSTRAINTS = 1<<1,
	CP_SPACE_DEBUG_DRAW_COLLISION_POINTS = 1<<2,
} cpSpaceDebugDrawFlags;


typedef struct cpSpaceDebugDrawOptions {

	cpSpaceDebugDrawCircleImpl drawCircle;

	cpSpaceDebugDrawSegmentImpl drawSegment;

	cpSpaceDebugDrawFatSegmentImpl drawFatSegment;

	cpSpaceDebugDrawPolygonImpl drawPolygon;

	cpSpaceDebugDrawDotImpl drawDot;


	cpSpaceDebugDrawFlags flags;

	cpSpaceDebugColor shapeOutlineColor;

	cpSpaceDebugDrawColorForShapeImpl colorForShape;

	cpSpaceDebugColor constraintColor;

	cpSpaceDebugColor collisionPointColor;


	cpDataPointer data;
} cpSpaceDebugDrawOptions;


void cpSpaceDebugDraw(cpSpace *space, cpSpaceDebugDrawOptions *options);

#endif


