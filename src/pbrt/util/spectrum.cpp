
/*
    pbrt source code is Copyright(c) 1998-2016
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

// spectrum/spectrum.cpp*
#include <pbrt/util/spectrum.h>

#include <pbrt/gpu.h>
#include <pbrt/util/color.h>
#include <pbrt/util/colorspace.h>
#include <pbrt/util/error.h>
#include <pbrt/util/file.h>
#include <pbrt/util/print.h>
#include <pbrt/util/rng.h>
#include <pbrt/util/sampling.h>
#include <pbrt/util/stats.h>

#include <algorithm>
#include <cmath>
#include <map>

namespace pbrt {

Float Blackbody(Float lambda, Float T) {
    if (T <= 0) return 0;

    const Float c = 299792458;
    const Float h = 6.62606957e-34;
    const Float kb = 1.3806488e-23;

    // Compute emitted radiance for blackbody at wavelength _lambda_
    Float l = lambda * 1e-9;
    Float Le =
        (2 * h * c * c) / (Pow<5>(l) * (std::exp((h * c) / (l * kb * T)) - 1));
    CHECK(!std::isnan(Le));
    return Le;
}

std::string SpectrumHandle::ToString() const {
    switch (Tag()) {
    case TypeIndex<BlackbodySpectrum>():
        return Cast<BlackbodySpectrum>()->ToString();
    case TypeIndex<ConstantSpectrum>():
        return Cast<ConstantSpectrum>()->ToString();
    case TypeIndex<ProductSpectrum>():
        return Cast<ProductSpectrum>()->ToString();
    case TypeIndex<ScaledSpectrum>():
        return Cast<ScaledSpectrum>()->ToString();
    case TypeIndex<PiecewiseLinearSpectrum>():
        return Cast<PiecewiseLinearSpectrum>()->ToString();
    case TypeIndex<DenselySampledSpectrum>():
        return Cast<DenselySampledSpectrum>()->ToString();
    case TypeIndex<RGBReflectanceSpectrum>():
        return Cast<RGBReflectanceSpectrum>()->ToString();
    case TypeIndex<RGBSpectrum>():
        return Cast<RGBSpectrum>()->ToString();
    default:
        LOG_FATAL("Unhandled Spectrum type %d", Tag());
        return {};
    }
}

std::string SpectrumHandle::ParameterType() const {
    switch (Tag()) {
    case TypeIndex<BlackbodySpectrum>():
        return Cast<BlackbodySpectrum>()->ParameterType();
    case TypeIndex<ConstantSpectrum>():
        return Cast<ConstantSpectrum>()->ParameterType();
    case TypeIndex<ProductSpectrum>():
        return Cast<ProductSpectrum>()->ParameterType();
    case TypeIndex<ScaledSpectrum>():
        return Cast<ScaledSpectrum>()->ParameterType();
    case TypeIndex<PiecewiseLinearSpectrum>():
        return Cast<PiecewiseLinearSpectrum>()->ParameterType();
    case TypeIndex<DenselySampledSpectrum>():
        return Cast<DenselySampledSpectrum>()->ParameterType();
    case TypeIndex<RGBReflectanceSpectrum>():
        return Cast<RGBReflectanceSpectrum>()->ParameterType();
    case TypeIndex<RGBSpectrum>():
        return Cast<RGBSpectrum>()->ParameterType();
    default:
        LOG_FATAL("Unhandled Spectrum type %d", Tag());
        return {};
    }
}

std::string SpectrumHandle::ParameterString() const {
    switch (Tag()) {
    case TypeIndex<BlackbodySpectrum>():
        return Cast<BlackbodySpectrum>()->ParameterString();
    case TypeIndex<ConstantSpectrum>():
        return Cast<ConstantSpectrum>()->ParameterString();
    case TypeIndex<ProductSpectrum>():
        return Cast<ProductSpectrum>()->ParameterString();
    case TypeIndex<ScaledSpectrum>():
        return Cast<ScaledSpectrum>()->ParameterString();
    case TypeIndex<PiecewiseLinearSpectrum>():
        return Cast<PiecewiseLinearSpectrum>()->ParameterString();
    case TypeIndex<DenselySampledSpectrum>():
        return Cast<DenselySampledSpectrum>()->ParameterString();
    case TypeIndex<RGBReflectanceSpectrum>():
        return Cast<RGBReflectanceSpectrum>()->ParameterString();
    case TypeIndex<RGBSpectrum>():
        return Cast<RGBSpectrum>()->ParameterString();
    default:
        LOG_FATAL("Unhandled Spectrum type %d", Tag());
        return {};
    }
}

Float SpectrumToY(SpectrumHandle s) {
    Float y = 0;
    for (Float lambda = LambdaMin; lambda <= LambdaMax; ++lambda)
        y += SPDs::Y()(lambda) * s(lambda);
    return y / SPDs::CIE_Y_integral;
}

XYZ SpectrumToXYZ(SpectrumHandle s) {
    XYZ xyz;
    for (Float lambda = LambdaMin; lambda <= LambdaMax; ++lambda) {
        xyz.X += SPDs::X()(lambda) * s(lambda);
        xyz.Y += SPDs::Y()(lambda) * s(lambda);
        xyz.Z += SPDs::Z()(lambda) * s(lambda);
    }
    return xyz / SPDs::CIE_Y_integral;
}

PiecewiseLinearSpectrum::PiecewiseLinearSpectrum(pstd::span<const Float> l,
                                                 pstd::span<const Float> values,
                                                 Allocator alloc)
    : lambda(l.begin(), l.end(), alloc), v(values.begin(), values.end(), alloc) {
    CHECK_EQ(lambda.size(), v.size());
    for (size_t i = 0; i < lambda.size() - 1; ++i)
        CHECK_LT(lambda[i], lambda[i + 1]);
}

Float PiecewiseLinearSpectrum::operator()(Float l) const {
    if (lambda.empty()) return 0;
    if (l <= lambda.front()) return v.front();
    if (l >= lambda.back()) return v.back();
    int offset = FindInterval(lambda.size(),
                              [&](int index) { return lambda[index] <= l; });
    DCHECK(l >= lambda[offset] && l <= lambda[offset + 1]);
    Float t = (l - lambda[offset]) / (lambda[offset + 1] - lambda[offset]);
    return Lerp(t, v[offset], v[offset + 1]);
}

Float PiecewiseLinearSpectrum::MaxValue() const {
    if (v.empty()) return 0;
    return *std::max_element(v.begin(), v.end());
}

std::string PiecewiseLinearSpectrum::ToString() const {
    return std::string("[ PiecewiseLinearSpectrum ") + ParameterString() + " ]";
}

std::string PiecewiseLinearSpectrum::ParameterType() const {
    return "spectrum";
}

std::string PiecewiseLinearSpectrum::ParameterString() const {
    std::string name = SPDs::FindMatchingNamed(this);
    if (!name.empty())
        return StringPrintf("\"%s\"", name);

    std::string ret;
    for (size_t i = 0; i < lambda.size(); ++i)
        ret += StringPrintf("%f %f ", lambda[i], v[i]);
    return ret;
}

pstd::optional<SpectrumHandle> PiecewiseLinearSpectrum::Read(const std::string &fn,
                                                             Allocator alloc) {
    pstd::optional<std::vector<Float>> vals = ReadFloatFile(fn);
    if (!vals) {
        Warning("%s: unable to read spectrum file.", fn);
        return {};
    } else {
        if (vals->size() % 2 != 0) {
            Warning("%s: extra value found in spectrum file.", fn);
            return {};
        }
        std::vector<Float> lambda, v;
        for (size_t i = 0; i < vals->size() / 2; ++i) {
            if (i > 0 && (*vals)[2 * i] <= lambda.back()) {
                Warning("%s: spectrum file invalid: at %d'th entry, wavelengths aren't "
                        "increasing: %f >= %f.", fn, int(i), lambda.back(), (*vals)[2 * i]);
                return {};
            }
            lambda.push_back((*vals)[2 * i]);
            v.push_back((*vals)[2 * i + 1]);
        }
        SpectrumHandle handle = alloc.new_object<PiecewiseLinearSpectrum>(lambda, v, alloc);
        return handle;
    }
}

SampledSpectrum BlackbodySpectrum::Sample(const SampledWavelengths &lambda) const {
    SampledSpectrum s;
    for (int i = 0; i < NSpectrumSamples; ++i)
        s[i] = scale * Blackbody(lambda[i], T);
    return s;
}

std::string BlackbodySpectrum::ToString() const {
    return StringPrintf("[ BlackbodySpectrum %f ]", T);
}

std::string BlackbodySpectrum::ParameterType() const {
    return "blackbody";
}

std::string BlackbodySpectrum::ParameterString() const {
    return StringPrintf("%f", T);
}

SampledSpectrum ConstantSpectrum::Sample(const SampledWavelengths &) const {
    return SampledSpectrum(c);
}

std::string ConstantSpectrum::ToString() const {
    return StringPrintf("[ ConstantSpectrum %f ]", c);
}

std::string ConstantSpectrum::ParameterType() const {
    LOG_FATAL("Shouldn't be called");
    return {};
}

std::string ConstantSpectrum::ParameterString() const {
    LOG_FATAL("Shouldn't be called");
    return {};
}

SampledSpectrum ScaledSpectrum::Sample(const SampledWavelengths &lambda) const {
    return scale * s.Sample(lambda);
}

std::string ScaledSpectrum::ToString() const {
    return StringPrintf("[ ScaledSpectrum scale: %f s: %s ]", scale, s);
}

std::string ScaledSpectrum::ParameterType() const {
    LOG_FATAL("Shouldn't be called");
    return {};
}

std::string ScaledSpectrum::ParameterString() const {
    LOG_FATAL("Shouldn't be called");
    return {};
}

SampledSpectrum ProductSpectrum::Sample(const SampledWavelengths &lambda) const {
    return s1.Sample(lambda) * s2.Sample(lambda);
}

std::string ProductSpectrum::ToString() const {
    return StringPrintf("[ ProductSpectrum s1: %s s2: %s ]", s1, s2);
}

std::string ProductSpectrum::ParameterType() const {
    LOG_FATAL("Shouldn't be called");
    return {};
}

std::string ProductSpectrum::ParameterString() const {
    LOG_FATAL("Shouldn't be called");
    return {};
}

DenselySampledSpectrum::DenselySampledSpectrum(
      SpectrumHandle s, int lambdaMin, int lambdaMax, Allocator alloc)
    : lambdaMin(lambdaMin), lambdaMax(lambdaMax),
      v(lambdaMax - lambdaMin + 1, alloc) {
    CHECK_GE(lambdaMax, lambdaMin);
    for (int lambda = lambdaMin; lambda <= lambdaMax; ++lambda)
        v[lambda - lambdaMin] = s(lambda + 0.5f);
}

std::string DenselySampledSpectrum::ToString() const {
    std::string s =
        StringPrintf("[ DenselySampledSpectrum lambdaMin: %f lambdaMax: %f "
                     "values: [ ", lambdaMin, lambdaMax);
    for (int i = 0; i < v.size(); ++i)
        s += StringPrintf("%f ", v[i]);
    s += "] ]";
    return s;
}

std::string DenselySampledSpectrum::ParameterType() const {
    LOG_FATAL("Shouldn't be called");
    return {};
}

std::string DenselySampledSpectrum::ParameterString() const {
    LOG_FATAL("Shouldn't be called");
    return {};
}

RGBReflectanceSpectrum::RGBReflectanceSpectrum(const RGBColorSpace &cs, const RGB &rgb)
    : rgb(rgb), rsp(cs.ToRGBCoeffs(rgb)) { }

std::string RGBReflectanceSpectrum::ToString() const {
    return StringPrintf("[ RGBReflectanceSpectrum rsp: %s ]", rsp);
}

std::string RGBReflectanceSpectrum::ParameterType() const {
    return "rgb";
}

std::string RGBReflectanceSpectrum::ParameterString() const {
    return StringPrintf("%f %f %f", rgb.r, rgb.g, rgb.b);
}

RGBSpectrum::RGBSpectrum(const RGBColorSpace &cs, const RGB &rgb)
    : rgb(rgb), illuminant(&cs.illuminant) {
    Float m = std::max({rgb.r, rgb.g, rgb.b});
    scale = m > 0 ? 0.5f / m : 0;
    rsp = cs.ToRGBCoeffs(rgb * scale);
}

std::string RGBSpectrum::ParameterType() const {
    return "rgb";
}

std::string RGBSpectrum::ParameterString() const {
    return StringPrintf("%f %f %f", rgb.r, rgb.g, rgb.b);
}

std::string RGBSpectrum::ToString() const {
    return StringPrintf("[ RGBSpectrum rsp: %s scale: %f illuminant: %s ]",
                        rsp, scale, *illuminant);
}

std::string SampledWavelengths::ToString() const {
    std::string r = "[";
    for (size_t i = 0; i < lambda.size(); ++i)
        r += StringPrintf(" %f%c", lambda[i], i != lambda.size() - 1 ? ',' : ' ');
    r += ']';
    return r;
}

// SampledSpectrumMethod Definitions
Float SampledSpectrum::y(const SampledWavelengths &lambda) const {

    Float ySum = 0;
    SampledSpectrum Ys = SPDs::Y().Sample(lambda);
    for (int i = 0; i < NSpectrumSamples; ++i) {
        if (lambda.pdf[i] == 0) continue;
        ySum += Ys[i] * v[i] / lambda.pdf[i];
    }
    return ySum / (SPDs::CIE_Y_integral * NSpectrumSamples);
}

std::string SampledSpectrum::ToString() const {
    std::string str = "[ ";
    for (int i = 0; i < NSpectrumSamples; ++i) {
        str += StringPrintf("%f", v[i]);
        if (i + 1 < NSpectrumSamples) str += ", ";
    }
    str += " ]";
    return str;
}

XYZ SampledSpectrum::ToXYZ(const SampledWavelengths &lambda) const {
    XYZ xyz;
    SampledSpectrum Xs = SPDs::X().Sample(lambda);
    SampledSpectrum Ys = SPDs::Y().Sample(lambda);
    SampledSpectrum Zs = SPDs::Z().Sample(lambda);
    for (int i = 0; i < NSpectrumSamples; ++i) {
        if (lambda.pdf[i] == 0) continue;
        xyz.X += Xs[i] * v[i] / lambda.pdf[i];
        xyz.Y += Ys[i] * v[i] / lambda.pdf[i];
        xyz.Z += Zs[i] * v[i] / lambda.pdf[i];
    }
    return xyz / (SPDs::CIE_Y_integral * NSpectrumSamples);
}

RGB SampledSpectrum::ToRGB(const SampledWavelengths &lambda,
                           const RGBColorSpace &cs) const {
    XYZ xyz = ToXYZ(lambda);
    return cs.ToRGB(xyz);
}

namespace {

// Spectral Data Declarations
constexpr int nCIESamples = 471;
extern const Float CIE_lambda[nCIESamples];
const Float CIE_lambdaStart = CIE_lambda[0];
const Float CIE_lambdaEnd = CIE_lambda[nCIESamples - 1];

const Float CIE_X[nCIESamples] = {
    // CIE X function values
    0.0001299000,   0.0001458470,   0.0001638021,   0.0001840037,
    0.0002066902,   0.0002321000,   0.0002607280,   0.0002930750,
    0.0003293880,   0.0003699140,   0.0004149000,   0.0004641587,
    0.0005189860,   0.0005818540,   0.0006552347,   0.0007416000,
    0.0008450296,   0.0009645268,   0.001094949,    0.001231154,
    0.001368000,    0.001502050,    0.001642328,    0.001802382,
    0.001995757,    0.002236000,    0.002535385,    0.002892603,
    0.003300829,    0.003753236,    0.004243000,    0.004762389,
    0.005330048,    0.005978712,    0.006741117,    0.007650000,
    0.008751373,    0.01002888,     0.01142170,     0.01286901,
    0.01431000,     0.01570443,     0.01714744,     0.01878122,
    0.02074801,     0.02319000,     0.02620736,     0.02978248,
    0.03388092,     0.03846824,     0.04351000,     0.04899560,
    0.05502260,     0.06171880,     0.06921200,     0.07763000,
    0.08695811,     0.09717672,     0.1084063,      0.1207672,
    0.1343800,      0.1493582,      0.1653957,      0.1819831,
    0.1986110,      0.2147700,      0.2301868,      0.2448797,
    0.2587773,      0.2718079,      0.2839000,      0.2949438,
    0.3048965,      0.3137873,      0.3216454,      0.3285000,
    0.3343513,      0.3392101,      0.3431213,      0.3461296,
    0.3482800,      0.3495999,      0.3501474,      0.3500130,
    0.3492870,      0.3480600,      0.3463733,      0.3442624,
    0.3418088,      0.3390941,      0.3362000,      0.3331977,
    0.3300411,      0.3266357,      0.3228868,      0.3187000,
    0.3140251,      0.3088840,      0.3032904,      0.2972579,
    0.2908000,      0.2839701,      0.2767214,      0.2689178,
    0.2604227,      0.2511000,      0.2408475,      0.2298512,
    0.2184072,      0.2068115,      0.1953600,      0.1842136,
    0.1733273,      0.1626881,      0.1522833,      0.1421000,
    0.1321786,      0.1225696,      0.1132752,      0.1042979,
    0.09564000,     0.08729955,     0.07930804,     0.07171776,
    0.06458099,     0.05795001,     0.05186211,     0.04628152,
    0.04115088,     0.03641283,     0.03201000,     0.02791720,
    0.02414440,     0.02068700,     0.01754040,     0.01470000,
    0.01216179,     0.009919960,    0.007967240,    0.006296346,
    0.004900000,    0.003777173,    0.002945320,    0.002424880,
    0.002236293,    0.002400000,    0.002925520,    0.003836560,
    0.005174840,    0.006982080,    0.009300000,    0.01214949,
    0.01553588,     0.01947752,     0.02399277,     0.02910000,
    0.03481485,     0.04112016,     0.04798504,     0.05537861,
    0.06327000,     0.07163501,     0.08046224,     0.08973996,
    0.09945645,     0.1096000,      0.1201674,      0.1311145,
    0.1423679,      0.1538542,      0.1655000,      0.1772571,
    0.1891400,      0.2011694,      0.2133658,      0.2257499,
    0.2383209,      0.2510668,      0.2639922,      0.2771017,
    0.2904000,      0.3038912,      0.3175726,      0.3314384,
    0.3454828,      0.3597000,      0.3740839,      0.3886396,
    0.4033784,      0.4183115,      0.4334499,      0.4487953,
    0.4643360,      0.4800640,      0.4959713,      0.5120501,
    0.5282959,      0.5446916,      0.5612094,      0.5778215,
    0.5945000,      0.6112209,      0.6279758,      0.6447602,
    0.6615697,      0.6784000,      0.6952392,      0.7120586,
    0.7288284,      0.7455188,      0.7621000,      0.7785432,
    0.7948256,      0.8109264,      0.8268248,      0.8425000,
    0.8579325,      0.8730816,      0.8878944,      0.9023181,
    0.9163000,      0.9297995,      0.9427984,      0.9552776,
    0.9672179,      0.9786000,      0.9893856,      0.9995488,
    1.0090892,      1.0180064,      1.0263000,      1.0339827,
    1.0409860,      1.0471880,      1.0524667,      1.0567000,
    1.0597944,      1.0617992,      1.0628068,      1.0629096,
    1.0622000,      1.0607352,      1.0584436,      1.0552244,
    1.0509768,      1.0456000,      1.0390369,      1.0313608,
    1.0226662,      1.0130477,      1.0026000,      0.9913675,
    0.9793314,      0.9664916,      0.9528479,      0.9384000,
    0.9231940,      0.9072440,      0.8905020,      0.8729200,
    0.8544499,      0.8350840,      0.8149460,      0.7941860,
    0.7729540,      0.7514000,      0.7295836,      0.7075888,
    0.6856022,      0.6638104,      0.6424000,      0.6215149,
    0.6011138,      0.5811052,      0.5613977,      0.5419000,
    0.5225995,      0.5035464,      0.4847436,      0.4661939,
    0.4479000,      0.4298613,      0.4120980,      0.3946440,
    0.3775333,      0.3608000,      0.3444563,      0.3285168,
    0.3130192,      0.2980011,      0.2835000,      0.2695448,
    0.2561184,      0.2431896,      0.2307272,      0.2187000,
    0.2070971,      0.1959232,      0.1851708,      0.1748323,
    0.1649000,      0.1553667,      0.1462300,      0.1374900,
    0.1291467,      0.1212000,      0.1136397,      0.1064650,
    0.09969044,     0.09333061,     0.08740000,     0.08190096,
    0.07680428,     0.07207712,     0.06768664,     0.06360000,
    0.05980685,     0.05628216,     0.05297104,     0.04981861,
    0.04677000,     0.04378405,     0.04087536,     0.03807264,
    0.03540461,     0.03290000,     0.03056419,     0.02838056,
    0.02634484,     0.02445275,     0.02270000,     0.02108429,
    0.01959988,     0.01823732,     0.01698717,     0.01584000,
    0.01479064,     0.01383132,     0.01294868,     0.01212920,
    0.01135916,     0.01062935,     0.009938846,    0.009288422,
    0.008678854,    0.008110916,    0.007582388,    0.007088746,
    0.006627313,    0.006195408,    0.005790346,    0.005409826,
    0.005052583,    0.004717512,    0.004403507,    0.004109457,
    0.003833913,    0.003575748,    0.003334342,    0.003109075,
    0.002899327,    0.002704348,    0.002523020,    0.002354168,
    0.002196616,    0.002049190,    0.001910960,    0.001781438,
    0.001660110,    0.001546459,    0.001439971,    0.001340042,
    0.001246275,    0.001158471,    0.001076430,    0.0009999493,
    0.0009287358,   0.0008624332,   0.0008007503,   0.0007433960,
    0.0006900786,   0.0006405156,   0.0005945021,   0.0005518646,
    0.0005124290,   0.0004760213,   0.0004424536,   0.0004115117,
    0.0003829814,   0.0003566491,   0.0003323011,   0.0003097586,
    0.0002888871,   0.0002695394,   0.0002515682,   0.0002348261,
    0.0002191710,   0.0002045258,   0.0001908405,   0.0001780654,
    0.0001661505,   0.0001550236,   0.0001446219,   0.0001349098,
    0.0001258520,   0.0001174130,   0.0001095515,   0.0001022245,
    0.00009539445,  0.00008902390,  0.00008307527,  0.00007751269,
    0.00007231304,  0.00006745778,  0.00006292844,  0.00005870652,
    0.00005477028,  0.00005109918,  0.00004767654,  0.00004448567,
    0.00004150994,  0.00003873324,  0.00003614203,  0.00003372352,
    0.00003146487,  0.00002935326,  0.00002737573,  0.00002552433,
    0.00002379376,  0.00002217870,  0.00002067383,  0.00001927226,
    0.00001796640,  0.00001674991,  0.00001561648,  0.00001455977,
    0.00001357387,  0.00001265436,  0.00001179723,  0.00001099844,
    0.00001025398,  0.000009559646, 0.000008912044, 0.000008308358,
    0.000007745769, 0.000007221456, 0.000006732475, 0.000006276423,
    0.000005851304, 0.000005455118, 0.000005085868, 0.000004741466,
    0.000004420236, 0.000004120783, 0.000003841716, 0.000003581652,
    0.000003339127, 0.000003112949, 0.000002902121, 0.000002705645,
    0.000002522525, 0.000002351726, 0.000002192415, 0.000002043902,
    0.000001905497, 0.000001776509, 0.000001656215, 0.000001544022,
    0.000001439440, 0.000001341977, 0.000001251141};

const Float CIE_Y[nCIESamples] = {
    // CIE Y function values
    0.000003917000,  0.000004393581,  0.000004929604,  0.000005532136,
    0.000006208245,  0.000006965000,  0.000007813219,  0.000008767336,
    0.000009839844,  0.00001104323,   0.00001239000,   0.00001388641,
    0.00001555728,   0.00001744296,   0.00001958375,   0.00002202000,
    0.00002483965,   0.00002804126,   0.00003153104,   0.00003521521,
    0.00003900000,   0.00004282640,   0.00004691460,   0.00005158960,
    0.00005717640,   0.00006400000,   0.00007234421,   0.00008221224,
    0.00009350816,   0.0001061361,    0.0001200000,    0.0001349840,
    0.0001514920,    0.0001702080,    0.0001918160,    0.0002170000,
    0.0002469067,    0.0002812400,    0.0003185200,    0.0003572667,
    0.0003960000,    0.0004337147,    0.0004730240,    0.0005178760,
    0.0005722187,    0.0006400000,    0.0007245600,    0.0008255000,
    0.0009411600,    0.001069880,     0.001210000,     0.001362091,
    0.001530752,     0.001720368,     0.001935323,     0.002180000,
    0.002454800,     0.002764000,     0.003117800,     0.003526400,
    0.004000000,     0.004546240,     0.005159320,     0.005829280,
    0.006546160,     0.007300000,     0.008086507,     0.008908720,
    0.009767680,     0.01066443,      0.01160000,      0.01257317,
    0.01358272,      0.01462968,      0.01571509,      0.01684000,
    0.01800736,      0.01921448,      0.02045392,      0.02171824,
    0.02300000,      0.02429461,      0.02561024,      0.02695857,
    0.02835125,      0.02980000,      0.03131083,      0.03288368,
    0.03452112,      0.03622571,      0.03800000,      0.03984667,
    0.04176800,      0.04376600,      0.04584267,      0.04800000,
    0.05024368,      0.05257304,      0.05498056,      0.05745872,
    0.06000000,      0.06260197,      0.06527752,      0.06804208,
    0.07091109,      0.07390000,      0.07701600,      0.08026640,
    0.08366680,      0.08723280,      0.09098000,      0.09491755,
    0.09904584,      0.1033674,       0.1078846,       0.1126000,
    0.1175320,       0.1226744,       0.1279928,       0.1334528,
    0.1390200,       0.1446764,       0.1504693,       0.1564619,
    0.1627177,       0.1693000,       0.1762431,       0.1835581,
    0.1912735,       0.1994180,       0.2080200,       0.2171199,
    0.2267345,       0.2368571,       0.2474812,       0.2586000,
    0.2701849,       0.2822939,       0.2950505,       0.3085780,
    0.3230000,       0.3384021,       0.3546858,       0.3716986,
    0.3892875,       0.4073000,       0.4256299,       0.4443096,
    0.4633944,       0.4829395,       0.5030000,       0.5235693,
    0.5445120,       0.5656900,       0.5869653,       0.6082000,
    0.6293456,       0.6503068,       0.6708752,       0.6908424,
    0.7100000,       0.7281852,       0.7454636,       0.7619694,
    0.7778368,       0.7932000,       0.8081104,       0.8224962,
    0.8363068,       0.8494916,       0.8620000,       0.8738108,
    0.8849624,       0.8954936,       0.9054432,       0.9148501,
    0.9237348,       0.9320924,       0.9399226,       0.9472252,
    0.9540000,       0.9602561,       0.9660074,       0.9712606,
    0.9760225,       0.9803000,       0.9840924,       0.9874812,
    0.9903128,       0.9928116,       0.9949501,       0.9967108,
    0.9980983,       0.9991120,       0.9997482,       1.0000000,
    0.9998567,       0.9993046,       0.9983255,       0.9968987,
    0.9950000,       0.9926005,       0.9897426,       0.9864444,
    0.9827241,       0.9786000,       0.9740837,       0.9691712,
    0.9638568,       0.9581349,       0.9520000,       0.9454504,
    0.9384992,       0.9311628,       0.9234576,       0.9154000,
    0.9070064,       0.8982772,       0.8892048,       0.8797816,
    0.8700000,       0.8598613,       0.8493920,       0.8386220,
    0.8275813,       0.8163000,       0.8047947,       0.7930820,
    0.7811920,       0.7691547,       0.7570000,       0.7447541,
    0.7324224,       0.7200036,       0.7074965,       0.6949000,
    0.6822192,       0.6694716,       0.6566744,       0.6438448,
    0.6310000,       0.6181555,       0.6053144,       0.5924756,
    0.5796379,       0.5668000,       0.5539611,       0.5411372,
    0.5283528,       0.5156323,       0.5030000,       0.4904688,
    0.4780304,       0.4656776,       0.4534032,       0.4412000,
    0.4290800,       0.4170360,       0.4050320,       0.3930320,
    0.3810000,       0.3689184,       0.3568272,       0.3447768,
    0.3328176,       0.3210000,       0.3093381,       0.2978504,
    0.2865936,       0.2756245,       0.2650000,       0.2547632,
    0.2448896,       0.2353344,       0.2260528,       0.2170000,
    0.2081616,       0.1995488,       0.1911552,       0.1829744,
    0.1750000,       0.1672235,       0.1596464,       0.1522776,
    0.1451259,       0.1382000,       0.1315003,       0.1250248,
    0.1187792,       0.1127691,       0.1070000,       0.1014762,
    0.09618864,      0.09112296,      0.08626485,      0.08160000,
    0.07712064,      0.07282552,      0.06871008,      0.06476976,
    0.06100000,      0.05739621,      0.05395504,      0.05067376,
    0.04754965,      0.04458000,      0.04175872,      0.03908496,
    0.03656384,      0.03420048,      0.03200000,      0.02996261,
    0.02807664,      0.02632936,      0.02470805,      0.02320000,
    0.02180077,      0.02050112,      0.01928108,      0.01812069,
    0.01700000,      0.01590379,      0.01483718,      0.01381068,
    0.01283478,      0.01192000,      0.01106831,      0.01027339,
    0.009533311,     0.008846157,     0.008210000,     0.007623781,
    0.007085424,     0.006591476,     0.006138485,     0.005723000,
    0.005343059,     0.004995796,     0.004676404,     0.004380075,
    0.004102000,     0.003838453,     0.003589099,     0.003354219,
    0.003134093,     0.002929000,     0.002738139,     0.002559876,
    0.002393244,     0.002237275,     0.002091000,     0.001953587,
    0.001824580,     0.001703580,     0.001590187,     0.001484000,
    0.001384496,     0.001291268,     0.001204092,     0.001122744,
    0.001047000,     0.0009765896,    0.0009111088,    0.0008501332,
    0.0007932384,    0.0007400000,    0.0006900827,    0.0006433100,
    0.0005994960,    0.0005584547,    0.0005200000,    0.0004839136,
    0.0004500528,    0.0004183452,    0.0003887184,    0.0003611000,
    0.0003353835,    0.0003114404,    0.0002891656,    0.0002684539,
    0.0002492000,    0.0002313019,    0.0002146856,    0.0001992884,
    0.0001850475,    0.0001719000,    0.0001597781,    0.0001486044,
    0.0001383016,    0.0001287925,    0.0001200000,    0.0001118595,
    0.0001043224,    0.00009733560,   0.00009084587,   0.00008480000,
    0.00007914667,   0.00007385800,   0.00006891600,   0.00006430267,
    0.00006000000,   0.00005598187,   0.00005222560,   0.00004871840,
    0.00004544747,   0.00004240000,   0.00003956104,   0.00003691512,
    0.00003444868,   0.00003214816,   0.00003000000,   0.00002799125,
    0.00002611356,   0.00002436024,   0.00002272461,   0.00002120000,
    0.00001977855,   0.00001845285,   0.00001721687,   0.00001606459,
    0.00001499000,   0.00001398728,   0.00001305155,   0.00001217818,
    0.00001136254,   0.00001060000,   0.000009885877,  0.000009217304,
    0.000008592362,  0.000008009133,  0.000007465700,  0.000006959567,
    0.000006487995,  0.000006048699,  0.000005639396,  0.000005257800,
    0.000004901771,  0.000004569720,  0.000004260194,  0.000003971739,
    0.000003702900,  0.000003452163,  0.000003218302,  0.000003000300,
    0.000002797139,  0.000002607800,  0.000002431220,  0.000002266531,
    0.000002113013,  0.000001969943,  0.000001836600,  0.000001712230,
    0.000001596228,  0.000001488090,  0.000001387314,  0.000001293400,
    0.000001205820,  0.000001124143,  0.000001048009,  0.0000009770578,
    0.0000009109300, 0.0000008492513, 0.0000007917212, 0.0000007380904,
    0.0000006881098, 0.0000006415300, 0.0000005980895, 0.0000005575746,
    0.0000005198080, 0.0000004846123, 0.0000004518100};

const Float CIE_Z[nCIESamples] = {
    // CIE Z function values
    0.0006061000,   0.0006808792,   0.0007651456,   0.0008600124,
    0.0009665928,   0.001086000,    0.001220586,    0.001372729,
    0.001543579,    0.001734286,    0.001946000,    0.002177777,
    0.002435809,    0.002731953,    0.003078064,    0.003486000,
    0.003975227,    0.004540880,    0.005158320,    0.005802907,
    0.006450001,    0.007083216,    0.007745488,    0.008501152,
    0.009414544,    0.01054999,     0.01196580,     0.01365587,
    0.01558805,     0.01773015,     0.02005001,     0.02251136,
    0.02520288,     0.02827972,     0.03189704,     0.03621000,
    0.04143771,     0.04750372,     0.05411988,     0.06099803,
    0.06785001,     0.07448632,     0.08136156,     0.08915364,
    0.09854048,     0.1102000,      0.1246133,      0.1417017,
    0.1613035,      0.1832568,      0.2074000,      0.2336921,
    0.2626114,      0.2947746,      0.3307985,      0.3713000,
    0.4162091,      0.4654642,      0.5196948,      0.5795303,
    0.6456000,      0.7184838,      0.7967133,      0.8778459,
    0.9594390,      1.0390501,      1.1153673,      1.1884971,
    1.2581233,      1.3239296,      1.3856000,      1.4426352,
    1.4948035,      1.5421903,      1.5848807,      1.6229600,
    1.6564048,      1.6852959,      1.7098745,      1.7303821,
    1.7470600,      1.7600446,      1.7696233,      1.7762637,
    1.7804334,      1.7826000,      1.7829682,      1.7816998,
    1.7791982,      1.7758671,      1.7721100,      1.7682589,
    1.7640390,      1.7589438,      1.7524663,      1.7441000,
    1.7335595,      1.7208581,      1.7059369,      1.6887372,
    1.6692000,      1.6475287,      1.6234127,      1.5960223,
    1.5645280,      1.5281000,      1.4861114,      1.4395215,
    1.3898799,      1.3387362,      1.2876400,      1.2374223,
    1.1878243,      1.1387611,      1.0901480,      1.0419000,
    0.9941976,      0.9473473,      0.9014531,      0.8566193,
    0.8129501,      0.7705173,      0.7294448,      0.6899136,
    0.6521049,      0.6162000,      0.5823286,      0.5504162,
    0.5203376,      0.4919673,      0.4651800,      0.4399246,
    0.4161836,      0.3938822,      0.3729459,      0.3533000,
    0.3348578,      0.3175521,      0.3013375,      0.2861686,
    0.2720000,      0.2588171,      0.2464838,      0.2347718,
    0.2234533,      0.2123000,      0.2011692,      0.1901196,
    0.1792254,      0.1685608,      0.1582000,      0.1481383,
    0.1383758,      0.1289942,      0.1200751,      0.1117000,
    0.1039048,      0.09666748,     0.08998272,     0.08384531,
    0.07824999,     0.07320899,     0.06867816,     0.06456784,
    0.06078835,     0.05725001,     0.05390435,     0.05074664,
    0.04775276,     0.04489859,     0.04216000,     0.03950728,
    0.03693564,     0.03445836,     0.03208872,     0.02984000,
    0.02771181,     0.02569444,     0.02378716,     0.02198925,
    0.02030000,     0.01871805,     0.01724036,     0.01586364,
    0.01458461,     0.01340000,     0.01230723,     0.01130188,
    0.01037792,     0.009529306,    0.008749999,    0.008035200,
    0.007381600,    0.006785400,    0.006242800,    0.005749999,
    0.005303600,    0.004899800,    0.004534200,    0.004202400,
    0.003900000,    0.003623200,    0.003370600,    0.003141400,
    0.002934800,    0.002749999,    0.002585200,    0.002438600,
    0.002309400,    0.002196800,    0.002100000,    0.002017733,
    0.001948200,    0.001889800,    0.001840933,    0.001800000,
    0.001766267,    0.001737800,    0.001711200,    0.001683067,
    0.001650001,    0.001610133,    0.001564400,    0.001513600,
    0.001458533,    0.001400000,    0.001336667,    0.001270000,
    0.001205000,    0.001146667,    0.001100000,    0.001068800,
    0.001049400,    0.001035600,    0.001021200,    0.001000000,
    0.0009686400,   0.0009299200,   0.0008868800,   0.0008425600,
    0.0008000000,   0.0007609600,   0.0007236800,   0.0006859200,
    0.0006454400,   0.0006000000,   0.0005478667,   0.0004916000,
    0.0004354000,   0.0003834667,   0.0003400000,   0.0003072533,
    0.0002831600,   0.0002654400,   0.0002518133,   0.0002400000,
    0.0002295467,   0.0002206400,   0.0002119600,   0.0002021867,
    0.0001900000,   0.0001742133,   0.0001556400,   0.0001359600,
    0.0001168533,   0.0001000000,   0.00008613333,  0.00007460000,
    0.00006500000,  0.00005693333,  0.00004999999,  0.00004416000,
    0.00003948000,  0.00003572000,  0.00003264000,  0.00003000000,
    0.00002765333,  0.00002556000,  0.00002364000,  0.00002181333,
    0.00002000000,  0.00001813333,  0.00001620000,  0.00001420000,
    0.00001213333,  0.00001000000,  0.000007733333, 0.000005400000,
    0.000003200000, 0.000001333333, 0.000000000000, 0.000000000000,
    0.000000000000, 0.000000000000, 0.000000000000, 0.000000000000,
    0.000000000000, 0.000000000000, 0.000000000000, 0.000000000000,
    0.000000000000, 0.000000000000, 0.000000000000, 0.000000000000,
    0.000000000000, 0.000000000000, 0.000000000000, 0.000000000000,
    0.000000000000, 0.000000000000, 0.000000000000, 0.000000000000,
    0.000000000000, 0.000000000000, 0.000000000000, 0.000000000000,
    0.000000000000, 0.000000000000, 0.000000000000, 0.000000000000,
    0.000000000000, 0.000000000000, 0.000000000000, 0.000000000000,
    0.000000000000, 0.000000000000, 0.000000000000, 0.000000000000,
    0.000000000000, 0.000000000000, 0.000000000000, 0.000000000000,
    0.000000000000, 0.000000000000, 0.000000000000, 0.000000000000,
    0.000000000000, 0.000000000000, 0.000000000000, 0.000000000000,
    0.000000000000, 0.000000000000, 0.000000000000, 0.000000000000,
    0.000000000000, 0.000000000000, 0.000000000000, 0.000000000000,
    0.000000000000, 0.000000000000, 0.000000000000, 0.000000000000,
    0.000000000000, 0.000000000000, 0.000000000000, 0.000000000000,
    0.000000000000, 0.000000000000, 0.000000000000, 0.000000000000,
    0.000000000000, 0.000000000000, 0.000000000000, 0.000000000000,
    0.000000000000, 0.000000000000, 0.000000000000, 0.000000000000,
    0.000000000000, 0.000000000000, 0.000000000000, 0.000000000000,
    0.000000000000, 0.000000000000, 0.000000000000, 0.000000000000,
    0.000000000000, 0.000000000000, 0.000000000000, 0.000000000000,
    0.000000000000, 0.000000000000, 0.000000000000, 0.000000000000,
    0.000000000000, 0.000000000000, 0.000000000000, 0.000000000000,
    0.000000000000, 0.000000000000, 0.000000000000, 0.000000000000,
    0.000000000000, 0.000000000000, 0.000000000000, 0.000000000000,
    0.000000000000, 0.000000000000, 0.000000000000, 0.000000000000,
    0.000000000000, 0.000000000000, 0.000000000000, 0.000000000000,
    0.000000000000, 0.000000000000, 0.000000000000, 0.000000000000,
    0.000000000000, 0.000000000000, 0.000000000000, 0.000000000000,
    0.000000000000, 0.000000000000, 0.000000000000, 0.000000000000,
    0.000000000000, 0.000000000000, 0.000000000000, 0.000000000000,
    0.000000000000, 0.000000000000, 0.000000000000, 0.000000000000,
    0.000000000000, 0.000000000000, 0.000000000000, 0.000000000000,
    0.000000000000, 0.000000000000, 0.000000000000, 0.000000000000,
    0.000000000000, 0.000000000000, 0.000000000000, 0.000000000000,
    0.000000000000, 0.000000000000, 0.000000000000, 0.000000000000,
    0.000000000000, 0.000000000000, 0.000000000000, 0.000000000000,
    0.000000000000, 0.000000000000, 0.000000000000, 0.000000000000,
    0.000000000000, 0.000000000000, 0.000000000000, 0.000000000000,
    0.000000000000, 0.000000000000, 0.000000000000, 0.000000000000,
    0.000000000000, 0.000000000000, 0.000000000000, 0.000000000000,
    0.000000000000, 0.000000000000, 0.000000000000, 0.000000000000,
    0.000000000000, 0.000000000000, 0.000000000000, 0.000000000000,
    0.000000000000, 0.000000000000, 0.000000000000};

const Float CIE_lambda[nCIESamples] = {
    360, 361, 362, 363, 364, 365, 366, 367, 368, 369, 370, 371, 372, 373, 374,
    375, 376, 377, 378, 379, 380, 381, 382, 383, 384, 385, 386, 387, 388, 389,
    390, 391, 392, 393, 394, 395, 396, 397, 398, 399, 400, 401, 402, 403, 404,
    405, 406, 407, 408, 409, 410, 411, 412, 413, 414, 415, 416, 417, 418, 419,
    420, 421, 422, 423, 424, 425, 426, 427, 428, 429, 430, 431, 432, 433, 434,
    435, 436, 437, 438, 439, 440, 441, 442, 443, 444, 445, 446, 447, 448, 449,
    450, 451, 452, 453, 454, 455, 456, 457, 458, 459, 460, 461, 462, 463, 464,
    465, 466, 467, 468, 469, 470, 471, 472, 473, 474, 475, 476, 477, 478, 479,
    480, 481, 482, 483, 484, 485, 486, 487, 488, 489, 490, 491, 492, 493, 494,
    495, 496, 497, 498, 499, 500, 501, 502, 503, 504, 505, 506, 507, 508, 509,
    510, 511, 512, 513, 514, 515, 516, 517, 518, 519, 520, 521, 522, 523, 524,
    525, 526, 527, 528, 529, 530, 531, 532, 533, 534, 535, 536, 537, 538, 539,
    540, 541, 542, 543, 544, 545, 546, 547, 548, 549, 550, 551, 552, 553, 554,
    555, 556, 557, 558, 559, 560, 561, 562, 563, 564, 565, 566, 567, 568, 569,
    570, 571, 572, 573, 574, 575, 576, 577, 578, 579, 580, 581, 582, 583, 584,
    585, 586, 587, 588, 589, 590, 591, 592, 593, 594, 595, 596, 597, 598, 599,
    600, 601, 602, 603, 604, 605, 606, 607, 608, 609, 610, 611, 612, 613, 614,
    615, 616, 617, 618, 619, 620, 621, 622, 623, 624, 625, 626, 627, 628, 629,
    630, 631, 632, 633, 634, 635, 636, 637, 638, 639, 640, 641, 642, 643, 644,
    645, 646, 647, 648, 649, 650, 651, 652, 653, 654, 655, 656, 657, 658, 659,
    660, 661, 662, 663, 664, 665, 666, 667, 668, 669, 670, 671, 672, 673, 674,
    675, 676, 677, 678, 679, 680, 681, 682, 683, 684, 685, 686, 687, 688, 689,
    690, 691, 692, 693, 694, 695, 696, 697, 698, 699, 700, 701, 702, 703, 704,
    705, 706, 707, 708, 709, 710, 711, 712, 713, 714, 715, 716, 717, 718, 719,
    720, 721, 722, 723, 724, 725, 726, 727, 728, 729, 730, 731, 732, 733, 734,
    735, 736, 737, 738, 739, 740, 741, 742, 743, 744, 745, 746, 747, 748, 749,
    750, 751, 752, 753, 754, 755, 756, 757, 758, 759, 760, 761, 762, 763, 764,
    765, 766, 767, 768, 769, 770, 771, 772, 773, 774, 775, 776, 777, 778, 779,
    780, 781, 782, 783, 784, 785, 786, 787, 788, 789, 790, 791, 792, 793, 794,
    795, 796, 797, 798, 799, 800, 801, 802, 803, 804, 805, 806, 807, 808, 809,
    810, 811, 812, 813, 814, 815, 816, 817, 818, 819, 820, 821, 822, 823, 824,
    825, 826, 827, 828, 829, 830};

const Float CIE_Illum_A[] = {
    300.000000, 0.930483,   305.000000, 1.128210,   310.000000, 1.357690,
    315.000000, 1.622190,   320.000000, 1.925080,   325.000000, 2.269800,
    330.000000, 2.659810,   335.000000, 3.098610,   340.000000, 3.589680,
    345.000000, 4.136480,   350.000000, 4.742380,   355.000000, 5.410700,
    360.000000, 6.144620,   365.000000, 6.947200,   370.000000, 7.821350,
    375.000000, 8.769800,   380.000000, 9.795100,   385.000000, 10.899600,
    390.000000, 12.085300,  395.000000, 13.354300,  400.000000, 14.708000,
    405.000000, 16.148001,  410.000000, 17.675301,  415.000000, 19.290701,
    420.000000, 20.995001,  425.000000, 22.788300,  430.000000, 24.670900,
    435.000000, 26.642500,  440.000000, 28.702700,  445.000000, 30.850800,
    450.000000, 33.085899,  455.000000, 35.406799,  460.000000, 37.812099,
    465.000000, 40.300201,  470.000000, 42.869301,  475.000000, 45.517399,
    480.000000, 48.242298,  485.000000, 51.041801,  490.000000, 53.913200,
    495.000000, 56.853901,  500.000000, 59.861099,  505.000000, 62.931999,
    510.000000, 66.063499,  515.000000, 69.252502,  520.000000, 72.495903,
    525.000000, 75.790298,  530.000000, 79.132599,  535.000000, 82.519302,
    540.000000, 85.946999,  545.000000, 89.412399,  550.000000, 92.912003,
    555.000000, 96.442299,  560.000000, 100.000000, 565.000000, 103.582001,
    570.000000, 107.183998, 575.000000, 110.803001, 580.000000, 114.435997,
    585.000000, 118.080002, 590.000000, 121.731003, 595.000000, 125.386002,
    600.000000, 129.042999, 605.000000, 132.697006, 610.000000, 136.345993,
    615.000000, 139.988007, 620.000000, 143.617996, 625.000000, 147.235001,
    630.000000, 150.835999, 635.000000, 154.417999, 640.000000, 157.979004,
    645.000000, 161.516006, 650.000000, 165.028000, 655.000000, 168.509995,
    660.000000, 171.962997, 665.000000, 175.382996, 670.000000, 178.768997,
    675.000000, 182.117996, 680.000000, 185.429001, 685.000000, 188.701004,
    690.000000, 191.931000, 695.000000, 195.117996, 700.000000, 198.261002,
    705.000000, 201.358994, 710.000000, 204.408997, 715.000000, 207.410995,
    720.000000, 210.365005, 725.000000, 213.268005, 730.000000, 216.119995,
    735.000000, 218.919998, 740.000000, 221.667007, 745.000000, 224.360992,
    750.000000, 227.000000, 755.000000, 229.585007, 760.000000, 232.115005,
    765.000000, 234.589005, 770.000000, 237.007996, 775.000000, 239.369995,
    780.000000, 241.675003, 785.000000, 243.923996, 790.000000, 246.115997,
    795.000000, 248.251007, 800.000000, 250.328995, 805.000000, 252.350006,
    810.000000, 254.313995, 815.000000, 256.221008, 820.000000, 258.071014,
    825.000000, 259.864990, 830.000000, 261.601990,
};

const Float CIE_Illum_D5000[] = {
    300.000000, 0.019200,   305.000000, 1.036600,   310.000000, 2.054000,
    315.000000, 4.913000,   320.000000, 7.772000,   325.000000, 11.255700,
    330.000000, 14.739500,  335.000000, 16.339001,  340.000000, 17.938601,
    345.000000, 19.466700,  350.000000, 20.994900,  355.000000, 22.459999,
    360.000000, 23.925100,  365.000000, 25.433901,  370.000000, 26.942699,
    375.000000, 25.701799,  380.000000, 24.461000,  385.000000, 27.150700,
    390.000000, 29.840401,  395.000000, 39.550301,  400.000000, 49.664001,
    405.000000, 53.155998,  410.000000, 56.647999,  415.000000, 58.445999,
    420.000000, 60.243999,  425.000000, 59.230000,  430.000000, 58.216000,
    435.000000, 66.973999,  440.000000, 75.732002,  445.000000, 81.998001,
    450.000000, 88.264000,  455.000000, 89.930000,  460.000000, 91.596001,
    465.000000, 91.940002,  470.000000, 92.283997,  475.000000, 94.155998,
    480.000000, 96.028000,  485.000000, 94.311996,  490.000000, 92.596001,
    495.000000, 94.424004,  500.000000, 96.251999,  505.000000, 96.662003,
    510.000000, 97.071999,  515.000000, 97.314003,  520.000000, 97.556000,
    525.000000, 100.005997, 530.000000, 102.456001, 535.000000, 101.694000,
    540.000000, 100.931999, 545.000000, 101.678001, 550.000000, 102.424004,
    555.000000, 101.211998, 560.000000, 100.000000, 565.000000, 98.036697,
    570.000000, 96.073402,  575.000000, 95.678398,  580.000000, 95.283501,
    585.000000, 92.577103,  590.000000, 89.870697,  595.000000, 90.772499,
    600.000000, 91.674400,  605.000000, 91.739502,  610.000000, 91.804703,
    615.000000, 90.964798,  620.000000, 90.124901,  625.000000, 87.998299,
    630.000000, 85.871696,  635.000000, 86.715302,  640.000000, 87.558899,
    645.000000, 86.069000,  650.000000, 84.579102,  655.000000, 85.167603,
    660.000000, 85.756203,  665.000000, 87.126404,  670.000000, 88.496597,
    675.000000, 86.769997,  680.000000, 85.043404,  685.000000, 79.994698,
    690.000000, 74.946098,  695.000000, 76.384598,  700.000000, 77.823196,
    705.000000, 78.671303,  710.000000, 79.519501,  715.000000, 72.694199,
    720.000000, 65.869003,  725.000000, 70.179100,  730.000000, 74.489197,
    735.000000, 77.212601,  740.000000, 79.935997,  745.000000, 73.797401,
    750.000000, 67.658897,  755.000000, 58.633598,  760.000000, 49.608398,
    765.000000, 60.462101,  770.000000, 71.315804,  775.000000, 69.405701,
    780.000000, 67.495598,  785.000000, 68.032303,  790.000000, 68.569000,
    795.000000, 65.958900,  800.000000, 63.348801,  805.000000, 59.333599,
    810.000000, 55.318501,  815.000000, 58.228600,  820.000000, 61.138699,
    825.000000, 62.712101,  830.000000, 64.285500,
};

// Via https://gist.github.com/aforsythe/4df4e5377853df76a5a83a3c001c7eeb
// with the critial bugfix:
// <    cct = 6000
// --
// >    cct = 6000.
const Float ACES_Illum_D60[] = {
    300, 0.02928, 305, 1.28964, 310, 2.55, 315, 9.0338, 320, 15.5176,
    325, 21.94705, 330, 28.3765, 335, 29.93335, 340, 31.4902, 345, 33.75765,
    350, 36.0251, 355, 37.2032, 360, 38.3813, 365, 40.6445, 370, 42.9077,
    375, 42.05735, 380, 41.207, 385, 43.8121, 390, 46.4172, 395, 59.26285,
    400, 72.1085, 405, 76.1756, 410, 80.2427, 415, 81.4878, 420, 82.7329,
    425, 80.13505, 430, 77.5372, 435, 86.5577, 440, 95.5782, 445, 101.72045,
    450, 107.8627, 455, 108.67115, 460, 109.4796, 465, 108.5873, 470, 107.695,
    475, 108.6598, 480, 109.6246, 485, 106.6426, 490, 103.6606, 495, 104.42795,
    500, 105.1953, 505, 104.7974, 510, 104.3995, 515, 103.45635, 520, 102.5132,
    525, 104.2813, 530, 106.0494, 535, 104.67885, 540, 103.3083, 545, 103.4228,
    550, 103.5373, 555, 101.76865, 560, 100.0, 565, 98.3769, 570, 96.7538,
    575, 96.73515, 580, 96.7165, 585, 93.3013, 590, 89.8861, 595, 90.91705,
    600, 91.948, 605, 91.98965, 610, 92.0313, 615, 91.3008, 620, 90.5703,
    625, 88.5077, 630, 86.4451, 635, 86.9551, 640, 87.4651, 645, 85.6558,
    650, 83.8465, 655, 84.20755, 660, 84.5686, 665, 85.9432, 670, 87.3178,
    675, 85.3068, 680, 83.2958, 685, 78.66005, 690, 74.0243, 695, 75.23535,
    700, 76.4464, 705, 77.67465, 710, 78.9029, 715, 72.12575, 720, 65.3486,
    725, 69.6609, 730, 73.9732, 735, 76.6802, 740, 79.3872, 745, 73.28855,
    750, 67.1899, 755, 58.18595, 760, 49.182, 765, 59.9723, 770, 70.7626,
    775, 68.9039, 780, 67.0452, 785, 67.5469, 790, 68.0486, 795, 65.4631,
    800, 62.8776, 805, 58.88595, 810, 54.8943, 815, 57.8066, 820, 60.7189,
    825, 62.2491, 830, 63.7793,
};

const Float CIE_Illum_D6500[] = {
    300.000000, 0.034100,   305.000000, 1.664300,   310.000000, 3.294500,
    315.000000, 11.765200,  320.000000, 20.236000,  325.000000, 28.644699,
    330.000000, 37.053501,  335.000000, 38.501099,  340.000000, 39.948799,
    345.000000, 42.430199,  350.000000, 44.911701,  355.000000, 45.775002,
    360.000000, 46.638302,  365.000000, 49.363701,  370.000000, 52.089100,
    375.000000, 51.032299,  380.000000, 49.975498,  385.000000, 52.311798,
    390.000000, 54.648201,  395.000000, 68.701500,  400.000000, 82.754898,
    405.000000, 87.120399,  410.000000, 91.486000,  415.000000, 92.458900,
    420.000000, 93.431801,  425.000000, 90.056999,  430.000000, 86.682297,
    435.000000, 95.773598,  440.000000, 104.864998, 445.000000, 110.935997,
    450.000000, 117.008003, 455.000000, 117.410004, 460.000000, 117.811996,
    465.000000, 116.335999, 470.000000, 114.861000, 475.000000, 115.391998,
    480.000000, 115.922997, 485.000000, 112.366997, 490.000000, 108.810997,
    495.000000, 109.082001, 500.000000, 109.353996, 505.000000, 108.578003,
    510.000000, 107.802002, 515.000000, 106.295998, 520.000000, 104.790001,
    525.000000, 106.238998, 530.000000, 107.689003, 535.000000, 106.046997,
    540.000000, 104.404999, 545.000000, 104.224998, 550.000000, 104.045998,
    555.000000, 102.023003, 560.000000, 100.000000, 565.000000, 98.167099,
    570.000000, 96.334198,  575.000000, 96.061096,  580.000000, 95.788002,
    585.000000, 92.236801,  590.000000, 88.685600,  595.000000, 89.345901,
    600.000000, 90.006203,  605.000000, 89.802597,  610.000000, 89.599098,
    615.000000, 88.648903,  620.000000, 87.698700,  625.000000, 85.493599,
    630.000000, 83.288597,  635.000000, 83.493896,  640.000000, 83.699203,
    645.000000, 81.862999,  650.000000, 80.026802,  655.000000, 80.120697,
    660.000000, 80.214600,  665.000000, 81.246201,  670.000000, 82.277802,
    675.000000, 80.280998,  680.000000, 78.284203,  685.000000, 74.002701,
    690.000000, 69.721298,  695.000000, 70.665199,  700.000000, 71.609100,
    705.000000, 72.978996,  710.000000, 74.348999,  715.000000, 67.976501,
    720.000000, 61.604000,  725.000000, 65.744797,  730.000000, 69.885597,
    735.000000, 72.486298,  740.000000, 75.086998,  745.000000, 69.339798,
    750.000000, 63.592701,  755.000000, 55.005402,  760.000000, 46.418201,
    765.000000, 56.611801,  770.000000, 66.805397,  775.000000, 65.094101,
    780.000000, 63.382801,  785.000000, 63.843399,  790.000000, 64.304001,
    795.000000, 61.877899,  800.000000, 59.451900,  805.000000, 55.705399,
    810.000000, 51.959000,  815.000000, 54.699799,  820.000000, 57.440601,
    825.000000, 58.876499,  830.000000, 60.312500,
};

const Float CIE_Illum_F1[] = {
    380.000000, 1.870000,  385.000000, 2.360000,  390.000000, 2.940000,
    395.000000, 3.470000,  400.000000, 5.170000,  405.000000, 19.490000,
    410.000000, 6.130000,  415.000000, 6.240000,  420.000000, 7.010000,
    425.000000, 7.790000,  430.000000, 8.560000,  435.000000, 43.669998,
    440.000000, 16.940001, 445.000000, 10.720000, 450.000000, 11.350000,
    455.000000, 11.890000, 460.000000, 12.370000, 465.000000, 12.750000,
    470.000000, 13.000000, 475.000000, 13.150000, 480.000000, 13.230000,
    485.000000, 13.170000, 490.000000, 13.130000, 495.000000, 12.850000,
    500.000000, 12.520000, 505.000000, 12.200000, 510.000000, 11.830000,
    515.000000, 11.500000, 520.000000, 11.220000, 525.000000, 11.050000,
    530.000000, 11.030000, 535.000000, 11.180000, 540.000000, 11.530000,
    545.000000, 27.740000, 550.000000, 17.049999, 555.000000, 13.550000,
    560.000000, 14.330000, 565.000000, 15.010000, 570.000000, 15.520000,
    575.000000, 18.290001, 580.000000, 19.549999, 585.000000, 15.480000,
    590.000000, 14.910000, 595.000000, 14.150000, 600.000000, 13.220000,
    605.000000, 12.190000, 610.000000, 11.120000, 615.000000, 10.030000,
    620.000000, 8.950000,  625.000000, 7.960000,  630.000000, 7.020000,
    635.000000, 6.200000,  640.000000, 5.420000,  645.000000, 4.730000,
    650.000000, 4.150000,  655.000000, 3.640000,  660.000000, 3.200000,
    665.000000, 2.810000,  670.000000, 2.470000,  675.000000, 2.180000,
    680.000000, 1.930000,  685.000000, 1.720000,  690.000000, 1.670000,
    695.000000, 1.430000,  700.000000, 1.290000,  705.000000, 1.190000,
    710.000000, 1.080000,  715.000000, 0.960000,  720.000000, 0.880000,
    725.000000, 0.810000,  730.000000, 0.770000,  735.000000, 0.750000,
    740.000000, 0.730000,  745.000000, 0.680000,  750.000000, 0.690000,
    755.000000, 0.640000,  760.000000, 0.680000,  765.000000, 0.690000,
    770.000000, 0.610000,  775.000000, 0.520000,  780.000000, 0.430000,
};

const Float CIE_Illum_F2[] = {
    380.000000, 1.180000,  385.000000, 1.480000,  390.000000, 1.840000,
    395.000000, 2.150000,  400.000000, 3.440000,  405.000000, 15.690000,
    410.000000, 3.850000,  415.000000, 3.740000,  420.000000, 4.190000,
    425.000000, 4.620000,  430.000000, 5.060000,  435.000000, 34.980000,
    440.000000, 11.810000, 445.000000, 6.270000,  450.000000, 6.630000,
    455.000000, 6.930000,  460.000000, 7.190000,  465.000000, 7.400000,
    470.000000, 7.540000,  475.000000, 7.620000,  480.000000, 7.650000,
    485.000000, 7.620000,  490.000000, 7.620000,  495.000000, 7.450000,
    500.000000, 7.280000,  505.000000, 7.150000,  510.000000, 7.050000,
    515.000000, 7.040000,  520.000000, 7.160000,  525.000000, 7.470000,
    530.000000, 8.040000,  535.000000, 8.880000,  540.000000, 10.010000,
    545.000000, 24.879999, 550.000000, 16.639999, 555.000000, 14.590000,
    560.000000, 16.160000, 565.000000, 17.559999, 570.000000, 18.620001,
    575.000000, 21.469999, 580.000000, 22.790001, 585.000000, 19.290001,
    590.000000, 18.660000, 595.000000, 17.730000, 600.000000, 16.540001,
    605.000000, 15.210000, 610.000000, 13.800000, 615.000000, 12.360000,
    620.000000, 10.950000, 625.000000, 9.650000,  630.000000, 8.400000,
    635.000000, 7.320000,  640.000000, 6.310000,  645.000000, 5.430000,
    650.000000, 4.680000,  655.000000, 4.020000,  660.000000, 3.450000,
    665.000000, 2.960000,  670.000000, 2.550000,  675.000000, 2.190000,
    680.000000, 1.890000,  685.000000, 1.640000,  690.000000, 1.530000,
    695.000000, 1.270000,  700.000000, 1.100000,  705.000000, 0.990000,
    710.000000, 0.880000,  715.000000, 0.760000,  720.000000, 0.680000,
    725.000000, 0.610000,  730.000000, 0.560000,  735.000000, 0.540000,
    740.000000, 0.510000,  745.000000, 0.470000,  750.000000, 0.470000,
    755.000000, 0.430000,  760.000000, 0.460000,  765.000000, 0.470000,
    770.000000, 0.400000,  775.000000, 0.330000,  780.000000, 0.270000,
};

const Float CIE_Illum_F3[] = {
    380.000000, 0.820000,  385.000000, 1.020000,  390.000000, 1.260000,
    395.000000, 1.440000,  400.000000, 2.570000,  405.000000, 14.360000,
    410.000000, 2.700000,  415.000000, 2.450000,  420.000000, 2.730000,
    425.000000, 3.000000,  430.000000, 3.280000,  435.000000, 31.850000,
    440.000000, 9.470000,  445.000000, 4.020000,  450.000000, 4.250000,
    455.000000, 4.440000,  460.000000, 4.590000,  465.000000, 4.720000,
    470.000000, 4.800000,  475.000000, 4.860000,  480.000000, 4.870000,
    485.000000, 4.850000,  490.000000, 4.880000,  495.000000, 4.770000,
    500.000000, 4.670000,  505.000000, 4.620000,  510.000000, 4.620000,
    515.000000, 4.730000,  520.000000, 4.990000,  525.000000, 5.480000,
    530.000000, 6.250000,  535.000000, 7.340000,  540.000000, 8.780000,
    545.000000, 23.820000, 550.000000, 16.139999, 555.000000, 14.590000,
    560.000000, 16.629999, 565.000000, 18.490000, 570.000000, 19.950001,
    575.000000, 23.110001, 580.000000, 24.690001, 585.000000, 21.410000,
    590.000000, 20.850000, 595.000000, 19.930000, 600.000000, 18.670000,
    605.000000, 17.219999, 610.000000, 15.650000, 615.000000, 14.040000,
    620.000000, 12.450000, 625.000000, 10.950000, 630.000000, 9.510000,
    635.000000, 8.270000,  640.000000, 7.110000,  645.000000, 6.090000,
    650.000000, 5.220000,  655.000000, 4.450000,  660.000000, 3.800000,
    665.000000, 3.230000,  670.000000, 2.750000,  675.000000, 2.330000,
    680.000000, 1.990000,  685.000000, 1.700000,  690.000000, 1.550000,
    695.000000, 1.270000,  700.000000, 1.090000,  705.000000, 0.960000,
    710.000000, 0.830000,  715.000000, 0.710000,  720.000000, 0.620000,
    725.000000, 0.540000,  730.000000, 0.490000,  735.000000, 0.460000,
    740.000000, 0.430000,  745.000000, 0.390000,  750.000000, 0.390000,
    755.000000, 0.350000,  760.000000, 0.380000,  765.000000, 0.390000,
    770.000000, 0.330000,  775.000000, 0.280000,  780.000000, 0.210000,
};

const Float CIE_Illum_F4[] = {
    380.000000, 0.570000,  385.000000, 0.700000,  390.000000, 0.870000,
    395.000000, 0.980000,  400.000000, 2.010000,  405.000000, 13.750000,
    410.000000, 1.950000,  415.000000, 1.590000,  420.000000, 1.760000,
    425.000000, 1.930000,  430.000000, 2.100000,  435.000000, 30.280001,
    440.000000, 8.030000,  445.000000, 2.550000,  450.000000, 2.700000,
    455.000000, 2.820000,  460.000000, 2.910000,  465.000000, 2.990000,
    470.000000, 3.040000,  475.000000, 3.080000,  480.000000, 3.090000,
    485.000000, 3.090000,  490.000000, 3.140000,  495.000000, 3.060000,
    500.000000, 3.000000,  505.000000, 2.980000,  510.000000, 3.010000,
    515.000000, 3.140000,  520.000000, 3.410000,  525.000000, 3.900000,
    530.000000, 4.690000,  535.000000, 5.810000,  540.000000, 7.320000,
    545.000000, 22.590000, 550.000000, 15.110000, 555.000000, 13.880000,
    560.000000, 16.330000, 565.000000, 18.680000, 570.000000, 20.639999,
    575.000000, 24.280001, 580.000000, 26.260000, 585.000000, 23.280001,
    590.000000, 22.940001, 595.000000, 22.139999, 600.000000, 20.910000,
    605.000000, 19.430000, 610.000000, 17.740000, 615.000000, 16.000000,
    620.000000, 14.420000, 625.000000, 12.560000, 630.000000, 10.930000,
    635.000000, 9.520000,  640.000000, 8.180000,  645.000000, 7.010000,
    650.000000, 6.000000,  655.000000, 5.110000,  660.000000, 4.360000,
    665.000000, 3.690000,  670.000000, 3.130000,  675.000000, 2.640000,
    680.000000, 2.240000,  685.000000, 1.910000,  690.000000, 1.700000,
    695.000000, 1.390000,  700.000000, 1.180000,  705.000000, 1.030000,
    710.000000, 0.880000,  715.000000, 0.740000,  720.000000, 0.640000,
    725.000000, 0.540000,  730.000000, 0.490000,  735.000000, 0.460000,
    740.000000, 0.420000,  745.000000, 0.370000,  750.000000, 0.370000,
    755.000000, 0.330000,  760.000000, 0.350000,  765.000000, 0.360000,
    770.000000, 0.310000,  775.000000, 0.260000,  780.000000, 0.190000,
};

const Float CIE_Illum_F5[] = {
    380.000000, 1.870000,  385.000000, 2.350000,  390.000000, 2.920000,
    395.000000, 3.450000,  400.000000, 5.100000,  405.000000, 18.910000,
    410.000000, 6.000000,  415.000000, 6.110000,  420.000000, 6.850000,
    425.000000, 7.580000,  430.000000, 8.310000,  435.000000, 40.759998,
    440.000000, 16.059999, 445.000000, 10.320000, 450.000000, 10.910000,
    455.000000, 11.400000, 460.000000, 11.830000, 465.000000, 12.170000,
    470.000000, 12.400000, 475.000000, 12.540000, 480.000000, 12.580000,
    485.000000, 12.520000, 490.000000, 12.470000, 495.000000, 12.200000,
    500.000000, 11.890000, 505.000000, 11.610000, 510.000000, 11.330000,
    515.000000, 11.100000, 520.000000, 10.960000, 525.000000, 10.970000,
    530.000000, 11.160000, 535.000000, 11.540000, 540.000000, 12.120000,
    545.000000, 27.780001, 550.000000, 17.730000, 555.000000, 14.470000,
    560.000000, 15.200000, 565.000000, 15.770000, 570.000000, 16.100000,
    575.000000, 18.540001, 580.000000, 19.500000, 585.000000, 15.390000,
    590.000000, 14.640000, 595.000000, 13.720000, 600.000000, 12.690000,
    605.000000, 11.570000, 610.000000, 10.450000, 615.000000, 9.350000,
    620.000000, 8.290000,  625.000000, 7.320000,  630.000000, 6.410000,
    635.000000, 5.630000,  640.000000, 4.900000,  645.000000, 4.260000,
    650.000000, 3.720000,  655.000000, 3.250000,  660.000000, 2.830000,
    665.000000, 2.490000,  670.000000, 2.190000,  675.000000, 1.930000,
    680.000000, 1.710000,  685.000000, 1.520000,  690.000000, 1.430000,
    695.000000, 1.260000,  700.000000, 1.130000,  705.000000, 1.050000,
    710.000000, 0.960000,  715.000000, 0.850000,  720.000000, 0.780000,
    725.000000, 0.720000,  730.000000, 0.680000,  735.000000, 0.670000,
    740.000000, 0.650000,  745.000000, 0.610000,  750.000000, 0.620000,
    755.000000, 0.590000,  760.000000, 0.620000,  765.000000, 0.640000,
    770.000000, 0.550000,  775.000000, 0.470000,  780.000000, 0.400000,
};

const Float CIE_Illum_F6[] = {
    380.000000, 1.050000,  385.000000, 1.310000,  390.000000, 1.630000,
    395.000000, 1.900000,  400.000000, 3.110000,  405.000000, 14.800000,
    410.000000, 3.430000,  415.000000, 3.300000,  420.000000, 3.680000,
    425.000000, 4.070000,  430.000000, 4.450000,  435.000000, 32.610001,
    440.000000, 10.740000, 445.000000, 5.480000,  450.000000, 5.780000,
    455.000000, 6.030000,  460.000000, 6.250000,  465.000000, 6.410000,
    470.000000, 6.520000,  475.000000, 6.580000,  480.000000, 6.590000,
    485.000000, 6.560000,  490.000000, 6.560000,  495.000000, 6.420000,
    500.000000, 6.280000,  505.000000, 6.200000,  510.000000, 6.190000,
    515.000000, 6.300000,  520.000000, 6.600000,  525.000000, 7.120000,
    530.000000, 7.940000,  535.000000, 9.070000,  540.000000, 10.490000,
    545.000000, 25.219999, 550.000000, 17.459999, 555.000000, 15.630000,
    560.000000, 17.219999, 565.000000, 18.530001, 570.000000, 19.430000,
    575.000000, 21.969999, 580.000000, 23.010000, 585.000000, 19.410000,
    590.000000, 18.559999, 595.000000, 17.420000, 600.000000, 16.090000,
    605.000000, 14.640000, 610.000000, 13.150000, 615.000000, 11.680000,
    620.000000, 10.250000, 625.000000, 8.960000,  630.000000, 7.740000,
    635.000000, 6.690000,  640.000000, 5.710000,  645.000000, 4.870000,
    650.000000, 4.160000,  655.000000, 3.550000,  660.000000, 3.020000,
    665.000000, 2.570000,  670.000000, 2.200000,  675.000000, 1.870000,
    680.000000, 1.600000,  685.000000, 1.370000,  690.000000, 1.290000,
    695.000000, 1.050000,  700.000000, 0.910000,  705.000000, 0.810000,
    710.000000, 0.710000,  715.000000, 0.610000,  720.000000, 0.540000,
    725.000000, 0.480000,  730.000000, 0.440000,  735.000000, 0.430000,
    740.000000, 0.400000,  745.000000, 0.370000,  750.000000, 0.380000,
    755.000000, 0.350000,  760.000000, 0.390000,  765.000000, 0.410000,
    770.000000, 0.330000,  775.000000, 0.260000,  780.000000, 0.210000,
};

const Float CIE_Illum_F7[] = {
    380.000000, 2.560000,  385.000000, 3.180000,  390.000000, 3.840000,
    395.000000, 4.530000,  400.000000, 6.150000,  405.000000, 19.370001,
    410.000000, 7.370000,  415.000000, 7.050000,  420.000000, 7.710000,
    425.000000, 8.410000,  430.000000, 9.150000,  435.000000, 44.139999,
    440.000000, 17.520000, 445.000000, 11.350000, 450.000000, 12.000000,
    455.000000, 12.580000, 460.000000, 13.080000, 465.000000, 13.450000,
    470.000000, 13.710000, 475.000000, 13.880000, 480.000000, 13.950000,
    485.000000, 13.930000, 490.000000, 13.820000, 495.000000, 13.640000,
    500.000000, 13.430000, 505.000000, 13.250000, 510.000000, 13.080000,
    515.000000, 12.930000, 520.000000, 12.780000, 525.000000, 12.600000,
    530.000000, 12.440000, 535.000000, 12.330000, 540.000000, 12.260000,
    545.000000, 29.520000, 550.000000, 17.049999, 555.000000, 12.440000,
    560.000000, 12.580000, 565.000000, 12.720000, 570.000000, 12.830000,
    575.000000, 15.460000, 580.000000, 16.750000, 585.000000, 12.830000,
    590.000000, 12.670000, 595.000000, 12.450000, 600.000000, 12.190000,
    605.000000, 11.890000, 610.000000, 11.600000, 615.000000, 11.350000,
    620.000000, 11.120000, 625.000000, 10.950000, 630.000000, 10.760000,
    635.000000, 10.420000, 640.000000, 10.110000, 645.000000, 10.040000,
    650.000000, 10.020000, 655.000000, 10.110000, 660.000000, 9.870000,
    665.000000, 8.650000,  670.000000, 7.270000,  675.000000, 6.440000,
    680.000000, 5.830000,  685.000000, 5.410000,  690.000000, 5.040000,
    695.000000, 4.570000,  700.000000, 4.120000,  705.000000, 3.770000,
    710.000000, 3.460000,  715.000000, 3.080000,  720.000000, 2.730000,
    725.000000, 2.470000,  730.000000, 2.250000,  735.000000, 2.060000,
    740.000000, 1.900000,  745.000000, 1.750000,  750.000000, 1.620000,
    755.000000, 1.540000,  760.000000, 1.450000,  765.000000, 1.320000,
    770.000000, 1.170000,  775.000000, 0.990000,  780.000000, 0.810000,
};

const Float CIE_Illum_F8[] = {
    380.000000, 1.210000,  385.000000, 1.500000,  390.000000, 1.810000,
    395.000000, 2.130000,  400.000000, 3.170000,  405.000000, 13.080000,
    410.000000, 3.830000,  415.000000, 3.450000,  420.000000, 3.860000,
    425.000000, 4.420000,  430.000000, 5.090000,  435.000000, 34.099998,
    440.000000, 12.420000, 445.000000, 7.680000,  450.000000, 8.600000,
    455.000000, 9.460000,  460.000000, 10.240000, 465.000000, 10.840000,
    470.000000, 11.330000, 475.000000, 11.710000, 480.000000, 11.980000,
    485.000000, 12.170000, 490.000000, 12.280000, 495.000000, 12.320000,
    500.000000, 12.350000, 505.000000, 12.440000, 510.000000, 12.550000,
    515.000000, 12.680000, 520.000000, 12.770000, 525.000000, 12.720000,
    530.000000, 12.600000, 535.000000, 12.430000, 540.000000, 12.220000,
    545.000000, 28.959999, 550.000000, 16.510000, 555.000000, 11.790000,
    560.000000, 11.760000, 565.000000, 11.770000, 570.000000, 11.840000,
    575.000000, 14.610000, 580.000000, 16.110001, 585.000000, 12.340000,
    590.000000, 12.530000, 595.000000, 12.720000, 600.000000, 12.920000,
    605.000000, 13.120000, 610.000000, 13.340000, 615.000000, 13.610000,
    620.000000, 13.870000, 625.000000, 14.070000, 630.000000, 14.200000,
    635.000000, 14.160000, 640.000000, 14.130000, 645.000000, 14.340000,
    650.000000, 14.500000, 655.000000, 14.460000, 660.000000, 14.000000,
    665.000000, 12.580000, 670.000000, 10.990000, 675.000000, 9.980000,
    680.000000, 9.220000,  685.000000, 8.620000,  690.000000, 8.070000,
    695.000000, 7.390000,  700.000000, 6.710000,  705.000000, 6.160000,
    710.000000, 5.630000,  715.000000, 5.030000,  720.000000, 4.460000,
    725.000000, 4.020000,  730.000000, 3.660000,  735.000000, 3.360000,
    740.000000, 3.090000,  745.000000, 2.850000,  750.000000, 2.650000,
    755.000000, 2.510000,  760.000000, 2.370000,  765.000000, 2.150000,
    770.000000, 1.890000,  775.000000, 1.610000,  780.000000, 1.320000,
};

const Float CIE_Illum_F9[] = {
    380.000000, 0.900000,  385.000000, 1.120000,  390.000000, 1.360000,
    395.000000, 1.600000,  400.000000, 2.590000,  405.000000, 12.800000,
    410.000000, 3.050000,  415.000000, 2.560000,  420.000000, 2.860000,
    425.000000, 3.300000,  430.000000, 3.820000,  435.000000, 32.619999,
    440.000000, 10.770000, 445.000000, 5.840000,  450.000000, 6.570000,
    455.000000, 7.250000,  460.000000, 7.860000,  465.000000, 8.350000,
    470.000000, 8.750000,  475.000000, 9.060000,  480.000000, 9.310000,
    485.000000, 9.480000,  490.000000, 9.610000,  495.000000, 9.680000,
    500.000000, 9.740000,  505.000000, 9.880000,  510.000000, 10.040000,
    515.000000, 10.260000, 520.000000, 10.480000, 525.000000, 10.630000,
    530.000000, 10.760000, 535.000000, 10.960000, 540.000000, 11.180000,
    545.000000, 27.709999, 550.000000, 16.290001, 555.000000, 12.280000,
    560.000000, 12.740000, 565.000000, 13.210000, 570.000000, 13.650000,
    575.000000, 16.570000, 580.000000, 18.139999, 585.000000, 14.550000,
    590.000000, 14.650000, 595.000000, 14.660000, 600.000000, 14.610000,
    605.000000, 14.500000, 610.000000, 14.390000, 615.000000, 14.400000,
    620.000000, 14.470000, 625.000000, 14.620000, 630.000000, 14.720000,
    635.000000, 14.550000, 640.000000, 14.400000, 645.000000, 14.580000,
    650.000000, 14.880000, 655.000000, 15.510000, 660.000000, 15.470000,
    665.000000, 13.200000, 670.000000, 10.570000, 675.000000, 9.180000,
    680.000000, 8.250000,  685.000000, 7.570000,  690.000000, 7.030000,
    695.000000, 6.350000,  700.000000, 5.720000,  705.000000, 5.250000,
    710.000000, 4.800000,  715.000000, 4.290000,  720.000000, 3.800000,
    725.000000, 3.430000,  730.000000, 3.120000,  735.000000, 2.860000,
    740.000000, 2.640000,  745.000000, 2.430000,  750.000000, 2.260000,
    755.000000, 2.140000,  760.000000, 2.020000,  765.000000, 1.830000,
    770.000000, 1.610000,  775.000000, 1.380000,  780.000000, 1.120000,
};

const Float CIE_Illum_F10[] = {
    380.000000, 1.110000,  385.000000, 0.630000,  390.000000, 0.620000,
    395.000000, 0.570000,  400.000000, 1.480000,  405.000000, 12.160000,
    410.000000, 2.120000,  415.000000, 2.700000,  420.000000, 3.740000,
    425.000000, 5.140000,  430.000000, 6.750000,  435.000000, 34.389999,
    440.000000, 14.860000, 445.000000, 10.400000, 450.000000, 10.760000,
    455.000000, 10.670000, 460.000000, 10.110000, 465.000000, 9.270000,
    470.000000, 8.290000,  475.000000, 7.290000,  480.000000, 7.910000,
    485.000000, 16.639999, 490.000000, 16.730000, 495.000000, 10.440000,
    500.000000, 5.940000,  505.000000, 3.340000,  510.000000, 2.350000,
    515.000000, 1.880000,  520.000000, 1.590000,  525.000000, 1.470000,
    530.000000, 1.800000,  535.000000, 5.710000,  540.000000, 40.980000,
    545.000000, 73.690002, 550.000000, 33.610001, 555.000000, 8.240000,
    560.000000, 3.380000,  565.000000, 2.470000,  570.000000, 2.140000,
    575.000000, 4.860000,  580.000000, 11.450000, 585.000000, 14.790000,
    590.000000, 12.160000, 595.000000, 8.970000,  600.000000, 6.520000,
    605.000000, 8.810000,  610.000000, 44.119999, 615.000000, 34.549999,
    620.000000, 12.090000, 625.000000, 12.150000, 630.000000, 10.520000,
    635.000000, 4.430000,  640.000000, 1.950000,  645.000000, 2.190000,
    650.000000, 3.190000,  655.000000, 2.770000,  660.000000, 2.290000,
    665.000000, 2.000000,  670.000000, 1.520000,  675.000000, 1.350000,
    680.000000, 1.470000,  685.000000, 1.790000,  690.000000, 1.740000,
    695.000000, 1.020000,  700.000000, 1.140000,  705.000000, 3.320000,
    710.000000, 4.490000,  715.000000, 2.050000,  720.000000, 0.490000,
    725.000000, 0.240000,  730.000000, 0.210000,  735.000000, 0.210000,
    740.000000, 0.240000,  745.000000, 0.240000,  750.000000, 0.210000,
    755.000000, 0.170000,  760.000000, 0.210000,  765.000000, 0.220000,
    770.000000, 0.170000,  775.000000, 0.120000,  780.000000, 0.090000,
};

const Float CIE_Illum_F11[] = {
    380.000000, 0.910000,  385.000000, 0.630000,  390.000000, 0.460000,
    395.000000, 0.370000,  400.000000, 1.290000,  405.000000, 12.680000,
    410.000000, 1.590000,  415.000000, 1.790000,  420.000000, 2.460000,
    425.000000, 3.330000,  430.000000, 4.490000,  435.000000, 33.939999,
    440.000000, 12.130000, 445.000000, 6.950000,  450.000000, 7.190000,
    455.000000, 7.120000,  460.000000, 6.720000,  465.000000, 6.130000,
    470.000000, 5.460000,  475.000000, 4.790000,  480.000000, 5.660000,
    485.000000, 14.290000, 490.000000, 14.960000, 495.000000, 8.970000,
    500.000000, 4.720000,  505.000000, 2.330000,  510.000000, 1.470000,
    515.000000, 1.100000,  520.000000, 0.890000,  525.000000, 0.830000,
    530.000000, 1.180000,  535.000000, 4.900000,  540.000000, 39.590000,
    545.000000, 72.839996, 550.000000, 32.610001, 555.000000, 7.520000,
    560.000000, 2.830000,  565.000000, 1.960000,  570.000000, 1.670000,
    575.000000, 4.430000,  580.000000, 11.280000, 585.000000, 14.760000,
    590.000000, 12.730000, 595.000000, 9.740000,  600.000000, 7.330000,
    605.000000, 9.720000,  610.000000, 55.270000, 615.000000, 42.580002,
    620.000000, 13.180000, 625.000000, 13.160000, 630.000000, 12.260000,
    635.000000, 5.110000,  640.000000, 2.070000,  645.000000, 2.340000,
    650.000000, 3.580000,  655.000000, 3.010000,  660.000000, 2.480000,
    665.000000, 2.140000,  670.000000, 1.540000,  675.000000, 1.330000,
    680.000000, 1.460000,  685.000000, 1.940000,  690.000000, 2.000000,
    695.000000, 1.200000,  700.000000, 1.350000,  705.000000, 4.100000,
    710.000000, 5.580000,  715.000000, 2.510000,  720.000000, 0.570000,
    725.000000, 0.270000,  730.000000, 0.230000,  735.000000, 0.210000,
    740.000000, 0.240000,  745.000000, 0.240000,  750.000000, 0.200000,
    755.000000, 0.240000,  760.000000, 0.320000,  765.000000, 0.260000,
    770.000000, 0.160000,  775.000000, 0.120000,  780.000000, 0.090000,
};

const Float CIE_Illum_F12[] = {
    380.000000, 0.960000,  385.000000, 0.640000,  390.000000, 0.450000,
    395.000000, 0.330000,  400.000000, 1.190000,  405.000000, 12.480000,
    410.000000, 1.120000,  415.000000, 0.940000,  420.000000, 1.080000,
    425.000000, 1.370000,  430.000000, 1.780000,  435.000000, 29.049999,
    440.000000, 7.900000,  445.000000, 2.650000,  450.000000, 2.710000,
    455.000000, 2.650000,  460.000000, 2.490000,  465.000000, 2.330000,
    470.000000, 2.100000,  475.000000, 1.910000,  480.000000, 3.010000,
    485.000000, 10.830000, 490.000000, 11.880000, 495.000000, 6.880000,
    500.000000, 3.430000,  505.000000, 1.490000,  510.000000, 0.920000,
    515.000000, 0.710000,  520.000000, 0.600000,  525.000000, 0.630000,
    530.000000, 1.100000,  535.000000, 4.560000,  540.000000, 34.400002,
    545.000000, 65.400002, 550.000000, 29.480000, 555.000000, 7.160000,
    560.000000, 3.080000,  565.000000, 2.470000,  570.000000, 2.270000,
    575.000000, 5.090000,  580.000000, 11.960000, 585.000000, 15.320000,
    590.000000, 14.270000, 595.000000, 11.860000, 600.000000, 9.280000,
    605.000000, 12.310000, 610.000000, 68.529999, 615.000000, 53.020000,
    620.000000, 14.670000, 625.000000, 14.380000, 630.000000, 14.710000,
    635.000000, 6.460000,  640.000000, 2.570000,  645.000000, 2.750000,
    650.000000, 4.180000,  655.000000, 3.440000,  660.000000, 2.810000,
    665.000000, 2.420000,  670.000000, 1.640000,  675.000000, 1.360000,
    680.000000, 1.490000,  685.000000, 2.140000,  690.000000, 2.340000,
    695.000000, 1.420000,  700.000000, 1.610000,  705.000000, 5.040000,
    710.000000, 6.980000,  715.000000, 3.190000,  720.000000, 0.710000,
    725.000000, 0.300000,  730.000000, 0.260000,  735.000000, 0.230000,
    740.000000, 0.280000,  745.000000, 0.280000,  750.000000, 0.210000,
    755.000000, 0.170000,  760.000000, 0.210000,  765.000000, 0.190000,
    770.000000, 0.150000,  775.000000, 0.100000,  780.000000, 0.050000,
};

const Float Ag_eta[] = {
    298.757050, 1.519000, 302.400421, 1.496000, 306.133759, 1.432500,
    309.960449, 1.323000, 313.884003, 1.142062, 317.908142, 0.932000,
    322.036835, 0.719062, 326.274139, 0.526000, 330.624481, 0.388125,
    335.092377, 0.294000, 339.682678, 0.253313, 344.400482, 0.238000,
    349.251221, 0.221438, 354.240509, 0.209000, 359.374420, 0.194813,
    364.659332, 0.186000, 370.102020, 0.192063, 375.709625, 0.200000,
    381.489777, 0.198063, 387.450562, 0.192000, 393.600555, 0.182000,
    399.948975, 0.173000, 406.505493, 0.172625, 413.280579, 0.173000,
    420.285339, 0.166688, 427.531647, 0.160000, 435.032196, 0.158500,
    442.800629, 0.157000, 450.851562, 0.151063, 459.200653, 0.144000,
    467.864838, 0.137313, 476.862213, 0.132000, 486.212463, 0.130250,
    495.936707, 0.130000, 506.057861, 0.129938, 516.600769, 0.130000,
    527.592224, 0.130063, 539.061646, 0.129000, 551.040771, 0.124375,
    563.564453, 0.120000, 576.670593, 0.119313, 590.400818, 0.121000,
    604.800842, 0.125500, 619.920898, 0.131000, 635.816284, 0.136125,
    652.548279, 0.140000, 670.184753, 0.140063, 688.800964, 0.140000,
    708.481018, 0.144313, 729.318665, 0.148000, 751.419250, 0.145875,
    774.901123, 0.143000, 799.897949, 0.142563, 826.561157, 0.145000,
    855.063293, 0.151938, 885.601257, 0.163000,
};

const Float Ag_k[] = {
    298.757050, 1.080000, 302.400421, 0.882000, 306.133759, 0.761063,
    309.960449, 0.647000, 313.884003, 0.550875, 317.908142, 0.504000,
    322.036835, 0.554375, 326.274139, 0.663000, 330.624481, 0.818563,
    335.092377, 0.986000, 339.682678, 1.120687, 344.400482, 1.240000,
    349.251221, 1.345250, 354.240509, 1.440000, 359.374420, 1.533750,
    364.659332, 1.610000, 370.102020, 1.641875, 375.709625, 1.670000,
    381.489777, 1.735000, 387.450562, 1.810000, 393.600555, 1.878750,
    399.948975, 1.950000, 406.505493, 2.029375, 413.280579, 2.110000,
    420.285339, 2.186250, 427.531647, 2.260000, 435.032196, 2.329375,
    442.800629, 2.400000, 450.851562, 2.478750, 459.200653, 2.560000,
    467.864838, 2.640000, 476.862213, 2.720000, 486.212463, 2.798125,
    495.936707, 2.880000, 506.057861, 2.973750, 516.600769, 3.070000,
    527.592224, 3.159375, 539.061646, 3.250000, 551.040771, 3.348125,
    563.564453, 3.450000, 576.670593, 3.553750, 590.400818, 3.660000,
    604.800842, 3.766250, 619.920898, 3.880000, 635.816284, 4.010625,
    652.548279, 4.150000, 670.184753, 4.293125, 688.800964, 4.440000,
    708.481018, 4.586250, 729.318665, 4.740000, 751.419250, 4.908125,
    774.901123, 5.090000, 799.897949, 5.288750, 826.561157, 5.500000,
    855.063293, 5.720624, 885.601257, 5.950000,
};

const Float Al_eta[] = {
    298.757050, 0.273375, 302.400421, 0.280000, 306.133759, 0.286813,
    309.960449, 0.294000, 313.884003, 0.301875, 317.908142, 0.310000,
    322.036835, 0.317875, 326.274139, 0.326000, 330.624481, 0.334750,
    335.092377, 0.344000, 339.682678, 0.353813, 344.400482, 0.364000,
    349.251221, 0.374375, 354.240509, 0.385000, 359.374420, 0.395750,
    364.659332, 0.407000, 370.102020, 0.419125, 375.709625, 0.432000,
    381.489777, 0.445688, 387.450562, 0.460000, 393.600555, 0.474688,
    399.948975, 0.490000, 406.505493, 0.506188, 413.280579, 0.523000,
    420.285339, 0.540063, 427.531647, 0.558000, 435.032196, 0.577313,
    442.800629, 0.598000, 450.851562, 0.620313, 459.200653, 0.644000,
    467.864838, 0.668625, 476.862213, 0.695000, 486.212463, 0.723750,
    495.936707, 0.755000, 506.057861, 0.789000, 516.600769, 0.826000,
    527.592224, 0.867000, 539.061646, 0.912000, 551.040771, 0.963000,
    563.564453, 1.020000, 576.670593, 1.080000, 590.400818, 1.150000,
    604.800842, 1.220000, 619.920898, 1.300000, 635.816284, 1.390000,
    652.548279, 1.490000, 670.184753, 1.600000, 688.800964, 1.740000,
    708.481018, 1.910000, 729.318665, 2.140000, 751.419250, 2.410000,
    774.901123, 2.630000, 799.897949, 2.800000, 826.561157, 2.740000,
    855.063293, 2.580000, 885.601257, 2.240000,
};

const Float Al_k[] = {
    298.757050, 3.593750, 302.400421, 3.640000, 306.133759, 3.689375,
    309.960449, 3.740000, 313.884003, 3.789375, 317.908142, 3.840000,
    322.036835, 3.894375, 326.274139, 3.950000, 330.624481, 4.005000,
    335.092377, 4.060000, 339.682678, 4.113750, 344.400482, 4.170000,
    349.251221, 4.233750, 354.240509, 4.300000, 359.374420, 4.365000,
    364.659332, 4.430000, 370.102020, 4.493750, 375.709625, 4.560000,
    381.489777, 4.633750, 387.450562, 4.710000, 393.600555, 4.784375,
    399.948975, 4.860000, 406.505493, 4.938125, 413.280579, 5.020000,
    420.285339, 5.108750, 427.531647, 5.200000, 435.032196, 5.290000,
    442.800629, 5.380000, 450.851562, 5.480000, 459.200653, 5.580000,
    467.864838, 5.690000, 476.862213, 5.800000, 486.212463, 5.915000,
    495.936707, 6.030000, 506.057861, 6.150000, 516.600769, 6.280000,
    527.592224, 6.420000, 539.061646, 6.550000, 551.040771, 6.700000,
    563.564453, 6.850000, 576.670593, 7.000000, 590.400818, 7.150000,
    604.800842, 7.310000, 619.920898, 7.480000, 635.816284, 7.650000,
    652.548279, 7.820000, 670.184753, 8.010000, 688.800964, 8.210000,
    708.481018, 8.390000, 729.318665, 8.570000, 751.419250, 8.620000,
    774.901123, 8.600000, 799.897949, 8.450000, 826.561157, 8.310000,
    855.063293, 8.210000, 885.601257, 8.210000,
};

const Float Au_eta[] = {
    298.757050, 1.795000, 302.400421, 1.812000, 306.133759, 1.822625,
    309.960449, 1.830000, 313.884003, 1.837125, 317.908142, 1.840000,
    322.036835, 1.834250, 326.274139, 1.824000, 330.624481, 1.812000,
    335.092377, 1.798000, 339.682678, 1.782000, 344.400482, 1.766000,
    349.251221, 1.752500, 354.240509, 1.740000, 359.374420, 1.727625,
    364.659332, 1.716000, 370.102020, 1.705875, 375.709625, 1.696000,
    381.489777, 1.684750, 387.450562, 1.674000, 393.600555, 1.666000,
    399.948975, 1.658000, 406.505493, 1.647250, 413.280579, 1.636000,
    420.285339, 1.628000, 427.531647, 1.616000, 435.032196, 1.596250,
    442.800629, 1.562000, 450.851562, 1.502125, 459.200653, 1.426000,
    467.864838, 1.345875, 476.862213, 1.242000, 486.212463, 1.086750,
    495.936707, 0.916000, 506.057861, 0.754500, 516.600769, 0.608000,
    527.592224, 0.491750, 539.061646, 0.402000, 551.040771, 0.345500,
    563.564453, 0.306000, 576.670593, 0.267625, 590.400818, 0.236000,
    604.800842, 0.212375, 619.920898, 0.194000, 635.816284, 0.177750,
    652.548279, 0.166000, 670.184753, 0.161000, 688.800964, 0.160000,
    708.481018, 0.160875, 729.318665, 0.164000, 751.419250, 0.169500,
    774.901123, 0.176000, 799.897949, 0.181375, 826.561157, 0.188000,
    855.063293, 0.198125, 885.601257, 0.210000,
};

const Float Au_k[] = {
    298.757050, 1.920375, 302.400421, 1.920000, 306.133759, 1.918875,
    309.960449, 1.916000, 313.884003, 1.911375, 317.908142, 1.904000,
    322.036835, 1.891375, 326.274139, 1.878000, 330.624481, 1.868250,
    335.092377, 1.860000, 339.682678, 1.851750, 344.400482, 1.846000,
    349.251221, 1.845250, 354.240509, 1.848000, 359.374420, 1.852375,
    364.659332, 1.862000, 370.102020, 1.883000, 375.709625, 1.906000,
    381.489777, 1.922500, 387.450562, 1.936000, 393.600555, 1.947750,
    399.948975, 1.956000, 406.505493, 1.959375, 413.280579, 1.958000,
    420.285339, 1.951375, 427.531647, 1.940000, 435.032196, 1.924500,
    442.800629, 1.904000, 450.851562, 1.875875, 459.200653, 1.846000,
    467.864838, 1.814625, 476.862213, 1.796000, 486.212463, 1.797375,
    495.936707, 1.840000, 506.057861, 1.956500, 516.600769, 2.120000,
    527.592224, 2.326250, 539.061646, 2.540000, 551.040771, 2.730625,
    563.564453, 2.880000, 576.670593, 2.940625, 590.400818, 2.970000,
    604.800842, 3.015000, 619.920898, 3.060000, 635.816284, 3.070000,
    652.548279, 3.150000, 670.184753, 3.445812, 688.800964, 3.800000,
    708.481018, 4.087687, 729.318665, 4.357000, 751.419250, 4.610188,
    774.901123, 4.860000, 799.897949, 5.125813, 826.561157, 5.390000,
    855.063293, 5.631250, 885.601257, 5.880000,
};

const Float Cu_eta[] = {
    298.757050, 1.400313, 302.400421, 1.380000, 306.133759, 1.358438,
    309.960449, 1.340000, 313.884003, 1.329063, 317.908142, 1.325000,
    322.036835, 1.332500, 326.274139, 1.340000, 330.624481, 1.334375,
    335.092377, 1.325000, 339.682678, 1.317812, 344.400482, 1.310000,
    349.251221, 1.300313, 354.240509, 1.290000, 359.374420, 1.281563,
    364.659332, 1.270000, 370.102020, 1.249062, 375.709625, 1.225000,
    381.489777, 1.200000, 387.450562, 1.180000, 393.600555, 1.174375,
    399.948975, 1.175000, 406.505493, 1.177500, 413.280579, 1.180000,
    420.285339, 1.178125, 427.531647, 1.175000, 435.032196, 1.172812,
    442.800629, 1.170000, 450.851562, 1.165312, 459.200653, 1.160000,
    467.864838, 1.155312, 476.862213, 1.150000, 486.212463, 1.142812,
    495.936707, 1.135000, 506.057861, 1.131562, 516.600769, 1.120000,
    527.592224, 1.092437, 539.061646, 1.040000, 551.040771, 0.950375,
    563.564453, 0.826000, 576.670593, 0.645875, 590.400818, 0.468000,
    604.800842, 0.351250, 619.920898, 0.272000, 635.816284, 0.230813,
    652.548279, 0.214000, 670.184753, 0.209250, 688.800964, 0.213000,
    708.481018, 0.216250, 729.318665, 0.223000, 751.419250, 0.236500,
    774.901123, 0.250000, 799.897949, 0.254188, 826.561157, 0.260000,
    855.063293, 0.280000, 885.601257, 0.300000,
};

const Float Cu_k[] = {
    298.757050, 1.662125, 302.400421, 1.687000, 306.133759, 1.703313,
    309.960449, 1.720000, 313.884003, 1.744563, 317.908142, 1.770000,
    322.036835, 1.791625, 326.274139, 1.810000, 330.624481, 1.822125,
    335.092377, 1.834000, 339.682678, 1.851750, 344.400482, 1.872000,
    349.251221, 1.894250, 354.240509, 1.916000, 359.374420, 1.931688,
    364.659332, 1.950000, 370.102020, 1.972438, 375.709625, 2.015000,
    381.489777, 2.121562, 387.450562, 2.210000, 393.600555, 2.177188,
    399.948975, 2.130000, 406.505493, 2.160063, 413.280579, 2.210000,
    420.285339, 2.249938, 427.531647, 2.289000, 435.032196, 2.326000,
    442.800629, 2.362000, 450.851562, 2.397625, 459.200653, 2.433000,
    467.864838, 2.469187, 476.862213, 2.504000, 486.212463, 2.535875,
    495.936707, 2.564000, 506.057861, 2.589625, 516.600769, 2.605000,
    527.592224, 2.595562, 539.061646, 2.583000, 551.040771, 2.576500,
    563.564453, 2.599000, 576.670593, 2.678062, 590.400818, 2.809000,
    604.800842, 3.010750, 619.920898, 3.240000, 635.816284, 3.458187,
    652.548279, 3.670000, 670.184753, 3.863125, 688.800964, 4.050000,
    708.481018, 4.239563, 729.318665, 4.430000, 751.419250, 4.619563,
    774.901123, 4.817000, 799.897949, 5.034125, 826.561157, 5.260000,
    855.063293, 5.485625, 885.601257, 5.717000,
};

const Float MgO_eta[] = {
    309.950012, 1.798000, 330.613007, 1.785000, 351.118988, 1.776800,
    355.549011, 1.775500, 360.932007, 1.773200, 361.141998, 1.773180,
    364.968994, 1.771860, 382.065002, 1.766800, 386.712006, 1.765500,
    393.337982, 1.763800, 404.634003, 1.761040, 430.935028, 1.755700,
    435.781982, 1.754710, 457.829010, 1.751200, 477.949036, 1.748300,
    486.004974, 1.747110, 487.918030, 1.746900, 499.919006, 1.745400,
    502.350006, 1.745300, 508.531982, 1.744460, 514.440002, 1.743900,
    546.166992, 1.740770, 589.258972, 1.737370, 632.874023, 1.734600,
    643.718018, 1.734000, 656.325989, 1.733350, 667.635986, 1.732770,
    690.695984, 1.731910, 706.439026, 1.731010, 767.677979, 1.728720,
};

const Float MgO_k[] = {
    309.950012, 0.000000, 330.613007, 0.000000, 351.118988, 0.000000,
    355.549011, 0.000001, 360.932007, 0.000001, 361.141998, 0.000001,
    364.968994, 0.000000, 382.065002, 0.000000, 386.712006, 0.000000,
    393.337982, 0.000000, 404.634003, 0.000000, 430.935028, 0.000000,
    435.781982, 0.000000, 457.829010, 0.000000, 477.949036, 0.000000,
    486.004974, 0.000000, 487.918030, 0.000000, 499.919006, 0.000000,
    502.350006, 0.000000, 508.531982, 0.000000, 514.440002, 0.000000,
    546.166992, 0.000000, 589.258972, 0.000000, 632.874023, 0.000000,
    643.718018, 0.000000, 656.325989, 0.000000, 667.635986, 0.000000,
    690.695984, 0.000000, 706.439026, 0.000000, 767.677979, 0.000000,
};

const Float TiO2_eta[] = {
    305.972015, 3.840000, 317.979004, 5.380000, 334.990997, 4.220000,
    344.007019, 4.360000, 359.988007, 3.870000, 388.044006, 3.490000,
    399.935028, 3.400000, 412.031006, 3.240000, 419.985992, 3.290000,
    439.957001, 3.200000, 460.037018, 3.130000, 479.985016, 3.080000,
    499.919006, 3.030000, 520.049988, 3.000000, 539.044006, 2.950000,
    539.983032, 2.970000, 559.981995, 2.940000, 579.888000, 2.920000,
    600.097046, 2.900000, 619.900024, 2.880000, 640.062012, 2.870000,
    659.819031, 2.850000, 680.088013, 2.840000, 700.056030, 2.830000,
    719.976990, 2.820000, 740.179016, 2.810000, 760.147034, 2.810000,
    779.747986, 2.800000, 799.871033, 2.790000, 819.974060, 2.790000,
    839.973083, 2.780000, 859.778015, 2.780000, 879.915039, 2.770000,
    899.709961, 2.770000,
};

const Float TiO2_k[] = {
    305.972015, 1.950000, 317.979004, 2.180000, 334.990997, 0.788000,
    344.007019, 0.000000, 359.988007, 0.251000, 388.044006, 0.000000,
    399.935028, 0.000000, 412.031006, 0.022000, 419.985992, 0.000000,
    439.957001, 0.000000, 460.037018, 0.000000, 479.985016, 0.000000,
    499.919006, 0.000000, 520.049988, 0.000000, 539.044006, 0.000000,
    539.983032, 0.000000, 559.981995, 0.000000, 579.888000, 0.000000,
    600.097046, 0.000000, 619.900024, 0.000000, 640.062012, 0.000000,
    659.819031, 0.000000, 680.088013, 0.000000, 700.056030, 0.000000,
    719.976990, 0.000000, 740.179016, 0.000000, 760.147034, 0.000000,
    779.747986, 0.000000, 799.871033, 0.000000, 819.974060, 0.000000,
    839.973083, 0.000000, 859.778015, 0.000000, 879.915039, 0.000000,
    899.709961, 0.000000,
};

// https://refractiveindex.info, public domain CC0:
// https://creativecommons.org/publicdomain/zero/1.0/

const Float GlassBK7_eta[] = {
    300, 1.5527702635739, 322, 1.5458699289209, 344, 1.5404466868331,
    366, 1.536090527917,  388, 1.53252773217,   410, 1.529568767224,
    432, 1.5270784291406, 454, 1.5249578457324, 476, 1.5231331738499,
    498, 1.5215482528369, 520, 1.5201596882463, 542, 1.5189334783109,
    564, 1.5178426478869, 586, 1.516865556749,  608, 1.5159846691816,
    630, 1.5151856452759, 652, 1.5144566604975, 674, 1.513787889767,
    696, 1.5131711117948, 718, 1.5125994024544, 740, 1.5120668948646,
    762, 1.5115685899969, 784, 1.5111002059336, 806, 1.5106580569705,
    828, 1.5102389559626, 850, 1.5098401349174, 872, 1.5094591800239,
    894, 1.5090939781792, 916, 1.5087426727363,
};

const Float GlassBAF10_eta[] = {
    350, 1.7126880848268, 371, 1.7044510025682, 393, 1.6978539633931,
    414, 1.6924597573902, 436, 1.6879747521657, 457, 1.6841935148947,
    479, 1.6809676313681, 500, 1.6781870617363, 522, 1.6757684467878,
    543, 1.6736474831891, 565, 1.6717737892968, 586, 1.6701073530462,
    608, 1.6686160168249, 629, 1.6672736605352, 651, 1.6660588657981,
    672, 1.6649539185393, 694, 1.6639440538738, 715, 1.6630168772865,
    737, 1.6621619159417, 758, 1.6613702672977, 780, 1.6606343213443,
    801, 1.6599475391478, 823, 1.6593042748862, 844, 1.6586996317841,
    866, 1.6581293446924, 887, 1.6575896837763, 909, 1.6570773750475,
};

const Float GlassFK51A_eta[] = {
    290, 1.5145777204082, 312, 1.5092112868865, 334, 1.5049961987453,
    356, 1.5016153970446, 378, 1.4988558885761, 400, 1.496569610433,
    422, 1.4946506898002, 444, 1.4930216011953, 466, 1.4916244098644,
    488, 1.49041505042,   511, 1.4893594837084, 533, 1.4884310526027,
    555, 1.4876086240083, 577, 1.486875258765,  599, 1.486217243501,
    621, 1.4856233753353, 643, 1.4850844262039, 665, 1.4845927367446,
    687, 1.484141904927,  709, 1.483726544853,  732, 1.4833420981287,
    754, 1.4829846850495, 776, 1.482650986233,  798, 1.4823381477539,
    820, 1.4820437045732, 842, 1.4817655183243, 864, 1.481501726448,
    886, 1.4812507003621, 908, 1.4810110108734,
};

const Float GlassLASF9_eta[] = {
    370, 1.9199725545705, 391, 1.9057858245373, 412, 1.8945401582481,
    433, 1.8854121949451, 455, 1.877863643024,  476, 1.8715257028176,
    497, 1.8661362648008, 519, 1.8615034773283, 540, 1.8574834752011,
    561, 1.8539661699122, 583, 1.8508658556,    604, 1.8481148099285,
    625, 1.8456588222442, 646, 1.8434539988324, 668, 1.8414644361915,
    689, 1.8396604975285, 710, 1.8380175167434, 732, 1.8365148106821,
    753, 1.8351349171703, 774, 1.8338630007484, 796, 1.8326863845545,
    817, 1.8315941782006, 838, 1.8305769794709, 859, 1.8296266333424,
    881, 1.8287360359155, 902, 1.8278989738228,
};

const Float GlassSF5_eta[] = {
    370, 1.7286549847245, 391, 1.7170151864402, 412, 1.7079037179421,
    433, 1.7005724270177, 455, 1.6945472844297, 476, 1.6895110487297,
    497, 1.685242265691,  519, 1.6815810964,    540, 1.678409006027,
    561, 1.6756360973958, 583, 1.6731928929908, 604, 1.6710248234743,
    625, 1.6690884260039, 646, 1.6673486579281, 668, 1.6657769585173,
    689, 1.6643498246044, 710, 1.6630477468358, 732, 1.6618544037398,
    753, 1.6607560432197, 774, 1.6597410023473, 796, 1.6587993305922,
    817, 1.6579224913632, 838, 1.6571031234995, 859, 1.6563348491305,
    881, 1.6556121177295, 902, 1.654930078671,
};

const Float GlassSF10_eta[] = {
    380, 1.7905788948419, 401, 1.7776074571692, 422, 1.7673620572474,
    443, 1.7590649148507, 464, 1.7522127524444, 486, 1.7464635698826,
    507, 1.741575877046,  528, 1.7373738218659, 549, 1.7337260730259,
    570, 1.7305324562829, 592, 1.7277151818026, 613, 1.7252129043714,
    634, 1.7229765939984, 655, 1.7209665988467, 676, 1.719150514229,
    698, 1.7175016091415, 719, 1.7159976462946, 740, 1.7146199848831,
    761, 1.7133528897994, 782, 1.7121829937648, 804, 1.7110988742233,
    825, 1.7100907173852, 846, 1.7091500491754, 867, 1.7082695180523,
    888, 1.7074427184169, 910, 1.7066640460471,
};

const Float GlassSF11_eta[] = {
    370, 1.8700216173234, 391, 1.8516255860581, 412, 1.8374707714715,
    433, 1.8262323798466, 455, 1.8170946940119, 476, 1.8095242343848,
    497, 1.803155581666,  519, 1.7977291183308, 540, 1.7930548640505,
    561, 1.7889903663666, 583, 1.7854266026774, 604, 1.7822786683156,
    625, 1.7794794394722, 646, 1.7769751487395, 668, 1.7747222267051,
    689, 1.7726850031375, 710, 1.770834004936,  732, 1.7691446766161,
    753, 1.7675964052635, 774, 1.7661717683505, 796, 1.764855947008,
    817, 1.7636362637211, 838, 1.7625018146862, 859, 1.7614431749629,
    881, 1.7604521601554, 902, 1.7595216323879,
};

pbrt::SpectrumHandle MakeSpectrumFromInterleaved(
    pstd::span<const Float> samples, bool normalize, Allocator alloc) {
    CHECK_EQ(0, samples.size() % 2);
    int n = samples.size() / 2;
    std::vector<Float> lambda(n), v(n);
    for (size_t i = 0; i < n; ++i) {
        lambda[i] = samples[2 * i];
        v[i] = samples[2 * i + 1];
        if (i > 0) CHECK_GT(lambda[i], lambda[i - 1]);
    }

    SpectrumHandle spec = alloc.new_object<pbrt::PiecewiseLinearSpectrum>(lambda, v, alloc);

    if (normalize)
        // Normalize to have luminance of 1.
        spec = alloc.new_object<pbrt::ScaledSpectrum>(1 / SpectrumToY(spec), spec);

    return spec;
}

}  // anonymous namespace

// Spectral Data Definitions
namespace SPDs {

#ifdef PBRT_HAVE_OPTIX
__device__ DenselySampledSpectrum *xGPU, *yGPU, *zGPU;
#endif
DenselySampledSpectrum *x, *y, *z;

namespace {

ConstantSpectrum *zero, *one;

SpectrumHandle illuma, illumd50, illumacesd60, illumd65;
SpectrumHandle illumf1, illumf2, illumf3, illumf4;
SpectrumHandle illumf5, illumf6, illumf7, illumf8;
SpectrumHandle illumf9, illumf10, illumf11, illumf12;

SpectrumHandle ageta, agk;
SpectrumHandle aleta, alk;
SpectrumHandle aueta, auk;
SpectrumHandle cueta, cuk;
SpectrumHandle mgoeta, mgok;
SpectrumHandle ti02eta, ti02k;
SpectrumHandle glassbk7eta, glassbaf10eta;
SpectrumHandle glassfk51aeta, glasslasf9eta;
SpectrumHandle glasssf5eta, glasssf10eta;
SpectrumHandle glasssf11eta;

std::map<std::string, SpectrumHandle> namedSPDs;

}

void Init(Allocator alloc) {
    zero = alloc.new_object<ConstantSpectrum>(0.);
    one = alloc.new_object<ConstantSpectrum>(1.);

    PiecewiseLinearSpectrum xpls(CIE_lambda, CIE_X);
    x = alloc.new_object<DenselySampledSpectrum>(&xpls, alloc);

    PiecewiseLinearSpectrum ypls(CIE_lambda, CIE_Y);
    y = alloc.new_object<DenselySampledSpectrum>(&ypls, alloc);

    PiecewiseLinearSpectrum zpls(CIE_lambda, CIE_Z);
    z = alloc.new_object<DenselySampledSpectrum>(&zpls, alloc);

#ifdef PBRT_HAVE_OPTIX
    CUDA_CHECK(cudaMemcpyToSymbol(xGPU, &x, sizeof(x)));
    CUDA_CHECK(cudaMemcpyToSymbol(yGPU, &y, sizeof(y)));
    CUDA_CHECK(cudaMemcpyToSymbol(zGPU, &z, sizeof(z)));
#endif

    illuma = MakeSpectrumFromInterleaved(CIE_Illum_A, true, alloc);
    illumd50 = MakeSpectrumFromInterleaved(CIE_Illum_D5000, true, alloc);
    illumacesd60 = MakeSpectrumFromInterleaved(ACES_Illum_D60, true, alloc);
    illumd65 = MakeSpectrumFromInterleaved(CIE_Illum_D6500, true, alloc);
    illumf1 = MakeSpectrumFromInterleaved(CIE_Illum_F1, true, alloc);
    illumf2 = MakeSpectrumFromInterleaved(CIE_Illum_F2, true, alloc);
    illumf3 = MakeSpectrumFromInterleaved(CIE_Illum_F3, true, alloc);
    illumf4 = MakeSpectrumFromInterleaved(CIE_Illum_F4, true, alloc);
    illumf5 = MakeSpectrumFromInterleaved(CIE_Illum_F5, true, alloc);
    illumf6 = MakeSpectrumFromInterleaved(CIE_Illum_F6, true, alloc);
    illumf7 = MakeSpectrumFromInterleaved(CIE_Illum_F7, true, alloc);
    illumf8 = MakeSpectrumFromInterleaved(CIE_Illum_F8, true, alloc);
    illumf9 = MakeSpectrumFromInterleaved(CIE_Illum_F9, true, alloc);
    illumf10 = MakeSpectrumFromInterleaved(CIE_Illum_F10, true, alloc);
    illumf11 = MakeSpectrumFromInterleaved(CIE_Illum_F11, true, alloc);
    illumf12 = MakeSpectrumFromInterleaved(CIE_Illum_F12, true, alloc);

    ageta = MakeSpectrumFromInterleaved(Ag_eta, false, alloc);
    agk = MakeSpectrumFromInterleaved(Ag_k, false, alloc);
    aleta = MakeSpectrumFromInterleaved(Al_eta, false, alloc);
    alk = MakeSpectrumFromInterleaved(Al_k, false, alloc);
    aueta = MakeSpectrumFromInterleaved(Au_eta, false, alloc);
    auk = MakeSpectrumFromInterleaved(Au_k, false, alloc);
    cueta = MakeSpectrumFromInterleaved(Cu_eta, false, alloc);
    cuk = MakeSpectrumFromInterleaved(Cu_k, false, alloc);
    mgoeta = MakeSpectrumFromInterleaved(MgO_eta, false, alloc);
    mgok = MakeSpectrumFromInterleaved(MgO_k, false, alloc);
    ti02eta = MakeSpectrumFromInterleaved(TiO2_eta, false, alloc);
    ti02k = MakeSpectrumFromInterleaved(TiO2_k, false, alloc);
    glassbk7eta = MakeSpectrumFromInterleaved(GlassBK7_eta, false, alloc);
    glassbaf10eta = MakeSpectrumFromInterleaved(GlassBAF10_eta, false, alloc);
    glassfk51aeta = MakeSpectrumFromInterleaved(GlassFK51A_eta, false, alloc);
    glasslasf9eta = MakeSpectrumFromInterleaved(GlassLASF9_eta, false, alloc);
    glasssf5eta = MakeSpectrumFromInterleaved(GlassSF5_eta, false, alloc);
    glasssf10eta = MakeSpectrumFromInterleaved(GlassSF10_eta, false, alloc);
    glasssf11eta = MakeSpectrumFromInterleaved(GlassSF11_eta, false, alloc);

    namedSPDs = {
                 {"glass-BK7", GlassBK7Eta()},       {"glass-BAF10", GlassBAF10Eta()},
                 {"glass-FK51A", GlassFK51AEta()},   {"glass-LASF9", GlassLASF9Eta()},
                 {"glass-F5", GlassSF5Eta()},        {"glass-F10", GlassSF10Eta()},
                 {"glass-F11", GlassSF11Eta()},

                 {"metal-Ag-eta", MetalAgEta()},     {"metal-Ag-k", MetalAgK()},
                 {"metal-Al-eta", MetalAlEta()},     {"metal-Al-k", MetalAlK()},
                 {"metal-Au-eta", MetalAuEta()},     {"metal-Au-k", MetalAuK()},
                 {"metal-Cu-eta", MetalCuEta()},     {"metal-Cu-k", MetalCuK()},
                 {"metal-MgO-eta", MetalMgOEta()},   {"metal-MgO-k", MetalMgOK()},
                 {"metal-TiO2-eta", MetalTiO2Eta()}, {"metal-TiO2-k", MetalTiO2K()},

                 {"stdillum-A", IllumA()},           {"stdillum-D50", IllumD50()},
                 {"stdillum-D65", IllumD65()},       {"stdillum-F1", IllumF1()},
                 {"stdillum-F2", IllumF2()},         {"stdillum-F3", IllumF3()},
                 {"stdillum-F4", IllumF4()},         {"stdillum-F5", IllumF5()},
                 {"stdillum-F6", IllumF6()},         {"stdillum-F7", IllumF7()},
                 {"stdillum-F8", IllumF8()},         {"stdillum-F9", IllumF9()},
                 {"stdillum-F10", IllumF10()},       {"stdillum-F11", IllumF11()},
                 {"stdillum-F12", IllumF12()},
    };
}

SpectrumHandle Zero() {
    return zero;
}

SpectrumHandle One() {
    return one;
}

SpectrumHandle IllumA() {
    return illuma;
}

SpectrumHandle IllumD50() {
    return illumd50;
}

SpectrumHandle IllumACESD60() {
    return illumacesd60;
}

SpectrumHandle IllumD65() {
    return illumd65;
}

SpectrumHandle IllumF1() {
    return illumf1;
}

SpectrumHandle IllumF2() {
    return illumf2;
}

SpectrumHandle IllumF3() {
    return illumf3;
}

SpectrumHandle IllumF4() {
    return illumf4;
}

SpectrumHandle IllumF5() {
    return illumf5;
}

SpectrumHandle IllumF6() {
    return illumf6;
}

SpectrumHandle IllumF7() {
    return illumf7;
}

SpectrumHandle IllumF8() {
    return illumf8;
}

SpectrumHandle IllumF9() {
    return illumf9;
}

SpectrumHandle IllumF10() {
    return illumf10;
}

SpectrumHandle IllumF11() {
    return illumf11;
}

SpectrumHandle IllumF12() {
    return illumf12;
}

SpectrumHandle MetalAgEta() {
    return ageta;
}

SpectrumHandle MetalAgK() {
    return agk;
}

SpectrumHandle MetalAlEta() {
    return aleta;
}

SpectrumHandle MetalAlK() {
    return alk;
}

SpectrumHandle MetalAuEta() {
    return aueta;
}

SpectrumHandle MetalAuK() {
    return auk;
}

SpectrumHandle MetalCuEta() {
    return cueta;
}

SpectrumHandle MetalCuK() {
    return cuk;
}

SpectrumHandle MetalMgOEta() {
    return mgoeta;
}

SpectrumHandle MetalMgOK() {
    return mgok;
}

SpectrumHandle MetalTiO2Eta() {
    return ti02eta;
}

SpectrumHandle MetalTiO2K() {
    return ti02k;
}

SpectrumHandle GlassBK7Eta() {
    return glassbk7eta;
}

SpectrumHandle GlassBAF10Eta() {
    return glassbaf10eta;
}

SpectrumHandle GlassFK51AEta() {
    return glassfk51aeta;
}

SpectrumHandle GlassLASF9Eta() {
    return glasslasf9eta;
}

SpectrumHandle GlassSF5Eta() {
    return glasssf5eta;
}

SpectrumHandle GlassSF10Eta() {
    return glasssf10eta;
}

SpectrumHandle GlassSF11Eta() {
    return glasssf11eta;
}

SpectrumHandle GetNamed(const std::string &name) {
    auto iter = namedSPDs.find(name);
    if (iter != namedSPDs.end()) return iter->second;
    return nullptr;
}

std::string FindMatchingNamed(SpectrumHandle s) {
    auto sampledLambdasMatch = [](SpectrumHandle a, SpectrumHandle b) {
        const Float wls[] = {380, 402, 455, 503, 579,
                             610, 660, 692, 702, 715.5};
        for (Float lambda : wls)
            if (a(lambda) != b(lambda)) return false;
        return true;
    };
    for (const auto &spd : namedSPDs) {
        if (sampledLambdasMatch(s, spd.second)) return spd.first;
    }
    return "";
}

}  // namespace SPDs

}  // namespace pbrt
