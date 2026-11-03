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
cpBool cpConstraintIsSlideJoint(const cpConstraint *constraint);


cpSlideJoint* cpSlideJointAlloc(void);

cpSlideJoint* cpSlideJointInit(cpSlideJoint *joint, cpBody *a, cpBody *b, cpVect anchorA, cpVect anchorB, cpFloat min, cpFloat max);

cpConstraint* cpSlideJointNew(cpBody *a, cpBody *b, cpVect anchorA, cpVect anchorB, cpFloat min, cpFloat max);


cpVect cpSlideJointGetAnchorA(const cpConstraint *constraint);

void cpSlideJointSetAnchorA(cpConstraint *constraint, cpVect anchorA);


cpVect cpSlideJointGetAnchorB(const cpConstraint *constraint);

void cpSlideJointSetAnchorB(cpConstraint *constraint, cpVect anchorB);


cpFloat cpSlideJointGetMin(const cpConstraint *constraint);

void cpSlideJointSetMin(cpConstraint *constraint, cpFloat min);


cpFloat cpSlideJointGetMax(const cpConstraint *constraint);

void cpSlideJointSetMax(cpConstraint *constraint, cpFloat max);


