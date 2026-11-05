// Chipmunk2DVulkan: Vulkan compute backend of the step (cpvk.comp). Layouts match the GLSL structs.
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct { float px, py, vx, vy, fx, fy, a, w, t, m_inv, i_inv; uint32_t flags; } CvkBody;            // 48 bytes
typedef struct { uint32_t body, flags; float r, hx, hy; uint32_t cat, mask, gen; } CvkShape;                // 32 bytes
typedef struct { uint32_t slot, pad[3]; CvkShape s; } CvkShapePatch;                                        // 48 bytes
typedef struct { uint32_t slot, keepPos, pad[2]; CvkBody b; } CvkBodyPatch;                                 // 64 bytes
typedef struct { float bb[4]; uint32_t shape, pad[3]; } CvkStaticRec;                                       // 32 bytes
typedef struct { uint32_t a, b, ga, gb, contact, pad[3]; } CvkCbPair;                                      // 32 bytes

enum { CVK_BT_NONE = 0, CVK_BT_DYNAMIC = 1, CVK_BT_KINEMATIC = 2, CVK_BT_STATIC = 3, CVK_BF_FRESH = 4 };
enum { CVK_SK_CIRCLE = 1, CVK_SK_BOX = 2, CVK_SF_SENSOR = 4, CVK_SF_CB_SHIFT = 3 };
#define CVK_NO_CONTACT 0xFFFFFFFFu
#define CVK_CONTACT_BYTES 112   // cpvk.comp struct Contact
#define CVK_NCOLORS 32       // contact colours of the Gauss-Seidel passes (cpvk.comp NCOLORS)

static inline uint32_t cvk_hash3(int x, int y, int l) { return ((uint32_t)x * 73856093u) ^ ((uint32_t)y * 19349663u) ^ ((uint32_t)l * 83492791u); }
#define CVK_STATIC_SALT 85   // cpvk.comp STATIC_SALT

// One step. The caller fills the inputs, cvk_reserve1 grows the buffers and sets the upload pointers (written by the
// caller), cvk_phase1 runs everything up to the contacts (callback pairs come back), cvk_reserve2 / cvk_phase2 run
// the solver and read back the bodies and the query index. Result pointers stay valid until the second phase2 after.
typedef struct CvkFrame {
	// inputs
	uint32_t nBodies, nShapes, nShapePatches;
	uint32_t hs, nStaticItems, nStaticRecs;          // static grid: buckets (power of two, 0 = none), items, records
	uint32_t iterations;
	float dt, damping, biasCoef, slop, dtCoef, gx, gy, cell0, scell;
	// upload pointers (cvk_reserve1)
	CvkBody *bodies; CvkShapePatch *shapePatches; uint32_t *staticStart; uint32_t *staticItems; CvkStaticRec *staticRecs;
	// phase 1 results
	uint32_t nContacts, nCb; const CvkCbPair *cb;
	uint32_t nColors, nUncolored;                     // colours used, contacts left to the Jacobi pass
	// phase 2 inputs and upload pointers (cvk_reserve2)
	uint32_t nDisable, nBodyPatches; uint32_t *disable; CvkBodyPatch *bodyPatches;
	// phase 2 results: bodies (8 floats each: p, v, a, w, -, -), the grid's entries (shape per entry) and the shape boxes
	const float *bodyOut; uint32_t nEntries; const uint32_t *eshape; const float *sbb;
	// timing of the last phases (µs): CPU recording + submit, wait, and GPU time when timestamps are available
	double usRecord1, usWait1, usRecord2, usWait2, usGpu1, usGpu2;
} CvkFrame;

int cvk_init(char *err, int errLen);       // 1 = ready (idempotent); 0 = no usable Vulkan device (err says why)
int cvk_prepare_shapes(uint32_t n);        // before the shape patches of a step: 1 = the shape buffer is new, send every shape; -1 = failure
const char *cvk_device_name(void);
int cvk_reserve1(CvkFrame *f);
int cvk_phase1(CvkFrame *f);
int cvk_reserve2(CvkFrame *f);
int cvk_phase2(CvkFrame *f);
void cvk_forget_history(void);            // next step without warm start / bias (after the world was reset)
const char *cvk_last_error(void);

// DXPhysicsVulkan.c
double now_us(void);
int env_int(const char *name, int def);   // the variable as a whole integer, else def

#ifdef __cplusplus
}
#endif
