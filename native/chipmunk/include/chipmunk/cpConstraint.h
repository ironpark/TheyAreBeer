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
typedef void (*cpConstraintPreSolveFunc)(cpConstraint *constraint, cpSpace *space);

typedef void (*cpConstraintPostSolveFunc)(cpConstraint *constraint, cpSpace *space);


void cpConstraintDestroy(cpConstraint *constraint);

void cpConstraintFree(cpConstraint *constraint);


cpSpace* cpConstraintGetSpace(const cpConstraint *constraint);


cpBody* cpConstraintGetBodyA(const cpConstraint *constraint);


cpBody* cpConstraintGetBodyB(const cpConstraint *constraint);


cpFloat cpConstraintGetMaxForce(const cpConstraint *constraint);

void cpConstraintSetMaxForce(cpConstraint *constraint, cpFloat maxForce);


cpFloat cpConstraintGetErrorBias(const cpConstraint *constraint);

void cpConstraintSetErrorBias(cpConstraint *constraint, cpFloat errorBias);


cpFloat cpConstraintGetMaxBias(const cpConstraint *constraint);

void cpConstraintSetMaxBias(cpConstraint *constraint, cpFloat maxBias);


cpBool cpConstraintGetCollideBodies(const cpConstraint *constraint);

void cpConstraintSetCollideBodies(cpConstraint *constraint, cpBool collideBodies);


cpConstraintPreSolveFunc cpConstraintGetPreSolveFunc(const cpConstraint *constraint);

void cpConstraintSetPreSolveFunc(cpConstraint *constraint, cpConstraintPreSolveFunc preSolveFunc);


cpConstraintPostSolveFunc cpConstraintGetPostSolveFunc(const cpConstraint *constraint);

void cpConstraintSetPostSolveFunc(cpConstraint *constraint, cpConstraintPostSolveFunc postSolveFunc);


cpDataPointer cpConstraintGetUserData(const cpConstraint *constraint);

void cpConstraintSetUserData(cpConstraint *constraint, cpDataPointer userData);


cpFloat cpConstraintGetImpulse(cpConstraint *constraint);

#include "cpPinJoint.h"
#include "cpSlideJoint.h"
#include "cpPivotJoint.h"
#include "cpGrooveJoint.h"
#include "cpDampedSpring.h"
#include "cpDampedRotarySpring.h"
#include "cpRotaryLimitJoint.h"
#include "cpRatchetJoint.h"
#include "cpGearJoint.h"
#include "cpSimpleMotor.h"


