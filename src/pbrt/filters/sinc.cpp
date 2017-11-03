
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


// filters/sinc.cpp*
#include <pbrt/filters/sinc.h>

#include <pbrt/util/math.h>
#include <pbrt/core/paramset.h>

namespace pbrt {

// Sinc Filter Method Definitions
Float LanczosSincFilter::Evaluate(const Point2f &p) const {
    return WindowedSinc(p.x, radius.x, tau) * WindowedSinc(p.y, radius.y, tau);
}

std::unique_ptr<LanczosSincFilter> CreateSincFilter(const ParamSet &ps) {
    Float xw = ps.GetOneFloat("xwidth", 4.);
    Float yw = ps.GetOneFloat("ywidth", 4.);
    Float tau = ps.GetOneFloat("tau", 3.f);
    return std::make_unique<LanczosSincFilter>(Vector2f(xw, yw), tau);
}

}  // namespace pbrt