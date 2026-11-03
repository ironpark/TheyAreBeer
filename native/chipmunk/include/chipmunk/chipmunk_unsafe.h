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
#ifndef CHIPMUNK_UNSAFE_H
#define CHIPMUNK_UNSAFE_H

#ifdef __cplusplus
extern "C" {
#endif


void cpCircleShapeSetRadius(cpShape *shape, cpFloat radius);

void cpCircleShapeSetOffset(cpShape *shape, cpVect offset);


void cpSegmentShapeSetEndpoints(cpShape *shape, cpVect a, cpVect b);

void cpSegmentShapeSetRadius(cpShape *shape, cpFloat radius);


void cpPolyShapeSetVerts(cpShape *shape, int count, cpVect *verts, cpTransform transform);
void cpPolyShapeSetVertsRaw(cpShape *shape, int count, cpVect *verts);

void cpPolyShapeSetRadius(cpShape *shape, cpFloat radius);

#ifdef __cplusplus
}
#endif
#endif

