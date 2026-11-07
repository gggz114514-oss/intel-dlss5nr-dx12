#pragma once
#include <cstdint>
#include <cmath>

namespace nrb {
// NGX texel * scale is pixels of the effective motion extent. Mochizuki's
// adapter divides by that extent; this proxy stores render pixels instead.
inline float ngx_motion_to_render_pixels(float scale, uint32_t render_extent,
                                        uint32_t motion_extent) {
    return scale * (float(render_extent) / float(motion_extent));
}
// Separate from AmdPreSr::Settings::encoding and RenoDX's source_encoding.
enum class InputEncoding : uint32_t { Auto, Sdr, Linear, Pq, Gamma22, Pq2020 };
enum class InputStorage : uint32_t { Float, Unorm10, Unorm8 };
enum class InputCodec : uint32_t { Auto, Divisor, Display };
struct InputRect {
    uint32_t x=0,y=0,width=0,height=0;
    bool fits(uint32_t w,uint32_t h) const {
        return width && height && x<=w && y<=h && width<=w-x && height<=h-y;
    }
    bool operator==(const InputRect&) const = default;
};
struct InputColorContract {
    // Shader encoding matches V6EncodingConstant, plus explicit Gamma 2.2.
    uint32_t encoding=1,curve=0,hdr=0;
    float divisor=1,unitNits=0;
    bool valid=false,relative=false;
    bool operator==(const InputColorContract&) const = default;
};
inline InputEncoding legacy_input_encoding(uint32_t amd) {
    switch(amd) {
    case 1:return InputEncoding::Linear;
    case 2:return InputEncoding::Sdr;
    case 3:return InputEncoding::Gamma22;
    default:return InputEncoding::Auto;
    }
}
// InterpretSourceUnits / FrameDisplayCodec / FrameCodecDivisor from the pinned
// Mochizuki RenoDX stage. Storage never establishes absolute linear nits.
inline InputColorContract input_color_contract(InputStorage storage, bool flagsKnown,
    bool hdrFlag, InputEncoding explicitEncoding, uint32_t amdEncoding,
    InputCodec codec=InputCodec::Auto, float linearUnitNits=0) {
    InputColorContract c;
    auto e=explicitEncoding==InputEncoding::Auto?legacy_input_encoding(amdEncoding):explicitEncoding;
    if(e==InputEncoding::Auto) {
        if(storage==InputStorage::Float)e=InputEncoding::Linear;
        else if(!flagsKnown || (storage==InputStorage::Unorm8 && hdrFlag))return c;
        else e=storage==InputStorage::Unorm10 && hdrFlag?InputEncoding::Pq:InputEncoding::Sdr;
    }
    if(!std::isfinite(linearUnitNits)||linearUnitNits<0)return c;
    switch(e) {
    case InputEncoding::Sdr:c.encoding=3;break;
    case InputEncoding::Gamma22:c.encoding=5;break;
    case InputEncoding::Linear:c.encoding=1;c.hdr=1;c.unitNits=linearUnitNits;break;
    case InputEncoding::Pq:c.encoding=2;c.hdr=1;c.unitNits=10000;break;
    case InputEncoding::Pq2020:c.encoding=4;c.hdr=1;c.unitNits=10000;break;
    default:return c;
    }
    c.relative=c.hdr && c.unitNits==0;
    c.curve=c.hdr && (codec==InputCodec::Display || (codec==InputCodec::Auto && c.relative))?1u:0u;
    // Official classic defaults: 203 nits diffuse white, 2.5375 paper white.
    c.divisor=!c.hdr?1.0f:c.relative?0.0f:c.curve?(c.encoding==2||c.encoding==4?203.0f:4.0f)/c.unitNits:
        (e==InputEncoding::Pq||e==InputEncoding::Pq2020)?203.0f/10000.0f:2.5375f;
    c.valid=true;return c;
}
struct InputHistoryIdentity {
    uintptr_t feature=0;
    InputRect colour,motion,depth;
    uint32_t sourceFormat=0,motionFormat=0,depthFormat=0;
    uint32_t depthWidth=0,depthHeight=0;
    InputColorContract color;
    float motionScaleX=0,motionScaleY=0;
    bool motionUsable=false,depthInverted=false;
    uint32_t feed=0;
    bool operator==(const InputHistoryIdentity&) const = default;
};
inline bool input_history_reset(const InputHistoryIdentity& previous,
    const InputHistoryIdentity& current,bool primed,bool reset,bool replay) {
    return reset || replay || !primed || previous!=current || !current.motionUsable;
}
}
