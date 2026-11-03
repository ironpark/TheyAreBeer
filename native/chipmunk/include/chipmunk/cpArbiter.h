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
#define CP_MAX_CONTACTS_PER_ARBITER 2


cpFloat cpArbiterGetRestitution(const cpArbiter *arb);

void cpArbiterSetRestitution(cpArbiter *arb, cpFloat restitution);

cpFloat cpArbiterGetFriction(const cpArbiter *arb);

void cpArbiterSetFriction(cpArbiter *arb, cpFloat friction);


cpVect cpArbiterGetSurfaceVelocity(cpArbiter *arb);


void cpArbiterSetSurfaceVelocity(cpArbiter *arb, cpVect vr);


cpDataPointer cpArbiterGetUserData(const cpArbiter *arb);

void cpArbiterSetUserData(cpArbiter *arb, cpDataPointer userData);


cpVect cpArbiterTotalImpulse(const cpArbiter *arb);

cpFloat cpArbiterTotalKE(const cpArbiter *arb);


cpBool cpArbiterIgnore(cpArbiter *arb);


void cpArbiterGetShapes(const cpArbiter *arb, cpShape **a, cpShape **b);


#define CP_ARBITER_GET_SHAPES(__arb__, __a__, __b__) cpShape *__a__, *__b__; cpArbiterGetShapes(__arb__, &__a__, &__b__);


void cpArbiterGetBodies(const cpArbiter *arb, cpBody **a, cpBody **b);


#define CP_ARBITER_GET_BODIES(__arb__, __a__, __b__) cpBody *__a__, *__b__; cpArbiterGetBodies(__arb__, &__a__, &__b__);


struct cpContactPointSet {

	int count;


	cpVect normal;


	struct {

		cpVect pointA, pointB;

		cpFloat distance;
	} points[CP_MAX_CONTACTS_PER_ARBITER];
};


cpContactPointSet cpArbiterGetContactPointSet(const cpArbiter *arb);


void cpArbiterSetContactPointSet(cpArbiter *arb, cpContactPointSet *set);


cpBool cpArbiterIsFirstContact(const cpArbiter *arb);

cpBool cpArbiterIsRemoval(const cpArbiter *arb);


int cpArbiterGetCount(const cpArbiter *arb);

cpVect cpArbiterGetNormal(const cpArbiter *arb);

cpVect cpArbiterGetPointA(const cpArbiter *arb, int i);

cpVect cpArbiterGetPointB(const cpArbiter *arb, int i);

cpFloat cpArbiterGetDepth(const cpArbiter *arb, int i);


cpBool cpArbiterCallWildcardBeginA(cpArbiter *arb, cpSpace *space);

cpBool cpArbiterCallWildcardBeginB(cpArbiter *arb, cpSpace *space);


cpBool cpArbiterCallWildcardPreSolveA(cpArbiter *arb, cpSpace *space);

cpBool cpArbiterCallWildcardPreSolveB(cpArbiter *arb, cpSpace *space);


void cpArbiterCallWildcardPostSolveA(cpArbiter *arb, cpSpace *space);

void cpArbiterCallWildcardPostSolveB(cpArbiter *arb, cpSpace *space);


void cpArbiterCallWildcardSeparateA(cpArbiter *arb, cpSpace *space);

void cpArbiterCallWildcardSeparateB(cpArbiter *arb, cpSpace *space);


