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
cpBool cpConstraintIsDampedSpring(const cpConstraint *constraint);


typedef cpFloat (*cpDampedSpringForceFunc)(cpConstraint *spring, cpFloat dist);


cpDampedSpring* cpDampedSpringAlloc(void);

cpDampedSpring* cpDampedSpringInit(cpDampedSpring *joint, cpBody *a, cpBody *b, cpVect anchorA, cpVect anchorB, cpFloat restLength, cpFloat stiffness, cpFloat damping);

cpConstraint* cpDampedSpringNew(cpBody *a, cpBody *b, cpVect anchorA, cpVect anchorB, cpFloat restLength, cpFloat stiffness, cpFloat damping);


cpVect cpDampedSpringGetAnchorA(const cpConstraint *constraint);

void cpDampedSpringSetAnchorA(cpConstraint *constraint, cpVect anchorA);


cpVect cpDampedSpringGetAnchorB(const cpConstraint *constraint);

void cpDampedSpringSetAnchorB(cpConstraint *constraint, cpVect anchorB);


cpFloat cpDampedSpringGetRestLength(const cpConstraint *constraint);

void cpDampedSpringSetRestLength(cpConstraint *constraint, cpFloat restLength);


cpFloat cpDampedSpringGetStiffness(const cpConstraint *constraint);

void cpDampedSpringSetStiffness(cpConstraint *constraint, cpFloat stiffness);


cpFloat cpDampedSpringGetDamping(const cpConstraint *constraint);

void cpDampedSpringSetDamping(cpConstraint *constraint, cpFloat damping);


cpDampedSpringForceFunc cpDampedSpringGetSpringForceFunc(const cpConstraint *constraint);

void cpDampedSpringSetSpringForceFunc(cpConstraint *constraint, cpDampedSpringForceFunc springForceFunc);


