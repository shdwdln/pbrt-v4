
/*
    pbrt source code is Copyright(c) 1998-2017
                        Matt Pharr, Greg Humphreys, and Wenzel Jakob.

    This file is part of pbrt.

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions are
    met:

    - Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.

    - Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
    IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
    TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
    PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
    HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
    SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
    LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
    DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
    THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
    (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
    OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

 */

#if defined(_MSC_VER)
#define NOMINMAX
#pragma once
#endif

#ifndef PBRT_MATERIALS_H
#define PBRT_MATERIALS_H

// materials.h*
#include <pbrt/pbrt.h>

#include <pbrt/base.h>
#include <pbrt/bssrdf.h>
#include <pbrt/interaction.h>
#include <pbrt/textures.h>
#include <pbrt/util/check.h>
#include <pbrt/util/spectrum.h>
#include <pbrt/util/taggedptr.h>

#include <memory>

namespace pbrt {

struct BumpEvalContext {
    BumpEvalContext() = default;
    PBRT_HOST_DEVICE
    BumpEvalContext(const SurfaceInteraction &si)
        : p(si.p()), dpdu(si.shading.dpdu), dpdv(si.shading.dpdv), dpdx(si.dpdx), dpdy(si.dpdy),
          uv(si.uv), dudx(si.dudx), dvdx(si.dvdx), dudy(si.dudy), dvdy(si.dvdy),
          n(si.shading.n), dndu(si.shading.dndu), dndv(si.shading.dndv) {
    }
    PBRT_HOST_DEVICE
    operator TextureEvalContext() const {
        return TextureEvalContext(p, dpdx, dpdy, uv, dudx, dvdx, dudy, dvdy);
    }

    Point3f p;
    Vector3f dpdu, dpdv;
    Vector3f dpdx, dpdy;
    Point2f uv;
    Float dudx, dvdx, dudy, dvdy;
    Normal3f n;
    Normal3f dndu, dndv;
};

template <typename TextureEvaluator>
PBRT_HOST_DEVICE
bool Bump(TextureEvaluator texEval, FloatTextureHandle displacement,
          const BumpEvalContext &ctx, Vector3f *dpdu, Vector3f *dpdv) {
    if (!displacement) {
        *dpdu = ctx.dpdu;
        *dpdv = ctx.dpdv;
        return true;
    }

    if (!texEval.Matches({displacement}, {}))
        return false;

    // Compute offset positions and evaluate displacement texture
    TextureEvalContext shiftedTexCtx = ctx;

    // Shift _shiftedTexCtx_ _du_ in the $u$ direction
    Float du = .5f * (std::abs(ctx.dudx) + std::abs(ctx.dudy));
    // The most common reason for du to be zero is for ray that start from
    // light sources, where no differentials are available. In this case,
    // we try to choose a small enough du so that we still get a decently
    // accurate bump value.
    if (du == 0) du = .0005f;
    shiftedTexCtx.p = ctx.p + du * ctx.dpdu;
    shiftedTexCtx.uv = ctx.uv + Vector2f(du, 0.f);
    Float uDisplace = texEval(displacement, shiftedTexCtx);

    // Shift _shiftedTexCtx_ _dv_ in the $v$ direction
    Float dv = .5f * (std::abs(ctx.dvdx) + std::abs(ctx.dvdy));
    if (dv == 0) dv = .0005f;
    shiftedTexCtx.p = ctx.p + dv * ctx.dpdv;
    shiftedTexCtx.uv = ctx.uv + Vector2f(0.f, dv);
    Float vDisplace = texEval(displacement, shiftedTexCtx);

    Float displace = texEval(displacement, ctx);

    // Compute bump-mapped differential geometry
    *dpdu = ctx.dpdu +
            (uDisplace - displace) / du * Vector3f(ctx.n) +
            displace * Vector3f(ctx.dndu);
    *dpdv = ctx.dpdv +
            (vDisplace - displace) / dv * Vector3f(ctx.n) +
            displace * Vector3f(ctx.dndv);
    return true;
}

// DielectricMaterial Declarations
class alignas(8) DielectricMaterial {
  public:
    // DielectricMaterial Public Methods
    DielectricMaterial(FloatTextureHandle uRoughness,
                       FloatTextureHandle vRoughness,
                       FloatTextureHandle etaF,
                       SpectrumTextureHandle etaS,
                       FloatTextureHandle displacement,
                       bool remapRoughness)
        : displacement(displacement),
          uRoughness(uRoughness),
          vRoughness(vRoughness),
          etaF(etaF),
          etaS(etaS),
          remapRoughness(remapRoughness) {
        CHECK((bool)etaF ^ (bool)etaS);
    }

    template <typename TextureEvaluator>
    PBRT_HOST_DEVICE
    BSDF *GetBSDF(TextureEvaluator texEval, SurfaceInteraction &si,
                  const SampledWavelengths &lambda,
                  MaterialBuffer &materialBuffer, TransportMode mode) const {
        if (!texEval.Matches({etaF, uRoughness, vRoughness}, {etaS}))
            return nullptr;

        // Compute index of refraction for glass
        Float eta;
        if (etaF)
            eta = texEval(etaF, si);
        else {
            eta = texEval(etaS, si, lambda)[0];
            lambda.TerminateSecondaryWavelengths();
        }

        Float urough = texEval(uRoughness, si), vrough = texEval(vRoughness, si);
        if (remapRoughness) {
            urough = TrowbridgeReitzDistribution::RoughnessToAlpha(urough);
            vrough = TrowbridgeReitzDistribution::RoughnessToAlpha(vrough);
        }
        MicrofacetDistributionHandle distrib = (urough != 0 && vrough != 0) ?
            materialBuffer.Alloc<TrowbridgeReitzDistribution>(urough, vrough) : nullptr;

        // Initialize _bsdf_ for smooth or rough dielectric
        return materialBuffer.Alloc<BSDF>(si,
                                          materialBuffer.Alloc<DielectricInterfaceBxDF>(eta, distrib, mode),
                                          eta);
    }

    PBRT_HOST_DEVICE_INLINE
    FloatTextureHandle GetDisplacement() const { return displacement; }

    static DielectricMaterial *Create(const TextureParameterDictionary &dict,
                                      const FileLoc *loc, Allocator alloc);

    std::string ToString() const;

  private:
    // DielectricMaterial Private Data
    FloatTextureHandle displacement;
    FloatTextureHandle uRoughness, vRoughness;
    FloatTextureHandle etaF;
    SpectrumTextureHandle etaS;
    bool remapRoughness;
};

// ThinDielectricMaterial Declarations
class alignas(8) ThinDielectricMaterial {
  public:
    // ThinDielectricMaterial Public Methods
    ThinDielectricMaterial(FloatTextureHandle etaF,
                           SpectrumTextureHandle etaS,
                           FloatTextureHandle displacement)
        : displacement(displacement),
          etaF(etaF),
          etaS(etaS) {
        CHECK((bool)etaF ^ (bool)etaS);
    }

    template <typename TextureEvaluator>
    PBRT_HOST_DEVICE
    BSDF *GetBSDF(TextureEvaluator texEval, SurfaceInteraction &si,
                  const SampledWavelengths &lambda,
                  MaterialBuffer &materialBuffer, TransportMode mode) const {
        if (!texEval.Matches({etaF}, {etaS}))
            return nullptr;

        Float eta;
        if (etaF)
            eta = texEval(etaF, si);
        else {
            eta = texEval(etaS, si, lambda)[0];
            lambda.TerminateSecondaryWavelengths();
        }

        return materialBuffer.Alloc<BSDF>(si, materialBuffer.Alloc<ThinDielectricBxDF>(eta, mode), eta);
    }

    PBRT_HOST_DEVICE_INLINE
    FloatTextureHandle GetDisplacement() const { return displacement; }

    static ThinDielectricMaterial *Create(const TextureParameterDictionary &dict,
                                          const FileLoc *loc, Allocator alloc);

    std::string ToString() const;

private:
    // ThinDielectricMaterial Private Data
    FloatTextureHandle displacement;
    FloatTextureHandle etaF;
    SpectrumTextureHandle etaS;
};

// HairMaterial Declarations
class alignas(8) HairMaterial {
  public:
    // HairMaterial Public Methods
    HairMaterial(SpectrumTextureHandle sigma_a,
                 SpectrumTextureHandle color,
                 FloatTextureHandle eumelanin,
                 FloatTextureHandle pheomelanin,
                 FloatTextureHandle eta,
                 FloatTextureHandle beta_m,
                 FloatTextureHandle beta_n,
                 FloatTextureHandle alpha)
        : sigma_a(sigma_a),
          color(color),
          eumelanin(eumelanin),
          pheomelanin(pheomelanin),
          eta(eta),
          beta_m(beta_m),
          beta_n(beta_n),
          alpha(alpha) {}

    template <typename TextureEvaluator>
    PBRT_HOST_DEVICE
    BSDF *GetBSDF(TextureEvaluator texEval, SurfaceInteraction &si,
                  const SampledWavelengths &lambda,
                  MaterialBuffer &materialBuffer, TransportMode mode) const {
        if (!texEval.Matches({eumelanin, pheomelanin, eta, beta_m, beta_n, alpha},
                             {sigma_a, color}))
            return nullptr;

        Float bm = std::max<Float>(1e-2, texEval(beta_m, si));
        Float bn = std::max<Float>(1e-2, texEval(beta_n, si));
        Float a = texEval(alpha, si);
        Float e = texEval(eta, si);

        SampledSpectrum sig_a;
        if (sigma_a)
            sig_a = ClampZero(texEval(sigma_a, si, lambda));
        else if (color) {
            SampledSpectrum c = Clamp(texEval(color, si, lambda), 0, 1);
            sig_a = HairBxDF::SigmaAFromReflectance(c, bn, lambda);
        } else {
            CHECK(eumelanin || pheomelanin);
            sig_a = HairBxDF::SigmaAFromConcentration(
                std::max(Float(0), eumelanin ? texEval(eumelanin, si) : 0),
                std::max(Float(0), pheomelanin ? texEval(pheomelanin, si) : 0)).Sample(lambda);
        }

        // Offset along width
        Float h = -1 + 2 * si.uv[1];
        return materialBuffer.Alloc<BSDF>(si, materialBuffer.Alloc<HairBxDF>(h, e, sig_a, bm, bn, a), e);
    }

    static HairMaterial *Create(const TextureParameterDictionary &dict,
                                const FileLoc *loc, Allocator alloc);

    PBRT_HOST_DEVICE_INLINE
    FloatTextureHandle GetDisplacement() const { return nullptr; }

    std::string ToString() const;

  private:
    // HairMaterial Private Data
    SpectrumTextureHandle sigma_a, color;
    FloatTextureHandle eumelanin, pheomelanin, eta;
    FloatTextureHandle beta_m, beta_n, alpha;
};

// DiffuseMaterial Declarations
class alignas(8) DiffuseMaterial {
  public:
    // DiffuseMaterial Public Methods
    DiffuseMaterial(SpectrumTextureHandle reflectance,
                    FloatTextureHandle sigma,
                    FloatTextureHandle displacement)
        : displacement(displacement), reflectance(reflectance), sigma(sigma) {}

    template <typename TextureEvaluator>
    PBRT_HOST_DEVICE
    BSDF *GetBSDF(TextureEvaluator texEval, SurfaceInteraction &si,
                  const SampledWavelengths &lambda,
                  MaterialBuffer &materialBuffer, TransportMode mode) const {
        if (!texEval.Matches({sigma}, {reflectance}))
            return nullptr;

        // Evaluate textures for _DiffuseMaterial_ material and allocate BRDF
        SampledSpectrum r = Clamp(texEval(reflectance, si, lambda), 0, 1);
        Float sig = Clamp(texEval(sigma, si), 0, 90);
        LambertianBxDF *bxdf =
            materialBuffer.Alloc<LambertianBxDF>(r, SampledSpectrum(0), sig);
        return materialBuffer.Alloc<BSDF>(si, bxdf);
    }

    PBRT_HOST_DEVICE_INLINE
    FloatTextureHandle GetDisplacement() const { return displacement; }

    static DiffuseMaterial *Create(const TextureParameterDictionary &dict,
                                   const FileLoc *loc, Allocator alloc);

    std::string ToString() const;

  private:
    // DiffuseMaterial Private Data
    FloatTextureHandle displacement;
    SpectrumTextureHandle reflectance;
    FloatTextureHandle sigma;
};

// ConductorMaterial Declarations
class alignas(8) ConductorMaterial {
  public:
    // ConductorMaterial Public Methods
    ConductorMaterial(SpectrumTextureHandle eta,
                      SpectrumTextureHandle k,
                      FloatTextureHandle uRoughness,
                      FloatTextureHandle vRoughness,
                      FloatTextureHandle displacement,
                      bool remapRoughness)
    : displacement(displacement), eta(eta), k(k), uRoughness(uRoughness),
      vRoughness(vRoughness), remapRoughness(remapRoughness) {}

    template <typename TextureEvaluator>
    PBRT_HOST_DEVICE
    BSDF *GetBSDF(TextureEvaluator texEval, SurfaceInteraction &si, const SampledWavelengths &lambda,
                  MaterialBuffer &materialBuffer, TransportMode mode) const {
        if (!texEval.Matches({uRoughness, vRoughness}, {eta, k}))
            return nullptr;

        Float uRough = texEval(uRoughness, si);
        Float vRough = texEval(vRoughness, si);
        if (remapRoughness) {
            uRough = TrowbridgeReitzDistribution::RoughnessToAlpha(uRough);
            vRough = TrowbridgeReitzDistribution::RoughnessToAlpha(vRough);
        }
        FresnelHandle frMf = materialBuffer.Alloc<FresnelConductor>(texEval(eta, si, lambda),
                                                                    texEval(k, si, lambda));
        if (uRough == 0 || vRough == 0) {
            return materialBuffer.Alloc<BSDF>(si,
                                              materialBuffer.Alloc<SpecularReflectionBxDF>(frMf));
        } else {
            MicrofacetDistributionHandle distrib =
                materialBuffer.Alloc<TrowbridgeReitzDistribution>(uRough, vRough);
            return materialBuffer.Alloc<BSDF>(si,
                                              materialBuffer.Alloc<MicrofacetReflectionBxDF>(distrib, frMf));
        }
    }

    PBRT_HOST_DEVICE_INLINE
    FloatTextureHandle GetDisplacement() const { return displacement; }

    static ConductorMaterial *Create(const TextureParameterDictionary &dict,
                                     const FileLoc *loc, Allocator alloc);

    std::string ToString() const;

  private:
    // ConductorMaterial Private Data
    FloatTextureHandle displacement;
    SpectrumTextureHandle eta, k;
    FloatTextureHandle uRoughness, vRoughness;
    bool remapRoughness;
};

// MixMaterial Declarations
class alignas(8) MixMaterial {
  public:
    // MixMaterial Public Methods
    MixMaterial(MaterialHandle m1, MaterialHandle m2, FloatTextureHandle amount)
        : m1(m1), m2(m2), amount(amount) {}

    template <typename TextureEvaluator>
    PBRT_HOST_DEVICE
    BSDF *GetBSDF(TextureEvaluator texEval, SurfaceInteraction &si, const SampledWavelengths &lambda,
                  MaterialBuffer &materialBuffer, TransportMode mode) const {
        if (!texEval.Matches({amount}, {}))
            return nullptr;

        // Compute weights and original _BxDF_s for mix material
        BSDF *bsdfs[2] = { m1.GetBSDF(texEval, si, lambda, materialBuffer, mode),
                           m2.GetBSDF(texEval, si, lambda, materialBuffer, mode) };
        if (!bsdfs[0] || !bsdfs[1])
            return nullptr;
        Float t = Clamp(texEval(amount, si), 0, 1);

        Float eta = Lerp(t, bsdfs[0]->eta, bsdfs[1]->eta);
        return materialBuffer.Alloc<BSDF>(si,
                                          materialBuffer.Alloc<MixBxDF>(t, bsdfs[0]->GetBxDF(), bsdfs[1]->GetBxDF()),
                                          eta);
    }

    static MixMaterial *Create(const TextureParameterDictionary &dict,
                               MaterialHandle m1, MaterialHandle m2,
                               const FileLoc *loc, Allocator alloc);

    PBRT_HOST_DEVICE_INLINE
    // FIXME?
    FloatTextureHandle GetDisplacement() const { return m1.GetDisplacement(); }

    std::string ToString() const;

  private:
    // MixMaterial Private Data
    MaterialHandle m1, m2;
    FloatTextureHandle amount;
};


// CoatedDiffuseMaterial Declarations
class alignas(8) CoatedDiffuseMaterial {
  public:
    // CoatedDiffuseMaterial Public Methods
    CoatedDiffuseMaterial(SpectrumTextureHandle reflectance,
                          FloatTextureHandle uRoughness,
                          FloatTextureHandle vRoughness,
                          FloatTextureHandle thickness,
                          FloatTextureHandle eta,
                          FloatTextureHandle displacement,
                          bool remapRoughness,
                          LayeredBxDFConfig config)
        : displacement(displacement),
          reflectance(reflectance),
          uRoughness(uRoughness),
          vRoughness(vRoughness),
          thickness(thickness),
          eta(eta),
          remapRoughness(remapRoughness),
          config(config) {}

    template <typename TextureEvaluator>
    PBRT_HOST_DEVICE
    BSDF *GetBSDF(TextureEvaluator texEval, SurfaceInteraction &si,
                  const SampledWavelengths &lambda,
                  MaterialBuffer &materialBuffer, TransportMode mode) const {
        if (!texEval.Matches({uRoughness, vRoughness, thickness, eta},
                             {reflectance}))
            return nullptr;

        // Initialize diffuse component of plastic material
        SampledSpectrum r = Clamp(texEval(reflectance, si, lambda), 0, 1);

        // Create microfacet distribution _distrib_ for plastic material
        Float urough = texEval(uRoughness, si);
        Float vrough = texEval(vRoughness, si);
        if (remapRoughness) {
            urough = TrowbridgeReitzDistribution::RoughnessToAlpha(urough);
            vrough = TrowbridgeReitzDistribution::RoughnessToAlpha(vrough);
        }
        MicrofacetDistributionHandle distrib =
            materialBuffer.Alloc<TrowbridgeReitzDistribution>(urough, vrough);

        Float thick = texEval(thickness, si);
        Float e = texEval(eta, si);

        BxDFHandle lb = materialBuffer.Alloc<CoatedDiffuseBxDF>(DielectricInterfaceBxDF(e, distrib, mode),
                                                                LambertianBxDF(r, SampledSpectrum(0), 0),
                                                                thick,
                                                                SampledSpectrum(0) /* albedo */,
                                                                0 /* g */,
                                                                config);
        return materialBuffer.Alloc<BSDF>(si, lb);
    }

    PBRT_HOST_DEVICE_INLINE
    FloatTextureHandle GetDisplacement() const { return displacement; }

    static CoatedDiffuseMaterial *Create(const TextureParameterDictionary &dict,
                                         const FileLoc *loc, Allocator alloc);

    std::string ToString() const;

  private:
    // CoatedDiffuseMaterial Private Data
    FloatTextureHandle displacement;
    SpectrumTextureHandle reflectance;
    FloatTextureHandle uRoughness, vRoughness, thickness, eta;
    bool remapRoughness;
    LayeredBxDFConfig config;
};

class alignas(8) LayeredMaterial {
  public:
    LayeredMaterial(MaterialHandle top, MaterialHandle base,
                    FloatTextureHandle thickness,
                    SpectrumTextureHandle albedo,
                    FloatTextureHandle g,
                    FloatTextureHandle displacement,
                    LayeredBxDFConfig config)
        : displacement(displacement),
          top(top),
          base(base),
          thickness(thickness),
          albedo(albedo),
          g(g),
          config(config) {}

    template <typename TextureEvaluator>
    PBRT_HOST_DEVICE
    BSDF *GetBSDF(TextureEvaluator texEval, SurfaceInteraction &si,
                  const SampledWavelengths &lambda,
                  MaterialBuffer &materialBuffer, TransportMode mode) const {
        BSDF *topBSDF = top.GetBSDF(texEval, si, lambda, materialBuffer, mode);
        BSDF *bottomBSDF = base.GetBSDF(texEval, si, lambda, materialBuffer, mode);
        if (!topBSDF || !bottomBSDF)
            return nullptr;

        if (!texEval.Matches({thickness, g}, {albedo}))
            return nullptr;

        Float thick = texEval(thickness, si);
        SampledSpectrum a = texEval(albedo, si, lambda);
        Float gg = texEval(g, si);

        BxDFHandle layered = materialBuffer.Alloc<GeneralLayeredBxDF>(topBSDF->GetBxDF(),
                                                                      bottomBSDF->GetBxDF(),
                                                                      thick, a, gg, config);
        return materialBuffer.Alloc<BSDF>(si, layered);
    }

    PBRT_HOST_DEVICE_INLINE
    FloatTextureHandle GetDisplacement() const { return displacement; }

    static LayeredMaterial *Create(const TextureParameterDictionary &dict,
                                   MaterialHandle top, MaterialHandle base,
                                   const FileLoc *loc, Allocator alloc);

    std::string ToString() const;

  private:
    FloatTextureHandle displacement;
    MaterialHandle top, base;
    FloatTextureHandle thickness;
    SpectrumTextureHandle albedo;
    FloatTextureHandle g;
    LayeredBxDFConfig config;
};


// SubsurfaceMaterial Declarations
class alignas(8) SubsurfaceMaterial {
  public:
    // SubsurfaceMaterial Public Methods
    SubsurfaceMaterial(Float scale,
                       SpectrumTextureHandle sigma_a,
                       SpectrumTextureHandle sigma_s,
                       SpectrumTextureHandle reflectance,
                       SpectrumTextureHandle mfp,
                       Float g, Float eta,
                       FloatTextureHandle uRoughness,
                       FloatTextureHandle vRoughness,
                       FloatTextureHandle displacement,
                       bool remapRoughness,
                       Allocator alloc)
        : displacement(displacement),
          scale(scale),
          sigma_a(sigma_a),
          sigma_s(sigma_s),
          reflectance(reflectance),
          mfp(mfp),
          uRoughness(uRoughness),
          vRoughness(vRoughness),
          eta(eta),
          remapRoughness(remapRoughness),
          table(100, 64, alloc) {
        ComputeBeamDiffusionBSSRDF(g, eta, &table);
    }

    template <typename TextureEvaluator>
    PBRT_HOST_DEVICE
    BSDF *GetBSDF(TextureEvaluator texEval, SurfaceInteraction &si,
                  const SampledWavelengths &lambda,
                  MaterialBuffer &materialBuffer, TransportMode mode) const {
        if (!texEval.Matches({uRoughness, vRoughness}, {}))
            return nullptr;

        // Initialize BSDF for _SubsurfaceMaterial_

        Float urough = texEval(uRoughness, si), vrough = texEval(vRoughness, si);
        if (remapRoughness) {
            urough = TrowbridgeReitzDistribution::RoughnessToAlpha(urough);
            vrough = TrowbridgeReitzDistribution::RoughnessToAlpha(vrough);
        }
        MicrofacetDistributionHandle distrib = (urough != 0 && vrough != 0) ?
            materialBuffer.Alloc<TrowbridgeReitzDistribution>(urough, vrough) : nullptr;

        // Initialize _bsdf_ for smooth or rough dielectric
        return materialBuffer.Alloc<BSDF>(si, materialBuffer.Alloc<DielectricInterfaceBxDF>(eta, distrib, mode), eta);
    }

    template <typename TextureEvaluator>
    PBRT_HOST_DEVICE
    BSSRDFHandle GetBSSRDF(TextureEvaluator texEval, SurfaceInteraction &si,
                           const SampledWavelengths &lambda,
                           MaterialBuffer &materialBuffer, TransportMode mode) const {
        SampledSpectrum sig_a, sig_s;
        if (sigma_a && sigma_s) {
            if (!texEval.Matches({}, {sigma_a, sigma_s}))
                return nullptr;

            sig_a = ClampZero(scale * texEval(sigma_a, si, lambda));
            sig_s = ClampZero(scale * texEval(sigma_s, si, lambda));
        } else {
            DCHECK(reflectance && mfp);
            if (!texEval.Matches({}, {mfp, reflectance}))
                return nullptr;

            SampledSpectrum mfree = ClampZero(scale * texEval(mfp, si, lambda));
            SampledSpectrum r = Clamp(texEval(reflectance, si, lambda), 0, 1);
            SubsurfaceFromDiffuse(table, r, mfree, &sig_a, &sig_s);
        }

        return materialBuffer.Alloc<TabulatedBSSRDF>(si, eta, mode, sig_a, sig_s, table);
    }

    PBRT_HOST_DEVICE_INLINE
    FloatTextureHandle GetDisplacement() const { return displacement; }

    static SubsurfaceMaterial *Create(const TextureParameterDictionary &dict,
                                      const FileLoc *loc, Allocator alloc);

    std::string ToString() const;

  private:
    // SubsurfaceMaterial Private Data
    FloatTextureHandle displacement;
    Float scale;
    SpectrumTextureHandle sigma_a, sigma_s, reflectance, mfp;
    FloatTextureHandle uRoughness, vRoughness;
    Float eta;
    bool remapRoughness;
    BSSRDFTable table;
};


// DiffuseTransmissionMaterial Declarations
class alignas(8) DiffuseTransmissionMaterial {
  public:
    // DiffuseTransmissionMaterial Public Methods
    DiffuseTransmissionMaterial(SpectrumTextureHandle reflectance,
                                SpectrumTextureHandle transmittance,
                                FloatTextureHandle sigma,
                                FloatTextureHandle displacement,
                                Float scale)
        : displacement(displacement),
          reflectance(reflectance),
          transmittance(transmittance),
          sigma(sigma),
          scale(scale) { }

    template <typename TextureEvaluator>
    PBRT_HOST_DEVICE
    BSDF *GetBSDF(TextureEvaluator texEval, SurfaceInteraction &si,
                  const SampledWavelengths &lambda,
                  MaterialBuffer &materialBuffer, TransportMode mode) const {
        if (!texEval.Matches({sigma}, {reflectance, transmittance}))
            return nullptr;

        SampledSpectrum r = Clamp(scale * texEval(reflectance, si, lambda), 0, 1);
        SampledSpectrum t = Clamp(scale * texEval(transmittance, si, lambda), 0, 1);
        Float s = texEval(sigma, si);
        return materialBuffer.Alloc<BSDF>(si, materialBuffer.Alloc<LambertianBxDF>(r, t, s));
    }

    PBRT_HOST_DEVICE_INLINE
    FloatTextureHandle GetDisplacement() const { return displacement; }

    static DiffuseTransmissionMaterial *Create(const TextureParameterDictionary &dict,
                                               const FileLoc *loc, Allocator alloc);

    std::string ToString() const;

  private:
    // DiffuseTransmissionMaterial Private Data
    FloatTextureHandle displacement;
    SpectrumTextureHandle reflectance, transmittance;
    FloatTextureHandle sigma;
    Float scale;
};

class alignas(8) MeasuredMaterial {
public:
    MeasuredMaterial(const std::string &filename, FloatTextureHandle displacement,
                     Allocator alloc);

    template <typename TextureEvaluator>
    PBRT_HOST_DEVICE
    BSDF *GetBSDF(TextureEvaluator texEval, SurfaceInteraction &si,
                  const SampledWavelengths &lambda,
                  MaterialBuffer &materialBuffer, TransportMode mode) const {
        return materialBuffer.Alloc<BSDF>(si, materialBuffer.Alloc<MeasuredBxDF>(brdfData, lambda));
    }

    PBRT_HOST_DEVICE_INLINE
    FloatTextureHandle GetDisplacement() const { return displacement; }

    static MeasuredMaterial *Create(const TextureParameterDictionary &dict,
                                    const FileLoc *loc, Allocator alloc);

    std::string ToString() const;

  private:
    FloatTextureHandle displacement;
    const MeasuredBRDFData *brdfData;
};

template <typename TextureEvaluator>
inline BSDF *
MaterialHandle::GetBSDF(TextureEvaluator texEval, SurfaceInteraction &si,
                        const SampledWavelengths &lambda,
                        MaterialBuffer &materialBuffer, TransportMode mode) const {
    DCHECK(ptr() != nullptr);

    switch (Tag()) {
    case TypeIndex<CoatedDiffuseMaterial>():
        return Cast<CoatedDiffuseMaterial>()->GetBSDF(texEval, si, lambda, materialBuffer, mode);
    case TypeIndex<ConductorMaterial>():
        return Cast<ConductorMaterial>()->GetBSDF(texEval, si, lambda, materialBuffer, mode);
    case TypeIndex<DielectricMaterial>():
        return Cast<DielectricMaterial>()->GetBSDF(texEval, si, lambda, materialBuffer, mode);
    case TypeIndex<DiffuseMaterial>():
        return Cast<DiffuseMaterial>()->GetBSDF(texEval, si, lambda, materialBuffer, mode);
    case TypeIndex<DiffuseTransmissionMaterial>():
        return Cast<DiffuseTransmissionMaterial>()->GetBSDF(texEval, si, lambda, materialBuffer, mode);
#ifndef __CUDA_ARCH__
    case TypeIndex<HairMaterial>():
        return Cast<HairMaterial>()->GetBSDF(texEval, si, lambda, materialBuffer, mode);
    case TypeIndex<LayeredMaterial>():
        return Cast<LayeredMaterial>()->GetBSDF(texEval, si, lambda, materialBuffer, mode);
#endif // __CUDA_ARCH__
    case TypeIndex<MeasuredMaterial>():
        return Cast<MeasuredMaterial>()->GetBSDF(texEval, si, lambda, materialBuffer, mode);
    case TypeIndex<MixMaterial>():
        return Cast<MixMaterial>()->GetBSDF(texEval, si, lambda, materialBuffer, mode);
    case TypeIndex<SubsurfaceMaterial>():
        return Cast<SubsurfaceMaterial>()->GetBSDF(texEval, si, lambda, materialBuffer, mode);
    case TypeIndex<ThinDielectricMaterial>():
        return Cast<ThinDielectricMaterial>()->GetBSDF(texEval, si, lambda, materialBuffer, mode);
    default:
        LOG_FATAL("Unhandled Material type: tag %d", Tag());
        return {};
    }
}

template <typename TextureEvaluator>
inline BSSRDFHandle
MaterialHandle::GetBSSRDF(TextureEvaluator texEval, SurfaceInteraction &si,
                          const SampledWavelengths &lambda,
                          MaterialBuffer &materialBuffer, TransportMode mode) const {
    if (Is<SubsurfaceMaterial>())
        return Cast<SubsurfaceMaterial>()->GetBSSRDF(texEval, si, lambda,
                                                     materialBuffer, mode);
    return nullptr;
}

inline bool MaterialHandle::IsTransparent() const {
    if (Is<ThinDielectricMaterial>())
        return true;
    return false;
}

inline bool MaterialHandle::HasSubsurfaceScattering() const {
    return Is<SubsurfaceMaterial>();
}

inline FloatTextureHandle MaterialHandle::GetDisplacement() const {
    switch (Tag()) {
    case TypeIndex<CoatedDiffuseMaterial>():
        return Cast<CoatedDiffuseMaterial>()->GetDisplacement();
    case TypeIndex<ConductorMaterial>():
        return Cast<ConductorMaterial>()->GetDisplacement();
    case TypeIndex<DielectricMaterial>():
        return Cast<DielectricMaterial>()->GetDisplacement();
    case TypeIndex<DiffuseMaterial>():
        return Cast<DiffuseMaterial>()->GetDisplacement();
    case TypeIndex<DiffuseTransmissionMaterial>():
        return Cast<DiffuseTransmissionMaterial>()->GetDisplacement();
#ifndef __CUDA_ARCH__
    case TypeIndex<HairMaterial>():
        return Cast<HairMaterial>()->GetDisplacement();
    case TypeIndex<LayeredMaterial>():
        return Cast<LayeredMaterial>()->GetDisplacement();
#endif // __CUDA_ARCH__
    case TypeIndex<MeasuredMaterial>():
        return Cast<MeasuredMaterial>()->GetDisplacement();
    case TypeIndex<MixMaterial>():
        return Cast<MixMaterial>()->GetDisplacement();
    case TypeIndex<SubsurfaceMaterial>():
        return Cast<SubsurfaceMaterial>()->GetDisplacement();
    case TypeIndex<ThinDielectricMaterial>():
        return Cast<ThinDielectricMaterial>()->GetDisplacement();
    default:
        LOG_FATAL("Unhandled Material type: tag %d", Tag());
        return {};
    }
}

}  // namespace pbrt

#endif  // PBRT_MATERIALS_UBER_H
