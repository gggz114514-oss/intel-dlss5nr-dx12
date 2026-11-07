// A08: each context A serves adjacent N16 columns; each B serves both existing M8 rows.
//
// T2 (2026-10-18) direct-epilogue variant: the F32 shared bridge of stages 1 and
// 3 (ffn_contract, projection) is deleted.  The parent stored each FP32
// accumulator to `shared float scratch[1024]`, synchronised the whole workgroup,
// read the value back at its (row,col) linear index, applied the residual FMA and
// re-narrowed to half.  The elementwise consumer read the *same* coordinate it
// owned, so no exchange was ever necessary: with layout RowMajor and stride 16 the
// fragment's linear index is `lane + 32*component`, i.e. component `ci` of lane
// `l` is (row = l/16 + 2*ci, col = l%16) - exactly the `e = lane + 32*ci`
// coordinate the parent recomputed.  The FMA, its operand order, its operand
// sources and the single half narrowing point are unchanged; only the transport
// (128 shared stores + 128 shared loads + 2 workgroup barriers per (group,side))
// is removed, and `scratch` (4096 B) disappears.
//
// Write domain is unchanged: `m` is a multiple of 8 and the live token count is a
// multiple of 8 (288), so every block with m < live covers rows m..m+7 < live and
// the parent's per-row `t < pc.p0.x` guard never fired.  Blocks with m >= live are
// still skipped by the existing `if (m>=pc.p0.x) continue;`.
layout(local_size_x=256) in;
#include "vit_common.glsl"
#define MH coopmat<float16_t,gl_ScopeSubgroup,8,16,gl_MatrixUseAccumulator>

// Stages 1 (ffn_contract) and 3/4 (projection) only: direct FP32 fragment ->
// single half narrowing -> fragment store.  No shared array is declared, so the
// kernel's workgroup shared footprint is 0.
#if VIT_STAGE == 1
void vit_direct_epilogue(MC acc,uint m,uint side,uint n0,uint lane) {
    MH res;
    uint col=n0+side*16u+(lane&15u);
    uint row0=m+(lane>>4u);
    for(uint ci=0u;ci<acc.length();++ci) {
        uint t=row0+2u*ci;
        res[ci]=float16_t(fma(float(w[pc.p2.x+col]),
                              float(x[pc.p5.x+t*1024u+col]),acc[ci]));
    }
    coopMatStore(res,y,pc.p5.w+m*1024u+n0+side*16u,1024u,RM);
}
#else
void vit_direct_epilogue(MC acc,uint m,uint side,uint n0,uint lane) {
    MH res;
    uint col=n0+side*16u+(lane&15u);
    uint row0=m+(lane>>4u);
    for(uint ci=0u;ci<acc.length();++ci) {
        uint t=row0+2u*ci;
        res[ci]=float16_t(fma(float(w[pc.p2.y+col]),
                              float(y[pc.p5.w+t*1024u+col]),acc[ci]));
    }
    coopMatStore(res,o,pc.p5.y+m*1024u+n0+side*16u,1024u,RM);
}
#endif

void main() {
    uint sg=gl_SubgroupID,lane=gl_SubgroupInvocationID;
    uint mA=gl_WorkGroupID.x*128u+sg*8u,mB=mA+64u,n0=gl_WorkGroupID.y*32u;
#if VIT_STAGE == 0
    const uint K=1024u; uint wb=pc.p1.x;
#elif VIT_STAGE == 1
    const uint K=4096u; uint wb=pc.p1.y;
#else
    const uint K=1024u; uint wb=pc.p1.w;
#endif
    // P05 complete/single specialization, retaining current A08 N32 owners.
    uint live=pc.p0.x;
    if(mA>=live) return;
    MC accA0=MC(0.0),accA1=MC(0.0);
    if(mB<live) {
        MC accB0=MC(0.0),accB1=MC(0.0);
        for(uint k0=0u;k0<K;k0+=16u) {
            MB b0,b1;
            coopMatLoad(b0,w,vit_wbase(wb,n0,k0,K),16u,CM);
            coopMatLoad(b1,w,vit_wbase(wb,n0+16u,k0,K),16u,CM);
            MA af;
            coopMatLoad(af,context,pc.p6.y+mA*K+k0,K,RM);
            accA0=coopMatMulAdd(af,b0,accA0);
            accA1=coopMatMulAdd(af,b1,accA1);
            coopMatLoad(af,context,pc.p6.y+mB*K+k0,K,RM);
            accB0=coopMatMulAdd(af,b0,accB0);
            accB1=coopMatMulAdd(af,b1,accB1);
        }
        vit_direct_epilogue(accA0,mA,0u,n0,lane);
        vit_direct_epilogue(accA1,mA,1u,n0,lane);
        vit_direct_epilogue(accB0,mB,0u,n0,lane);
        vit_direct_epilogue(accB1,mB,1u,n0,lane);
    } else {
        for(uint k0=0u;k0<K;k0+=16u) {
            MB b0,b1;
            coopMatLoad(b0,w,vit_wbase(wb,n0,k0,K),16u,CM);
            coopMatLoad(b1,w,vit_wbase(wb,n0+16u,k0,K),16u,CM);
            MA af;
            coopMatLoad(af,context,pc.p6.y+mA*K+k0,K,RM);
            accA0=coopMatMulAdd(af,b0,accA0);
            accA1=coopMatMulAdd(af,b1,accA1);
        }
        vit_direct_epilogue(accA0,mA,0u,n0,lane);
        vit_direct_epilogue(accA1,mA,1u,n0,lane);
    }
}
