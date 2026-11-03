typedef cpFloat (*cpMarchSampleFunc)(cpVect point, void *data);


typedef void (*cpMarchSegmentFunc)(cpVect v0, cpVect v1, void *data);


void cpMarchSoft(
  cpBB bb, unsigned long x_samples, unsigned long y_samples, cpFloat threshold,
  cpMarchSegmentFunc segment, void *segment_data,
  cpMarchSampleFunc sample, void *sample_data
);


void cpMarchHard(
  cpBB bb, unsigned long x_samples, unsigned long y_samples, cpFloat threshold,
  cpMarchSegmentFunc segment, void *segment_data,
  cpMarchSampleFunc sample, void *sample_data
);
