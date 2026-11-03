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
typedef enum cpBodyType {

	CP_BODY_TYPE_DYNAMIC,

	CP_BODY_TYPE_KINEMATIC,

	CP_BODY_TYPE_STATIC,
} cpBodyType;


typedef void (*cpBodyVelocityFunc)(cpBody *body, cpVect gravity, cpFloat damping, cpFloat dt);

typedef void (*cpBodyPositionFunc)(cpBody *body, cpFloat dt);


cpBody* cpBodyAlloc(void);

cpBody* cpBodyInit(cpBody *body, cpFloat mass, cpFloat moment);

cpBody* cpBodyNew(cpFloat mass, cpFloat moment);


cpBody* cpBodyNewKinematic(void);

cpBody* cpBodyNewStatic(void);


void cpBodyDestroy(cpBody *body);

void cpBodyFree(cpBody *body);


void cpBodyActivate(cpBody *body);

void cpBodyActivateStatic(cpBody *body, cpShape *filter);


void cpBodySleep(cpBody *body);

void cpBodySleepWithGroup(cpBody *body, cpBody *group);


cpBool cpBodyIsSleeping(const cpBody *body);


cpBodyType cpBodyGetType(cpBody *body);

void cpBodySetType(cpBody *body, cpBodyType type);


cpSpace* cpBodyGetSpace(const cpBody *body);


cpFloat cpBodyGetMass(const cpBody *body);

void cpBodySetMass(cpBody *body, cpFloat m);


cpFloat cpBodyGetMoment(const cpBody *body);

void cpBodySetMoment(cpBody *body, cpFloat i);


cpVect cpBodyGetPosition(const cpBody *body);

void cpBodySetPosition(cpBody *body, cpVect pos);


cpVect cpBodyGetCenterOfGravity(const cpBody *body);

void cpBodySetCenterOfGravity(cpBody *body, cpVect cog);


cpVect cpBodyGetVelocity(const cpBody *body);

void cpBodySetVelocity(cpBody *body, cpVect velocity);


cpVect cpBodyGetForce(const cpBody *body);

void cpBodySetForce(cpBody *body, cpVect force);


cpFloat cpBodyGetAngle(const cpBody *body);

void cpBodySetAngle(cpBody *body, cpFloat a);


cpFloat cpBodyGetAngularVelocity(const cpBody *body);

void cpBodySetAngularVelocity(cpBody *body, cpFloat angularVelocity);


cpFloat cpBodyGetTorque(const cpBody *body);

void cpBodySetTorque(cpBody *body, cpFloat torque);


cpVect cpBodyGetRotation(const cpBody *body);


cpDataPointer cpBodyGetUserData(const cpBody *body);

void cpBodySetUserData(cpBody *body, cpDataPointer userData);


void cpBodySetVelocityUpdateFunc(cpBody *body, cpBodyVelocityFunc velocityFunc);

void cpBodySetPositionUpdateFunc(cpBody *body, cpBodyPositionFunc positionFunc);


void cpBodyUpdateVelocity(cpBody *body, cpVect gravity, cpFloat damping, cpFloat dt);

void cpBodyUpdatePosition(cpBody *body, cpFloat dt);


cpVect cpBodyLocalToWorld(const cpBody *body, const cpVect point);

cpVect cpBodyWorldToLocal(const cpBody *body, const cpVect point);


void cpBodyApplyForceAtWorldPoint(cpBody *body, cpVect force, cpVect point);

void cpBodyApplyForceAtLocalPoint(cpBody *body, cpVect force, cpVect point);


void cpBodyApplyImpulseAtWorldPoint(cpBody *body, cpVect impulse, cpVect point);

void cpBodyApplyImpulseAtLocalPoint(cpBody *body, cpVect impulse, cpVect point);


cpVect cpBodyGetVelocityAtWorldPoint(const cpBody *body, cpVect point);

cpVect cpBodyGetVelocityAtLocalPoint(const cpBody *body, cpVect point);


cpFloat cpBodyKineticEnergy(const cpBody *body);


typedef void (*cpBodyShapeIteratorFunc)(cpBody *body, cpShape *shape, void *data);

void cpBodyEachShape(cpBody *body, cpBodyShapeIteratorFunc func, void *data);


typedef void (*cpBodyConstraintIteratorFunc)(cpBody *body, cpConstraint *constraint, void *data);

void cpBodyEachConstraint(cpBody *body, cpBodyConstraintIteratorFunc func, void *data);


typedef void (*cpBodyArbiterIteratorFunc)(cpBody *body, cpArbiter *arbiter, void *data);

void cpBodyEachArbiter(cpBody *body, cpBodyArbiterIteratorFunc func, void *data);


