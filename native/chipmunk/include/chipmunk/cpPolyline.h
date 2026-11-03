typedef struct cpPolyline {
  int count, capacity;
  cpVect verts[];
} cpPolyline;


void cpPolylineFree(cpPolyline *line);


cpBool cpPolylineIsClosed(cpPolyline *line);



cpPolyline *cpPolylineSimplifyCurves(cpPolyline *line, cpFloat tol);



cpPolyline *cpPolylineSimplifyVertexes(cpPolyline *line, cpFloat tol);


cpPolyline *cpPolylineToConvexHull(cpPolyline *line, cpFloat tol);



typedef struct cpPolylineSet {
  int count, capacity;
  cpPolyline **lines;
} cpPolylineSet;


cpPolylineSet *cpPolylineSetAlloc(void);


cpPolylineSet *cpPolylineSetInit(cpPolylineSet *set);


cpPolylineSet *cpPolylineSetNew(void);


void cpPolylineSetDestroy(cpPolylineSet *set, cpBool freePolylines);


void cpPolylineSetFree(cpPolylineSet *set, cpBool freePolylines);



void cpPolylineSetCollectSegment(cpVect v0, cpVect v1, cpPolylineSet *lines);




cpPolylineSet *cpPolylineConvexDecomposition(cpPolyline *line, cpFloat tol);

#define cpPolylineConvexDecomposition_BETA cpPolylineConvexDecomposition
