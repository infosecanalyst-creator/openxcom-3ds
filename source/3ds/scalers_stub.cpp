// 3DS: the hqx scalers carry a 64 MB static lookup table (RGBtoYUV) and
// xBRZ is far too heavy for a 400x240 screen. They are compiled out
// (Makefile.3ds) and these stubs fall back to plain copying so OpenXcom's
// scaler selection code still links.
#ifdef __3DS__
#include <stdint.h>
#include <string.h>
#include "../Engine/Scalers/hqx.h"
#include "../Engine/Scalers/xbrz.h"

static void nearest(const uint32_t *src, uint32_t srcRB, uint32_t *dst, uint32_t dstRB, int w, int h, int f)
{
	for (int y = 0; y < h * f; ++y)
		for (int x = 0; x < w * f; ++x)
			dst[y * (dstRB / 4) + x] = src[(y / f) * (srcRB / 4) + (x / f)];
}

HQX_API void HQX_CALLCONV hqxInit(void) {}
HQX_API void HQX_CALLCONV hq2x_32(const uint32_t *s, uint32_t *d, int w, int h) { nearest(s, w * 4, d, w * 8, w, h, 2); }
HQX_API void HQX_CALLCONV hq3x_32(const uint32_t *s, uint32_t *d, int w, int h) { nearest(s, w * 4, d, w * 12, w, h, 3); }
HQX_API void HQX_CALLCONV hq4x_32(const uint32_t *s, uint32_t *d, int w, int h) { nearest(s, w * 4, d, w * 16, w, h, 4); }
HQX_API void HQX_CALLCONV hq2x_32_rb(const uint32_t *s, uint32_t srb, uint32_t *d, uint32_t drb, int w, int h) { nearest(s, srb, d, drb, w, h, 2); }
HQX_API void HQX_CALLCONV hq3x_32_rb(const uint32_t *s, uint32_t srb, uint32_t *d, uint32_t drb, int w, int h) { nearest(s, srb, d, drb, w, h, 3); }
HQX_API void HQX_CALLCONV hq4x_32_rb(const uint32_t *s, uint32_t srb, uint32_t *d, uint32_t drb, int w, int h) { nearest(s, srb, d, drb, w, h, 4); }

namespace xbrz {
void scale(size_t factor, const uint32_t *src, uint32_t *trg, int srcWidth, int srcHeight, ColorFormat, const ScalerCfg &, int, int)
{
	nearest(src, srcWidth * 4, trg, srcWidth * 4 * factor, srcWidth, srcHeight, (int)factor);
}
}
#endif
