// Intel D3D12 input adapter. Colour math below is derived from Mochizuki
// DLSSNR-AMD cc27501dc116bdcfbb43612e497c5ca12497574a, Windows RenoDX
// v6_common / v6_encode / v6_autoscale / v6_exposure_scale. Host glue,
// descriptor layout, replay tokens, SDR residual and fused sampling are local.
// The local vendor patch-26 Hold and compute-format patches are not enabled here.
// MIT License -- Copyright (c) 2026 mochizuki0323
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
Texture2D<float4> Original : register(t0);
Texture2D<float4> Motion : register(t1);
Texture2D<float4> Neural : register(t2);
Texture2D<float4> Exposure : register(t3);
Texture2D<float4> Proxy : register(t4);
Texture2D<float> Depth : register(t5);
RWTexture2D<float4> ProxyOut : register(u0);
RWTexture2D<float2> MotionOut : register(u1);
RWTexture2D<float3> Destination : register(u2);
RWTexture2D<float4> Destination4 : register(u3);
RWTexture2D<float4> Norm : register(u4); // 2x1: frame divisor + frozen previous stream state
RWTexture2D<float4> Commit : register(u5); // 2x1: governor + replay serial
RWTexture2D<float> DepthOut : register(u6);
cbuffer Params : register(b0) {
    uint2 Size; uint2 SourceBase;
    uint2 SourceSize; uint2 MotionSize;
    uint2 MotionBase; float2 MotionScale;
    uint Encoding; uint Curve; uint Hdr; uint Feed;
    float Divisor; float PreExposure; float ExposureScale; float Strength;
    float ShadowFloor; float ShadowRange; float HdrKnee; float ChromaClampStops;
    uint Token; uint UseMotion; float SlewStops; float ReleaseStops;
    uint GovernorMode; float SnapNow; float StyleSat; uint StyleMode;
    float GradeStrength; float StyleExposure; float StyleContrast; float Extrapolation;
    float4 Curve0; float4 Curve1; float4 Curve2; float4 Curve3;
    uint2 DepthSize;uint2 DepthBase;
    uint UseDepth;uint3 Padding;
};
#define Width Size.x
#define Height Size.y
#define ProxySize Size
#define NeuralSize Size
#define TransferStrength 1.0
#define ColorStrength 1.0
#define TransferMode 0u
#define HighlightGuardStops 0.0
float curve_sample(float x) {
    float t = saturate(x) * 15.0f;
    int i = (int)floor(t);
    float w = t - (float)i;
    float lut[16] = { Curve0.x, Curve0.y, Curve0.z, Curve0.w,
                      Curve1.x, Curve1.y, Curve1.z, Curve1.w,
                      Curve2.x, Curve2.y, Curve2.z, Curve2.w,
                      Curve3.x, Curve3.y, Curve3.z, Curve3.w };
    return lerp(lut[i], lut[min(i + 1, 15)], w);
}
float style_curve(float x) {
    if (StyleMode == 1u) {
        float v = saturate(x * StyleExposure);
        return saturate(v + (v*v*(3.0f-2.0f*v)-v)*StyleContrast);
    }
    return saturate(x);
}
float hue_to_rgb(float p, float q, float t) {
    t = frac(t);
    if (t < 1.0f / 6.0f) return p + (q - p) * 6.0f * t;
    if (t < 0.5f) return q;
    if (t < 2.0f / 3.0f) return p + (q - p) * (2.0f / 3.0f - t) * 6.0f;
    return p;
}
float3 style_grade(float3 nr) {
    if (StyleMode == 0u) return nr;
    float3 c = float3(style_curve(nr.r), style_curve(nr.g), style_curve(nr.b));
    float maximum = max(c.r, max(c.g, c.b));
    float minimum = min(c.r, min(c.g, c.b));
    float lightness = (maximum + minimum) * 0.5f;
    float difference = maximum - minimum;
    float hue = 0.0f, saturation = 0.0f;
    if (difference > 0.0f) {
        saturation = (lightness > 0.5f)
            ? difference / max(2.0f - maximum - minimum, 1e-6f)
            : difference / max(maximum + minimum, 1e-6f);
        if (maximum == c.r)
            hue = (c.g - c.b) / difference + ((c.g < c.b) ? 6.0f : 0.0f);
        else if (maximum == c.g)
            hue = (c.b - c.r) / difference + 2.0f;
        else
            hue = (c.r - c.g) / difference + 4.0f;
        hue *= 1.0f / 6.0f;
    }
    saturation = saturate(saturation * StyleSat);
    float q = (lightness < 0.5f) ? lightness * (1.0f + saturation)
                                 : lightness + saturation - lightness * saturation;
    float p = 2.0f * lightness - q;
    return saturate(float3(hue_to_rgb(p, q, hue + 1.0f / 3.0f),
                           hue_to_rgb(p, q, hue),
                           hue_to_rgb(p, q, hue - 1.0f / 3.0f)));
}

// Display proxy curve (the Curve == 1 branch of v6_encode) as a function of
// the max channel m in proxy units: f = shoulder(toe(m)).  The encode scales
// all three channels by g(m) = f(m) / m, so P = x * g(m) and max(P) = f(m):
// the curve is recoverable from the proxy alone, at proxy resolution.
static const float kDisplayToeA = 0.014;
static const float kDisplayToeB = 0.001;
static const float kDisplayKnee = 0.75;
static const float kDisplayShoulderRate = 5.770780;

float DisplayCurve(float m) {
  const float t = m + kDisplayToeA * m / (m + kDisplayToeB);
  return t > kDisplayKnee
      ? kDisplayKnee
          + (1.0 - kDisplayKnee)
              * (1.0 - exp(-kDisplayShoulderRate * (t - kDisplayKnee)))
      : t;
}

// s(m) = d(srgb(f)) / d(ln m): how much ENCODED proxy signal one neper of
// scene brightness produces.  The model reads and writes the sRGB-encoded
// plane, so its error is uniform there, not in linear light; 1/s is then the
// nepers of scene change per unit of model error - the conditioning of the
// inverse - and it collapses on the shoulder.
float DisplayCurveLogSlope(float m) {
  const float t = m + kDisplayToeA * m / (m + kDisplayToeB);
  const float toe_slope =
      1.0 + kDisplayToeA * kDisplayToeB / ((m + kDisplayToeB) * (m + kDisplayToeB));
  const float shoulder_slope = t > kDisplayKnee
      ? (1.0 - kDisplayKnee) * kDisplayShoulderRate
          * exp(-kDisplayShoulderRate * (t - kDisplayKnee))
      : 1.0;
  const float f = DisplayCurve(m);
  const float encode_slope = f > 0.0031308
      ? (1.055 / 2.4) * pow(f, 1.0 / 2.4 - 1.0)
      : 12.92;
  return m * toe_slope * shoulder_slope * encode_slope;
}

// Exact inverse: the shoulder by its log, the toe by the positive root of
// m^2 + (A + B - t) m - t B = 0, taken in the cancellation-free form on each
// sign of b (the dark range has b < 0 and would lose every digit otherwise).
// u == 1 maps to a finite value; the damping weight is ~0 there anyway.
float DisplayCurveInverse(float u) {
  const float t = u > kDisplayKnee
      ? kDisplayKnee
          - log(max(1.0 - (u - kDisplayKnee) / (1.0 - kDisplayKnee), 1e-7))
              / kDisplayShoulderRate
      : u;
  const float b = t - kDisplayToeA - kDisplayToeB;
  const float root = sqrt(b * b + 4.0 * t * kDisplayToeB);
  return b < 0.0 ? 2.0 * t * kDisplayToeB / (root - b) : 0.5 * (b + root);
}

// Pure colour math shared by the codec families (legacy_common, v6_common)
// and the NR look stage (look_common).  No resources and no constants here:
// each family declares its own bindings and cbuffer over the same registers,
// so only functions can be shared.

float Luminance(float3 color) {
  return dot(color, float3(0.212639, 0.715169, 0.072192));
}

float3 SrgbEncode(float3 color) {
  color = saturate(color);
  return color <= 0.0031308
      ? color * 12.92
      : 1.055 * pow(color, 1.0 / 2.4) - 0.055;
}

float3 SrgbDecode(float3 color) {
  color = saturate(color);
  return color <= 0.04045
      ? color / 12.92
      : pow((color + 0.055) / 1.055, 2.4);
}

// The restore's maximum gate is spatial data, so a hard comparison after
// interpolation still makes a full-pedestal step when it crosses the gate.
// Smooth mode tapers only inside the admitted dark range: full restoration
// below half the gate, none at or above it. The previous hard gate remains
// selectable for both codec families.
float BlackRestoreWeight(float block_maximum, float dark_gate, bool smooth_gate) {
  return smooth_gate && dark_gate > 0.0
      ? 1.0 - smoothstep(0.5 * dark_gate, dark_gate, block_maximum)
      : (block_maximum <= dark_gate ? 1.0 : 0.0);
}

// Per-pixel denominator-floor trust for the N/P ratio gain (luma and chroma):
// where the proxy sits at the floor the ratio is noise and cannot support any
// bounded correction. It is a mathematical bound, not a frame-health or
// engagement heuristic.  Users: v6_resolve (the Display transfer), the
// look stage (N' = n * 2^(t * (e' - e)), so floor pixels keep the network's
// value) and UpgradeToneMap's neural-floor chroma guard (v6_common, Curve 2).
float ProxyFloorTrust(float3 proxy_linear) {
  const float m = max(proxy_linear.x, max(proxy_linear.y, proxy_linear.z));
  return smoothstep(1.0 / 1024.0, 1.0 / 64.0, m);
}


// ST.2084 (PQ) transfer, reference white normalized to 1.0 = 10000 nits.
float3 LinearToPQ(float3 lin) {
  lin = saturate(lin);
  const float m1 = 0.1593017578125;
  const float m2 = 78.84375;
  const float c1 = 0.8359375;
  const float c2 = 18.8515625;
  const float c3 = 18.6875;
  float3 x = pow(lin, m1);
  float3 y = (c1 + c2 * x) / (1.0 + c3 * x);
  return pow(y, m2);
}

float3 PQToLinear(float3 pq) {
  pq = saturate(pq);
  const float m1 = 0.1593017578125;
  const float m2 = 78.84375;
  const float c1 = 0.8359375;
  const float c2 = 18.8515625;
  const float c3 = 18.6875;
  float3 x = pow(pq, 1.0 / m2);
  float3 y = max(x - c1, 0.0) / (c2 - c3 * x);
  return pow(y, 1.0 / m1);
}

// Display-referred SDR in a float buffer (NRSourceEncoding=1, Encoding 3):
// the sRGB transfer extended past [0, 1] by mirroring it through zero and
// continuing the power segment (the scRGB convention), so a float buffer's
// negative or over-white values survive the linearize -> commit round trip
// that SrgbEncode / SrgbDecode, which clip for the proxy, would cut off.
float3 SrgbDecodeExtended(float3 encoded) {
  const float3 a = abs(encoded);
  return sign(encoded) * (a <= 0.04045 ? a / 12.92 : pow((a + 0.055) / 1.055, 2.4));
}

float3 SrgbEncodeExtended(float3 lin) {
  const float3 a = abs(lin);
  return sign(lin) * (a <= 0.0031308 ? a * 12.92 : 1.055 * pow(a, 1.0 / 2.4) - 0.055);
}

// Encoding 4 (rc11, an HDR10 swapchain at the Present hook point): PQ with
// BT.2020 primaries.  The whole v6 chain between linearize and commit is
// BT.709 linear (the proxy is BT.709 display-referred), so the linearize
// converts after the PQ decode and the commit converts back before the PQ
// encode.  Colours outside BT.709 go negative in between, which ClampAp1
// already carries (AP1 contains BT.2020); linear D65, ITU-R BT.2087.
float3 Bt2020ToBt709(float3 color) {
  const float3x3 m = {
    1.6604910, -0.5876411, -0.0728499,
    -0.1245505, 1.1328999, -0.0083494,
    -0.0181508, -0.1005789, 1.1187297,
  };
  return mul(m, color);
}

float3 Bt709ToBt2020(float3 color) {
  const float3x3 m = {
    0.6274040, 0.3292820, 0.0433136,
    0.0690970, 0.9195400, 0.0113612,
    0.0163916, 0.0880132, 0.8955950,
  };
  return mul(m, color);
}

float3 CbrtSigned(float3 value) {
  return sign(value) * pow(abs(value), 1.0 / 3.0);
}

float3 ToOkLab(float3 color) {
  const float3x3 rgb_to_lms = {
    0.4122214708, 0.5363325363, 0.0514459929,
    0.2119034982, 0.6806995451, 0.1073969566,
    0.0883024619, 0.2817188376, 0.6299787005
  };
  const float3x3 lms_to_lab = {
    0.2104542553, 0.7936177850, -0.0040720468,
    1.9779984951, -2.4285922050, 0.4505937099,
    0.0259040371, 0.7827717662, -0.8086757660
  };
  return mul(lms_to_lab, CbrtSigned(mul(rgb_to_lms, color)));
}

float3 FromOkLab(float3 lab) {
  const float3x3 lab_to_lms = {
    1.0, 0.3963377774, 0.2158037573,
    1.0, -0.1055613458, -0.0638541728,
    1.0, -0.0894841775, -1.2914855480
  };
  const float3x3 lms_to_rgb = {
    4.0767416621, -3.3077115913, 0.2309699292,
    -1.2684380046, 2.6097574011, -0.3413193965,
    -0.0041960863, -0.7034186147, 1.7076147010
  };
  float3 lms = mul(lab_to_lms, lab);
  return mul(lms_to_rgb, lms * lms * lms);
}

float3 ClampAp1(float3 color) {
  const float3x3 bt709_to_ap1 = {
    0.613097, 0.339523, 0.047379,
    0.070194, 0.916354, 0.013452,
    0.020616, 0.109570, 0.869815
  };
  const float3x3 ap1_to_bt709 = {
    1.705051, -0.621792, -0.083259,
    -0.130256, 1.140805, -0.010548,
    -0.024003, -0.128969, 1.152972
  };
  return mul(ap1_to_bt709, max(0.0, mul(bt709_to_ap1, color)));
}

float3 HueOkLab(float3 incorrect, float3 correct) {
  float3 incorrect_lab = ToOkLab(incorrect);
  const float3 correct_lab = ToOkLab(correct);
  const float incorrect_chroma = length(incorrect_lab.yz);
  const float correct_chroma = length(correct_lab.yz);
  incorrect_lab.yz = correct_lab.yz
      * (correct_chroma == 0.0 ? 1.0 : incorrect_chroma / correct_chroma);
  return ClampAp1(FromOkLab(incorrect_lab));
}

float3 UpgradeToneMap(float3 original, float3 proxy, float3 neural) {
  const float original_y = Luminance(original);
  const float proxy_y = Luminance(proxy);
  const float neural_y = Luminance(neural);
  // The DLSSNR proxy output can be degenerate/empty for some HDR inputs (e.g. a
  // PQ buffer linearized through sRGB).  With TransferStrength=1 the ratio path
  // below sets ratio=0 when neural_y==0, which makes `scaled` (and therefore
  // `upgraded`) collapse to black; with ColorStrength=0 the final result is then
  // original*0 -> a fully black frame.  Preserve the original scene instead.
  // Single-exit: FXC X4000-proof (every path returns an initialized value).
  float3 result = original;
  [branch]
  if (neural_y > 1e-5) {
    float ratio = original_y / max(proxy_y, 1e-6);
    if (original_y >= proxy_y) {
      const float new_y = neural_y + max(0.0, original_y - proxy_y);
      ratio = new_y / neural_y;
    }
    float3 scaled = HueOkLab(neural * ratio, neural);
    // Neural-floor chroma guard (Curve 2; ported from v6.6.0's C6).  `ratio`
    // carries the neural colour to the target luminance; for a degenerate
    // neural result (near-black, one channel at a few 8-bit codes) over a
    // bright original it reaches 1e3-1e5 and turned an orange highlight
    // green (harness C6: 40 stops of chroma error).  The colour falls back
    // to the original's only where the neural value sits at the floor AND
    // the pixel is amplified past a stop; the transferred luminance
    // (neural_y * ratio) is kept either way, so this is no tone curve.
    // Trust 1 - above the floor, ratio <= 2, or Curve 0 - is the previous
    // math exactly.
    const float trust = Curve == 2u
        ? max(ProxyFloorTrust(neural), 1.0 - smoothstep(1.0, 3.0, log2(max(ratio, 1.0))))
        : 1.0;
    if (trust < 1.0) {
      scaled = lerp(original * (neural_y * ratio / max(original_y, 1e-6)), scaled, trust);
    }
    result = lerp(original, scaled, TransferStrength);
  }
  return result;
}

// Proxy-domain decode back to linear display units.  This is a FIXED decode
// (plain sRGB) applied identically to the conditioned proxy (P) and the raw
// neural output (N) - it is NOT the inverse of the encode curve.  It never
// needs to be: the Display path only consumes P and N through their ratio,
// and N ~ P keeps that ratio near 1 even where the decode mismatches the
// encoder.
float3 DecodeProxyLinear(float3 encoded) {
  return SrgbDecode(encoded);
}

float3 SampleProxyLinearBilinear(uint2 tex_size, uint2 pixel) {
  float3 result;
  if (all(tex_size == Size)) {
    result = DecodeProxyLinear(Proxy.Load(int3(pixel, 0)).rgb);
  } else {
  const float2 pos = ((float2(pixel) + 0.5) * float2(tex_size)) / float2(Size) - 0.5;
  const int2 max_coord = int2(tex_size) - int2(1, 1);
  const int2 b0 = clamp(int2(floor(pos)), int2(0, 0), max_coord);
  const int2 b1 = min(b0 + int2(1, 1), max_coord);
  const float2 f = saturate(pos - float2(b0));
  const float3 s00 = DecodeProxyLinear(Proxy.Load(int3(b0, 0)).rgb);
  const float3 s10 = DecodeProxyLinear(Proxy.Load(int3(int2(b1.x, b0.y), 0)).rgb);
  const float3 s01 = DecodeProxyLinear(Proxy.Load(int3(int2(b0.x, b1.y), 0)).rgb);
  const float3 s11 = DecodeProxyLinear(Proxy.Load(int3(b1, 0)).rgb);
    result = s00 * (1.0 - f.x) * (1.0 - f.y) + s10 * f.x * (1.0 - f.y)
      + s01 * (1.0 - f.x) * f.y + s11 * f.x * f.y;
  }
  return result;
}

float3 FinishNeural(float3 proxy, float3 neural) {
    if (StyleMode != 0u)
        neural = lerp(proxy, style_grade(neural), GradeStrength);
    if (Extrapolation != 1.0f)
        neural = saturate(proxy + Extrapolation * (neural - proxy));
    return neural;
}

float3 SampleNeuralLinearBilinear(uint2 tex_size, uint2 pixel) {
  float3 result;
  if (all(tex_size == Size)) {
    result = DecodeProxyLinear(FinishNeural(Proxy.Load(int3(pixel, 0)).rgb, Neural.Load(int3(pixel, 0)).rgb));
  } else {
  const float2 pos = ((float2(pixel) + 0.5) * float2(tex_size)) / float2(Size) - 0.5;
  const int2 max_coord = int2(tex_size) - int2(1, 1);
  const int2 b0 = clamp(int2(floor(pos)), int2(0, 0), max_coord);
  const int2 b1 = min(b0 + int2(1, 1), max_coord);
  const float2 f = saturate(pos - float2(b0));
  const float3 s00 = DecodeProxyLinear(Neural.Load(int3(b0, 0)).rgb);
  const float3 s10 = DecodeProxyLinear(Neural.Load(int3(int2(b1.x, b0.y), 0)).rgb);
  const float3 s01 = DecodeProxyLinear(Neural.Load(int3(int2(b0.x, b1.y), 0)).rgb);
  const float3 s11 = DecodeProxyLinear(Neural.Load(int3(b1, 0)).rgb);
    result = s00 * (1.0 - f.x) * (1.0 - f.y) + s10 * f.x * (1.0 - f.y)
      + s01 * (1.0 - f.x) * f.y + s11 * f.x * f.y;
  }
  return result;
}

// One pixel of the v6 resolve, in linear source units.  Users: v6_resolve,
// and v6_pedestal_reduce built with MEASURE_UNSHAPED (the pedestal measuring
// NR's own lift while the look shapes).
float3 ResolvePixel(uint2 pixel, float3 original) {
  // A game's non-finite pixel (Inf or NaN in its colour) passes through as
  // the game made it: any gain turns Inf into NaN (Inf - Inf in the lerp),
  // at every strength (rc11, DATA-09).
  if (any((asuint(original) & 0x7F800000u) == 0x7F800000u)) return original;
  const float3 proxy = SampleProxyLinearBilinear(max(ProxySize, uint2(1, 1)), pixel);
  const float3 neural = SampleNeuralLinearBilinear(max(NeuralSize, uint2(1, 1)), pixel);
  float3 result;
  [branch]
  if (Curve == 1u) {
    // Display codec: apply the neural residual directly in bounded log space.
    // N == P is exact identity.  Luma is bounded to +/-2 stops and chroma to
    // +/-ChromaClampStops so invalid ratios cannot explode the untouched HDR
    // source.  There is deliberately no host-side health/authority multiplier.
    const float3 safe_ratio =
        max(neural, 1.0 / 1024.0) / max(proxy, 1.0 / 1024.0);
    const float3 lg = log2(safe_ratio);
    const float luma = dot(lg, float3(0.212639, 0.715169, 0.072192));
    const float3 chroma = lg - luma;
    // Consistent transfer (TransferMode 1).  The ratio N/P undoes the encode
    // gain g(m_P) the proxy was built with, but a brightness edit moves the
    // pixel along the curve, where the gain is g(m_N): the exact inverse of
    // the encode is x * (N/P) * g(m_P)/g(m_N).  Without the last factor a
    // +1 stop edit is ~half lost at 0.5-1x white and entirely lost above it
    // (harness: exposure+1.00, curve 1).  m_N is a damped inverse: MAP in
    // ln m with weight w = s^2 / (s^2 + lambda), s = d(encoded f)/d(ln m) at
    // the worse end, so w -> 1 where the curve is invertible and w -> 0 on
    // the flat shoulder, where this reduces to the bounded ratio above.
    // lambda is a noise budget: output change per unit of encoded proxy error
    // peaks at 1 / (2 sqrt(lambda)) nepers, so 1.28e-4 bounds the
    // correction's share at 0.25 stops per 8-bit code of model error.
    // Both gains come from the same forward function, so N == P stays an
    // exact identity.
    float correction = 0.0;
    if (TransferMode == 1u) {
      const float m_proxy =
          DisplayCurveInverse(max(proxy.x, max(proxy.y, proxy.z)));
      const float m_target =
          DisplayCurveInverse(max(neural.x, max(neural.y, neural.z)));
      if (m_proxy > 1e-7 && m_target > 1e-7) {
        const float s = min(
            DisplayCurveLogSlope(m_proxy), DisplayCurveLogSlope(m_target));
        const float m_neural =
            m_proxy * pow(m_target / m_proxy, s * s / (s * s + 1.28e-4));
        correction = log2(
            (DisplayCurve(m_proxy) / m_proxy)
            / (DisplayCurve(m_neural) / m_neural));
      }
    }
    const float l = clamp(luma + correction, -2.0, 2.0);
    // Style highlight guard: bound only the NEGATIVE luma edit, and only
    // where the proxy is on the Display shoulder.  The bound eases in from
    // the knee so no visible seam rides the 0.75 edge; at the default 0 the
    // branch never runs, so the math is the previous one bit for bit.
    float l_guarded = l;
    if (HighlightGuardStops > 0.0 && l < 0.0) {
      const float m = max(proxy.x, max(proxy.y, proxy.z));
      const float w = smoothstep(kDisplayKnee, kDisplayKnee * 2.0, m);
      l_guarded = max(l, lerp(-2.0, -HighlightGuardStops, w));
    }
    const float3 c = clamp(chroma, -ChromaClampStops, ChromaClampStops);
    // The whole bounded gain rides the denominator-floor trust: the luma term
    // used to bypass it, so in the floor-dominated band of a very dark title
    // every pixel carried a +/-2-stop multiplier built from ratio noise, with
    // no reason to be temporally stable. Trust == 1 (well-lit pixels) keeps
    // the exact previous math.
    const float floor_trust = ProxyFloorTrust(proxy);
    const float3 gain = exp2(floor_trust * (l_guarded + c));
    result = lerp(original, original * gain, ColorStrength);
  } else {
    // Divisor family: UpgradeToneMap compares in proxy units (the encode
    // divided by Divisor); scale the neural result back to source units
    // immediately so the work surface keeps a single unit system.
    const float divisor = Divisor > 0.0
        ? Divisor
        : max(Norm[uint2(0,0)].r, 1e-8);
    const float3 upgraded = UpgradeToneMap(max(original, 0.0) / divisor, proxy, neural) * divisor;
    const float original_y = max(Luminance(original), 0.0);
    const float upgraded_y = Luminance(upgraded);
    const float ratio = original_y == 0.0 ? 1.0 : clamp(upgraded_y / original_y, 0.0, 4.0);
    const float3 luminance_only = original * ratio;
    result = lerp(luminance_only, upgraded, ColorStrength);
  }
  // The work surfaces are R16G16B16A16_FLOAT (kWorkingFormat): a channel
  // past 65504 is clipped on the write, per channel, which bends a bright
  // colour toward white or a primary.  Scale the whole pixel down to the
  // ceiling instead, keeping its hue (rc11, DATA-09).  Below the ceiling
  // this is the identity.
  const float peak = max(result.x, max(result.y, result.z));
  if (peak > 65504.0) result *= 65504.0 / peak;
  return result;
}

float3 Clean(float3 c) {
    return (asuint(c)&0x7F800000u)!=0x7F800000u?c:(c>0.0?65504.0:0.0);
}
float3 DecodeSource(float3 c) {
    if(Encoding==2u||Encoding==4u)c=PQToLinear(c);
    if(Encoding==3u)c=SrgbDecodeExtended(c);
    if(Encoding==5u)c=sign(c)*pow(abs(c),2.2);
    if(Encoding==4u)c=Bt2020ToBt709(c);
    return c;
}
float3 EncodeSource(float3 c) {
    if(Encoding==4u)c=Bt709ToBt2020(c);
    if(Encoding==2u||Encoding==4u)c=LinearToPQ(c);
    if(Encoding==3u)c=SrgbEncodeExtended(c);
    if(Encoding==5u)c=sign(c)*pow(abs(c),1.0/2.2);
    return c;
}
uint2 Mirror(uint2 p,uint2 size) {
    uint2 period=max(size*2u,uint2(3,3))-2u;
    uint2 folded=p%period;
    return min(folded<size?folded:period-folded,size-1u);
}
float3 ReadLinear(uint2 p) {
    return Clean(DecodeSource(Clean(Original.Load(int3(SourceBase+Mirror(p,SourceSize),0)).rgb)));
}
float2 ReadMotion(uint2 p) {
    // Same linear/clamp sampling convention as Mochizuki runtime_mv_rescale.
    float2 pos=(float2(p)+0.5)*float2(MotionSize)/float2(Size)-0.5;
    int2 a=clamp(int2(floor(pos)),int2(0,0),int2(MotionSize)-1);
    int2 b=min(a+1,int2(MotionSize)-1);
    float2 f=saturate(pos-float2(a));
    float2 m=lerp(lerp(Motion.Load(int3(MotionBase+a,0)).xy,
                            Motion.Load(int3(MotionBase+int2(b.x,a.y),0)).xy,f.x),
                  lerp(Motion.Load(int3(MotionBase+int2(a.x,b.y),0)).xy,
                       Motion.Load(int3(MotionBase+b,0)).xy,f.x),f.y)*MotionScale;
    return all(isfinite(m))?clamp(m,-65504.0,65504.0):0.0;
}
[numthreads(8,8,1)]
void prepare(uint3 p:SV_DispatchThreadID) {
    if(any(p.xy>=Size))return;
    float3 sceneLinear=ReadLinear(p.xy);
    float3 proxy=sceneLinear;
    if(Hdr!=0u) {
        float divisor=Divisor>0.0?Divisor:max(Norm[uint2(0,0)].r,1e-8);
        proxy=max(sceneLinear,0.0)/divisor;
        float m=max(proxy.r,max(proxy.g,proxy.b));
        if(Curve==1u) {
            float m2=m+0.014*m/(m+0.001);
            proxy*=m2/max(m,1e-6);m=m2;
        }
        if(m>0.75)proxy*=(0.75+0.25*(1.0-exp(-5.770780*(m-0.75))))/max(m,1e-6);
    }
    ProxyOut[p.xy]=float4(SrgbEncode(proxy),1.0);
    MotionOut[p.xy]=UseMotion!=0u?ReadMotion(p.xy):float2(0,0);
    if(UseDepth!=0u) {
      uint2 d=min(uint2((float2(p.xy)+0.5)*float2(DepthSize)/float2(Size)),DepthSize-1u);
      float z=Depth.Load(int3(DepthBase+d,0));DepthOut[p.xy]=isfinite(z)?z:0.0;
    }
}
// Both output variants share codec math; only the storage arity differs.
float4 Compose(uint2 p) {
    float4 src=Original.Load(int3(SourceBase+p,0));
    float3 proxy=Proxy.Load(int3(p,0)).rgb;
    float3 neural=Neural.Load(int3(p,0)).rgb;
    // Exact identity before decode/encode, alpha and out-of-gamut values included.
    if(Strength==0.0 || all(proxy==neural) || !all(isfinite(src.rgb)))return src;
    float3 original=DecodeSource(src.rgb);
    float3 resolved=Hdr!=0u?ResolvePixel(p,original):
        original+SrgbDecode(FinishNeural(proxy, neural))-SrgbDecode(proxy);
    float peak=max(SrgbDecode(proxy).r,max(SrgbDecode(proxy).g,SrgbDecode(proxy).b));
    float shadow=lerp(ShadowFloor,1.0,saturate(peak/max(ShadowRange,1e-4)));
    float3 result=lerp(original,resolved,saturate(Strength)*shadow);
    float maximum=max(result.r,max(result.g,result.b));
    if(maximum>65504.0)result*=65504.0/maximum;
    return float4(EncodeSource(result),src.a);
}
[numthreads(8,8,1)]
void composite(uint3 p:SV_DispatchThreadID) {
    if(any(p.xy>=Size))return;
    Destination[SourceBase+p.xy]=Compose(p.xy).rgb;
}
[numthreads(8,8,1)]
void composite_f16(uint3 p:SV_DispatchThreadID) {
    if(any(p.xy>=Size))return;
    Destination4[SourceBase+p.xy]=Compose(p.xy);
}

static const float kEngageStops = 0.15;
static const float kSettleStops = 0.02;
static const float kReadableFloor = 1.0 / 1024.0;
static const uint kReadableSamples = 36u;  // 1/64 of the 64x36 lattice


groupshared uint Histogram[256];
groupshared uint ValidCount;
groupshared uint ShoulderCount;
groupshared float ExposureLogSum[64];
groupshared uint ExposureValidSum[64];

uint AutoScaleBin(float value) {
  const float stops = clamp(log2(max(value, 5.96046448e-8)), -24.0, 24.0);
  return min((uint)floor((stops + 24.0) * (256.0 / 48.0)), 255u);
}

[numthreads(64, 1, 1)]
void normalize(uint local : SV_GroupIndex) {
  float exposureLog=0.0;uint exposureValid=0u;
  // A retained recipe owns Norm. A replay recomputes from its frozen prior
  // stream state and live inputs; it never advances the shared governor.

  [unroll]
  for (uint bin = local; bin < 256u; bin += 64u) Histogram[bin] = 0u;
  if (local == 0u) { ValidCount = 0u; ShoulderCount = 0u; }
  GroupMemoryBarrierWithGroupSync();

  const uint2 source_size = max(SourceSize, uint2(1, 1));
  [loop]
  for (uint index = local; index < 64u * 36u; index += 64u) {
    const uint2 cell = uint2(index & 63u, index >> 6u);
    const uint2 pixel = min(
        uint2(
            ((cell.x * 2u + 1u) * source_size.x) / 128u,
            ((cell.y * 2u + 1u) * source_size.y) / 72u),
        source_size - 1u);
    const float3 rgb = DecodeSource(Original.Load(int3(SourceBase+Mirror(pixel,SourceSize),0)).rgb);
    float luma=dot(rgb,float3(0.2126,0.7152,0.0722));
    if(isfinite(luma)&&luma>0.0){exposureLog+=clamp(log2(luma),-24.0,24.0);++exposureValid;}
    const float value = max(rgb.x, max(rgb.y, rgb.z));
    const uint bits = asuint(value);
    const bool finite = (bits & 0x7F800000u) != 0x7F800000u;
    if (finite && value > 0.0) {
      InterlockedAdd(Histogram[AutoScaleBin(value)], 1u);
      InterlockedAdd(ValidCount, 1u);
      if (value > 0.75) InterlockedAdd(ShoulderCount, 1u);
    }
  }
  ExposureLogSum[local]=exposureLog;ExposureValidSum[local]=exposureValid;
  GroupMemoryBarrierWithGroupSync();

  if (local == 0u) {
    // >=1/32 of valid samples above the encode shoulder proves the current
    // scale is destructive (the flat-white class). The median->0.30 map then
    // clamps at 1.0, so the guardrail can reduce but never brighten; a frame
    // without proven over-range keeps divisor 1 (no rescale at all).  Mode 2
    // replaces the step gate with the continuous weight described above; the
    // guardrail stays one-sided (the stops term is never negative).
    const float gate_fraction =
        ValidCount != 0u ? float(ShoulderCount) / float(ValidCount) : 0.0;
    const float gate_weight = GovernorMode >= 2u
        ? smoothstep(-6.0, -4.0, log2(max(gate_fraction, 1e-6)))
        : (ValidCount != 0u && ShoulderCount * 32u >= ValidCount ? 1.0 : 0.0);
    float candidate = 1.0;
    if (gate_weight > 0.0) {
      const uint middle = (ValidCount - 1u) >> 1u;
      uint cumulative = 0u;
      uint median_bin = 0u;
      uint below = 0u;
      [loop]
      for (uint bin = 0u; bin < 256u; ++bin) {
        const uint next = cumulative + Histogram[bin];
        if (next > middle) {
          median_bin = bin;
          below = cumulative;
          break;
        }
        cumulative = next;
      }
      // Mode 2 places the median at its rank within the bin instead of the
      // bin centre, so the estimate moves continuously with the samples
      // rather than in 0.19-stop bin steps.
      const float within = GovernorMode >= 2u
          ? (float(middle - below) + 0.5) / float(max(Histogram[median_bin], 1u))
          : 0.5;
      const float median_stops =
          -24.0 + (float(median_bin) + within) * (48.0 / 256.0);
      // Modes 0/1 keep the shipped expression bit for bit.
      candidate = GovernorMode >= 2u
          ? exp2(gate_weight * max(median_stops - log2(0.30), 0.0))
          : max(exp2(median_stops) / 0.30, 1.0);
    }

    // Commit.  A previous value of 0 is an uninitialized texel (D3D12
    // zero-initializes a committed resource created with HEAP_FLAG_NONE),
    // which snaps like any other discontinuity rather than slewing up out
    // of nothing over hundreds of frames.
    const bool replay=asuint(Norm[uint2(0,0)].a)==Token;
    const float4 carried=replay?Norm[uint2(1,0)]:Commit[uint2(0,0)];
    if(!replay)Norm[uint2(1,0)]=carried;
    const float previous = carried.r;
    const bool cut_aware = GovernorMode >= 3u;
    // .g: 1 = hold bit, 2 = pending re-prime (modes 0-2 only ever store 0/1,
    // which decode exactly as the `carried.g > 0.5` of rc10).
    const uint flags = uint(clamp(carried.g, 0.0, 3.0) + 0.5);
    const bool was_tracking = (flags & 1u) != 0u;
    // Mode 3's limits come from the real dt and are 0 for two evaluates of
    // one frame, so a zero limit there means "no step", not Off.
    const bool requested = SnapNow != 0.0 || (cut_aware && (flags & 2u) != 0u);
    const bool snap = requested || (!cut_aware && SlewStops <= 0.0)
        || !(previous > 0.0);
    bool readable = true;
    if (cut_aware) {
      const uint floor_bin = AutoScaleBin(previous * kReadableFloor);
      uint lit = 0u;
      [loop]
      for (uint bin = floor_bin + 1u; bin < 256u; ++bin) lit += Histogram[bin];
      readable = lit >= kReadableSamples;
    }
    float committed = candidate;
    float tracking = 0.0;
    float pending = 0.0;
    if (!readable) {
      // Hold, keeping the hysteresis state; an uninitialized texel takes the
      // candidate and leaves the snap pending like a requested one.
      if (previous > 0.0) {
        committed = previous;
        tracking = was_tracking ? 1.0 : 0.0;
      }
      pending = requested || !(previous > 0.0) ? 1.0 : 0.0;
    } else if (!snap) {
      const float error = log2(candidate / previous);
      if (GovernorMode >= 2u) {
        tracking =
            abs(error) > (was_tracking ? kSettleStops : kEngageStops) ? 1.0 : 0.0;
        committed = tracking > 0.0
            ? previous * exp2(clamp(error, -ReleaseStops, SlewStops))
            : previous;
      } else {
        committed = previous * exp2(clamp(error, -SlewStops, SlewStops));
      }
    }
    // Feed v2 is evaluated after the independent v1 governor, as in
    // v6_exposure_scale. Its accepted signal bypasses the content clock.
    const float meterCommitted=committed;
    float heldExposure=carried.b;float rangeTripped=carried.a;
    if(Feed==1u) {
      float e=Exposure.Load(int3(0,0,0)).r;
      float reading=PreExposure/(e*ExposureScale);
      bool validReading=isfinite(reading)&&reading>=5.96046448e-8&&reading<=16777216.0;
      bool canHold=SnapNow==0.0&&isfinite(heldExposure)&&heldExposure>=5.96046448e-8&&heldExposure<=16777216.0;
      heldExposure=validReading?reading:canHold?heldExposure:1.0;
      float sum=0.0;uint count=0u;
      for(uint lane=0;lane<64u;++lane){sum+=ExposureLogSum[lane];count+=ExposureValidSum[lane];}
      bool wasTripped=SnapNow==0.0&&carried.a>0.5;
      rangeTripped=wasTripped?1.0:0.0;
      if(validReading&&count*16u>=64u*36u) {
        float exposed=sum/count-log2(reading);
        float margin=wasTripped?0.5:0.0;
        rangeTripped=exposed>=-7.1876+margin&&exposed<=-0.4150-margin?0.0:1.0;
      }
      if(!validReading&&!canHold)rangeTripped=1.0;
      committed=rangeTripped>0.0?meterCommitted:heldExposure;
    }
    // .b/.a belong to v6_exposure_scale (its held divisor and range-guard
    // state); on a guarded texture feed it runs right after this dispatch.
    if(Token>asuint(Commit[uint2(1,0)].r)) {
      Commit[uint2(0,0)]=float4(meterCommitted,tracking+2.0*pending,heldExposure,rangeTripped);
      Commit[uint2(1,0)]=float4(asfloat(Token),0,0,0);
    }
    // .r is the only channel the encode and resolve read (t3).  .g/.b/.a
    // carry the candidate, the tracking error in stops, and the shoulder-gate
    // fraction (engaged at >= 1/32) for diagnostic captures and the
    // NRNormTrace readback - the governor keeps no CPU-visible state, so
    // those are the only places this can be observed per frame.  A held
    // frame (mode 3) reports its gate fraction negated, -1 when it had no
    // valid sample at all, so the trace can tell a hold from a quiet commit.
    Norm[uint2(0, 0)] = float4(
        committed, candidate, abs(log2(candidate / max(committed, 1e-8))),
        asfloat(Token));
  }
}


