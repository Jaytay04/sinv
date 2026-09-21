#ifdef MODULE_ESP
#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES
#endif
#include <cmath>
#include <cstdint>
#include <cstring>

namespace hypersinv::esp::arc_raiders {

namespace {
constexpr float kPi = 3.14159265358979323846f;
float Rad(float degrees) {
    return degrees * kPi / 180.0f;
}
};

/*
 * Build a row-major view-projection from camera parameters.
 * View = inverse camera transform; projection = symmetric-FOV perspective
 * (vertical FOV in degrees, aspect = vw/vh). Result: vp * [x y z 1].
 */
void BuildViewProjectionMatrix(const double loc[3], const float rot[3],
                               float fov, float vw, float vh, float vp16[16]) {
    std::memset(vp16, 0, sizeof(float) * 16);
    if (fov <= 0.0f || fov >= 180.0f || vw <= 0.0f || vh <= 0.0f) {
        /* Keep identity projection so downstream code stays finite. */
        vp16[0] = 1.0f;
        vp16[5] = 1.0f;
        vp16[10] = 1.0f;
        vp16[15] = 1.0f;
        return;
    }
    const double p = Rad(rot[0]);
    const double y = Rad(rot[1]);
    const double r = Rad(rot[2]);
    const double sp = sin(p), cp = cos(p);
    const double sy = sin(y), cy = cos(y);
    const double sr = sin(r), cr = cos(r);
    const double rm[3][3] = {
        {cp * cy, cp * sy, sp},
        {sr * sp * cy - cr * sy, sr * sp * sy + cr * cy, -sr * cp},
        {cr * sp * cy + sr * sy, cr * sp * sy - sr * cy, cr * cp}
    };
    const float vm[4][4] = {
        {(float)rm[0][0], (float)rm[1][0], (float)rm[2][0], -(float)(rm[0][0]*loc[0] + rm[1][0]*loc[1] + rm[2][0]*loc[2])},
        {(float)rm[0][1], (float)rm[1][1], (float)rm[2][1], -(float)(rm[0][1]*loc[0] + rm[1][1]*loc[1] + rm[2][1]*loc[2])},
        {(float)rm[0][2], (float)rm[1][2], (float)rm[2][2], -(float)(rm[0][2]*loc[0] + rm[1][2]*loc[1] + rm[2][2]*loc[2])},
        {0.0f, 0.0f, 0.0f, 1.0f}
    };
    const double fov_rad = Rad(fov);
    const double aspect = vw / vh;
    const double th = tan(fov_rad / 2.0);
    const float pm[4][4] = {
        {1.0f / (float)(aspect * th), 0.0f, 0.0f, 0.0f},
        {0.0f, 1.0f / (float)th, 0.0f, 0.0f},
        {0.0f, 0.0f, 0.0f, 1.0f},
        {0.0f, 0.0f, 0.0f, 0.0f}
    };
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            double acc = 0.0;
            for (int k = 0; k < 4; ++k) {
                acc += vm[i][k] * pm[k][j];
            }
            vp16[i * 4 + j] = static_cast<float>(acc);
        }
    }
}

} // namespace hypersinv::esp::arc_raiders
#endif // MODULE_ESP
