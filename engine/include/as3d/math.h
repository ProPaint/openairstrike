// Small Quake-flavoured math library: vectors, column-major matrices matching GL's
// glUniformMatrix* layout, and the handful of builders every renderer needs
// (perspective/ortho/lookAt, TRS builders, and the classic id-Software AngleVectors).
//
// Header-only: every function here is `inline`, so there is no math.cpp to link.
//
// Conventions (read this before using anglesToAxis or the matrix builders):
//  - Matrices are column-major and stored as a flat `float m[16]` (Mat4) / `float m[9]`
//    (Mat3), laid out exactly as OpenGL/GLES expects for glUniformMatrix4fv(...,
//    transpose=GL_FALSE, ...): element (row r, col c) lives at m[c*4+r] (Mat4) or
//    m[c*3+r] (Mat3).
//  - Vectors are column vectors; a point is transformed as `M * v`. Composing
//    `A * B` means "apply B, then A" (standard GL convention).
//  - perspective()/ortho()/lookAt() follow the classic right-handed OpenGL convention:
//    view space looks down -Z, +X is right, +Y is up, and NDC z is in [-1, 1].
//  - anglesToAxis(angles, forward, right, up) reproduces id Software's original Quake
//    AngleVectors() *exactly*, quirks included. `angles` is (pitch, yaw, roll) in
//    degrees: yaw rotates around +Z (standard math sense, counter-clockwise seen from
//    +Z looking down), pitch rotates "look up/down" such that positive pitch looks
//    downward (matching Quake's mouse-look sign), roll banks around the forward axis.
//    At angles = (0,0,0): forward = (1,0,0), up = (0,0,1) -- this is Quake's Z-up,
//    X-forward world space, unrelated to the Y-up space used by perspective()/lookAt()
//    for rendering; converting between the two is a level/camera concern for later
//    work packages. NOTE (faithfully preserved quirk): the `right` vector produced
//    here actually points to the viewer's LEFT (forward x right == -up, not +up) --
//    this is a well-known sign quirk in id's original AngleVectors() that the rest of
//    Quake's code silently compensates for. Callers that want a true "strafe right"
//    vector should negate the result. The three vectors are still mutually orthogonal
//    unit vectors (an orthonormal, left-handed frame), which is what is tested.
#pragma once

#include <cmath>
#include <cstddef>

namespace as3d {

constexpr float kPi = 3.14159265358979323846f;

inline float degToRad(float deg) { return deg * (kPi / 180.0f); }
inline float radToDeg(float rad) { return rad * (180.0f / kPi); }

// ---------------------------------------------------------------------------
// Vec2
// ---------------------------------------------------------------------------
struct Vec2 {
    float x = 0.0f, y = 0.0f;
};

inline Vec2 operator+(const Vec2& a, const Vec2& b) { return {a.x + b.x, a.y + b.y}; }
inline Vec2 operator-(const Vec2& a, const Vec2& b) { return {a.x - b.x, a.y - b.y}; }
inline Vec2 operator-(const Vec2& a) { return {-a.x, -a.y}; }
inline Vec2 operator*(const Vec2& a, float s) { return {a.x * s, a.y * s}; }
inline Vec2 operator*(float s, const Vec2& a) { return a * s; }
inline float dot(const Vec2& a, const Vec2& b) { return a.x * b.x + a.y * b.y; }
inline float length(const Vec2& a) { return std::sqrt(dot(a, a)); }
inline Vec2 normalize(const Vec2& a) {
    float len = length(a);
    return len > 0.0f ? a * (1.0f / len) : Vec2{0, 0};
}

// ---------------------------------------------------------------------------
// Vec3
// ---------------------------------------------------------------------------
struct Vec3 {
    float x = 0.0f, y = 0.0f, z = 0.0f;
};

inline Vec3 operator+(const Vec3& a, const Vec3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 operator-(const Vec3& a, const Vec3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 operator-(const Vec3& a) { return {-a.x, -a.y, -a.z}; }
inline Vec3 operator*(const Vec3& a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline Vec3 operator*(float s, const Vec3& a) { return a * s; }
inline Vec3 operator*(const Vec3& a, const Vec3& b) { return {a.x * b.x, a.y * b.y, a.z * b.z}; }
inline float dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float length(const Vec3& a) { return std::sqrt(dot(a, a)); }
inline Vec3 normalize(const Vec3& a) {
    float len = length(a);
    return len > 0.0f ? a * (1.0f / len) : Vec3{0, 0, 0};
}
// a + b * s -- the classic Quake "multiply-add" used everywhere for stepping a point
// along a direction.
inline Vec3 vec3_ma(const Vec3& a, const Vec3& b, float s) { return a + b * s; }

// ---------------------------------------------------------------------------
// Vec4
// ---------------------------------------------------------------------------
struct Vec4 {
    float x = 0.0f, y = 0.0f, z = 0.0f, w = 0.0f;
};

inline Vec4 operator+(const Vec4& a, const Vec4& b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w};
}
inline Vec4 operator-(const Vec4& a, const Vec4& b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w};
}
inline Vec4 operator*(const Vec4& a, float s) { return {a.x * s, a.y * s, a.z * s, a.w * s}; }
inline Vec4 operator*(float s, const Vec4& a) { return a * s; }
inline float dot(const Vec4& a, const Vec4& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
}
inline float length(const Vec4& a) { return std::sqrt(dot(a, a)); }
inline Vec3 xyz(const Vec4& a) { return {a.x, a.y, a.z}; }

// ---------------------------------------------------------------------------
// Mat3 -- column-major 3x3, m[col*3+row]
// ---------------------------------------------------------------------------
struct Mat3 {
    float m[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};

    static Mat3 identity() { return Mat3{}; }
    float at(int col, int row) const { return m[col * 3 + row]; }
    float& at(int col, int row) { return m[col * 3 + row]; }
};

inline Mat3 operator*(const Mat3& a, const Mat3& b) {
    Mat3 r;
    for (int c = 0; c < 3; c++) {
        for (int row = 0; row < 3; row++) {
            float sum = 0.0f;
            for (int k = 0; k < 3; k++) sum += a.at(k, row) * b.at(c, k);
            r.at(c, row) = sum;
        }
    }
    return r;
}

inline Vec3 operator*(const Mat3& m, const Vec3& v) {
    return {m.at(0, 0) * v.x + m.at(1, 0) * v.y + m.at(2, 0) * v.z,
            m.at(0, 1) * v.x + m.at(1, 1) * v.y + m.at(2, 1) * v.z,
            m.at(0, 2) * v.x + m.at(1, 2) * v.y + m.at(2, 2) * v.z};
}

inline Mat3 transpose(const Mat3& a) {
    Mat3 r;
    for (int c = 0; c < 3; c++)
        for (int row = 0; row < 3; row++) r.at(row, c) = a.at(c, row);
    return r;
}

inline float determinant(const Mat3& a) {
    return a.at(0, 0) * (a.at(1, 1) * a.at(2, 2) - a.at(2, 1) * a.at(1, 2)) -
           a.at(1, 0) * (a.at(0, 1) * a.at(2, 2) - a.at(2, 1) * a.at(0, 2)) +
           a.at(2, 0) * (a.at(0, 1) * a.at(1, 2) - a.at(1, 1) * a.at(0, 2));
}

// General 3x3 inverse via the adjugate. Returns the identity if `a` is singular
// (determinant ~ 0), since callers (e.g. building a normal matrix) should not crash
// on a degenerate model transform.
inline Mat3 inverse(const Mat3& a) {
    float det = determinant(a);
    if (std::fabs(det) < 1e-20f) return Mat3::identity();
    float invDet = 1.0f / det;
    Mat3 r;
    r.at(0, 0) = (a.at(1, 1) * a.at(2, 2) - a.at(2, 1) * a.at(1, 2)) * invDet;
    r.at(1, 0) = (a.at(2, 0) * a.at(1, 2) - a.at(1, 0) * a.at(2, 2)) * invDet;
    r.at(2, 0) = (a.at(1, 0) * a.at(2, 1) - a.at(2, 0) * a.at(1, 1)) * invDet;
    r.at(0, 1) = (a.at(2, 1) * a.at(0, 2) - a.at(0, 1) * a.at(2, 2)) * invDet;
    r.at(1, 1) = (a.at(0, 0) * a.at(2, 2) - a.at(2, 0) * a.at(0, 2)) * invDet;
    r.at(2, 1) = (a.at(2, 0) * a.at(0, 1) - a.at(0, 0) * a.at(2, 1)) * invDet;
    r.at(0, 2) = (a.at(0, 1) * a.at(1, 2) - a.at(1, 1) * a.at(0, 2)) * invDet;
    r.at(1, 2) = (a.at(1, 0) * a.at(0, 2) - a.at(0, 0) * a.at(1, 2)) * invDet;
    r.at(2, 2) = (a.at(0, 0) * a.at(1, 1) - a.at(1, 0) * a.at(0, 1)) * invDet;
    return r;
}

// ---------------------------------------------------------------------------
// Mat4 -- column-major 4x4, m[col*4+row]
// ---------------------------------------------------------------------------
struct Mat4 {
    float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

    static Mat4 identity() { return Mat4{}; }
    float at(int col, int row) const { return m[col * 4 + row]; }
    float& at(int col, int row) { return m[col * 4 + row]; }
};

inline Mat4 operator*(const Mat4& a, const Mat4& b) {
    Mat4 r;
    for (int c = 0; c < 4; c++) {
        for (int row = 0; row < 4; row++) {
            float sum = 0.0f;
            for (int k = 0; k < 4; k++) sum += a.at(k, row) * b.at(c, k);
            r.at(c, row) = sum;
        }
    }
    return r;
}

inline Vec4 operator*(const Mat4& m, const Vec4& v) {
    return {m.at(0, 0) * v.x + m.at(1, 0) * v.y + m.at(2, 0) * v.z + m.at(3, 0) * v.w,
            m.at(0, 1) * v.x + m.at(1, 1) * v.y + m.at(2, 1) * v.z + m.at(3, 1) * v.w,
            m.at(0, 2) * v.x + m.at(1, 2) * v.y + m.at(2, 2) * v.z + m.at(3, 2) * v.w,
            m.at(0, 3) * v.x + m.at(1, 3) * v.y + m.at(2, 3) * v.z + m.at(3, 3) * v.w};
}

inline Mat4 transpose(const Mat4& a) {
    Mat4 r;
    for (int c = 0; c < 4; c++)
        for (int row = 0; row < 4; row++) r.at(row, c) = a.at(c, row);
    return r;
}

inline Mat3 mat3FromMat4(const Mat4& a) {
    Mat3 r;
    for (int c = 0; c < 3; c++)
        for (int row = 0; row < 3; row++) r.at(c, row) = a.at(c, row);
    return r;
}

// Transforms a point (w=1) through `m` and applies the perspective divide (a no-op
// for affine matrices, where w stays 1).
inline Vec3 transformPoint(const Mat4& m, const Vec3& p) {
    Vec4 r = m * Vec4{p.x, p.y, p.z, 1.0f};
    if (std::fabs(r.w) > 1e-20f && r.w != 1.0f) return {r.x / r.w, r.y / r.w, r.z / r.w};
    return {r.x, r.y, r.z};
}

// Transforms a direction (w=0): ignores translation, does not renormalize.
inline Vec3 transformDirection(const Mat4& m, const Vec3& d) {
    Vec4 r = m * Vec4{d.x, d.y, d.z, 0.0f};
    return {r.x, r.y, r.z};
}

// Fast inverse for an affine matrix (rotation * scale in the upper-left 3x3, plus a
// translation, and (0,0,0,1) as the last row -- i.e. any matrix built purely from
// translation()/rotation*()/scale() and their products, including non-uniform scale).
// Uses the general 3x3 inverse above rather than assuming orthonormality, so it also
// round-trips scaled matrices; it does NOT handle a matrix with real projective terms
// in the last row (use a general inverse for that, not needed by this engine yet).
inline Mat4 inverseAffine(const Mat4& a) {
    Mat3 upper = mat3FromMat4(a);
    Mat3 inv3 = inverse(upper);
    Vec3 t{a.at(3, 0), a.at(3, 1), a.at(3, 2)};
    Vec3 invT = -(inv3 * t);
    Mat4 r;
    for (int c = 0; c < 3; c++)
        for (int row = 0; row < 3; row++) r.at(c, row) = inv3.at(c, row);
    r.at(3, 0) = invT.x;
    r.at(3, 1) = invT.y;
    r.at(3, 2) = invT.z;
    r.at(0, 3) = r.at(1, 3) = r.at(2, 3) = 0.0f;
    r.at(3, 3) = 1.0f;
    return r;
}

// ---------------------------------------------------------------------------
// Builders
// ---------------------------------------------------------------------------
inline Mat4 translation(const Vec3& t) {
    Mat4 r;
    r.at(3, 0) = t.x;
    r.at(3, 1) = t.y;
    r.at(3, 2) = t.z;
    return r;
}

inline Mat4 scale(const Vec3& s) {
    Mat4 r;
    r.at(0, 0) = s.x;
    r.at(1, 1) = s.y;
    r.at(2, 2) = s.z;
    return r;
}

// Rotation about the X axis, right-handed, `degrees` counter-clockwise looking from
// +X towards the origin.
inline Mat4 rotationX(float degrees) {
    float rad = degToRad(degrees);
    float c = std::cos(rad), s = std::sin(rad);
    Mat4 r;
    r.at(1, 1) = c;
    r.at(2, 1) = -s;
    r.at(1, 2) = s;
    r.at(2, 2) = c;
    return r;
}

inline Mat4 rotationY(float degrees) {
    float rad = degToRad(degrees);
    float c = std::cos(rad), s = std::sin(rad);
    Mat4 r;
    r.at(0, 0) = c;
    r.at(2, 0) = s;
    r.at(0, 2) = -s;
    r.at(2, 2) = c;
    return r;
}

inline Mat4 rotationZ(float degrees) {
    float rad = degToRad(degrees);
    float c = std::cos(rad), s = std::sin(rad);
    Mat4 r;
    r.at(0, 0) = c;
    r.at(1, 0) = -s;
    r.at(0, 1) = s;
    r.at(1, 1) = c;
    return r;
}

// Right-handed OpenGL perspective projection. `fovYDegrees` is the full vertical
// field of view. Maps view-space z in [-near, -far] to NDC z in [-1, 1].
inline Mat4 perspective(float fovYDegrees, float aspect, float znear, float zfar) {
    float f = 1.0f / std::tan(degToRad(fovYDegrees) * 0.5f);
    Mat4 r;
    for (int i = 0; i < 16; i++) r.m[i] = 0.0f;
    r.at(0, 0) = f / aspect;
    r.at(1, 1) = f;
    r.at(2, 2) = (zfar + znear) / (znear - zfar);
    r.at(3, 2) = (2.0f * zfar * znear) / (znear - zfar);
    r.at(2, 3) = -1.0f;
    return r;
}

// Right-handed OpenGL orthographic projection.
inline Mat4 ortho(float left, float right, float bottom, float top, float znear, float zfar) {
    Mat4 r;
    r.at(0, 0) = 2.0f / (right - left);
    r.at(1, 1) = 2.0f / (top - bottom);
    r.at(2, 2) = -2.0f / (zfar - znear);
    r.at(3, 0) = -(right + left) / (right - left);
    r.at(3, 1) = -(top + bottom) / (top - bottom);
    r.at(3, 2) = -(zfar + znear) / (zfar - znear);
    return r;
}

// Right-handed OpenGL view matrix: camera at `eye` looking towards `center`, `up`
// need not be unit length or exactly perpendicular to the view direction.
inline Mat4 lookAt(const Vec3& eye, const Vec3& center, const Vec3& up) {
    Vec3 f = normalize(center - eye);
    Vec3 s = normalize(cross(f, up));
    Vec3 u = cross(s, f);
    Mat4 r;
    r.at(0, 0) = s.x;
    r.at(1, 0) = s.y;
    r.at(2, 0) = s.z;
    r.at(0, 1) = u.x;
    r.at(1, 1) = u.y;
    r.at(2, 1) = u.z;
    r.at(0, 2) = -f.x;
    r.at(1, 2) = -f.y;
    r.at(2, 2) = -f.z;
    r.at(3, 0) = -dot(s, eye);
    r.at(3, 1) = -dot(u, eye);
    r.at(3, 2) = dot(f, eye);
    return r;
}

// Reproduces id Software's original Quake AngleVectors() exactly. See the file-level
// comment for the coordinate convention and the (preserved) `right`-points-left quirk.
inline void anglesToAxis(const Vec3& anglesDegrees, Vec3& forward, Vec3& right, Vec3& up) {
    float pitch = degToRad(anglesDegrees.x);
    float yaw = degToRad(anglesDegrees.y);
    float roll = degToRad(anglesDegrees.z);

    float sy = std::sin(yaw), cy = std::cos(yaw);
    float sp = std::sin(pitch), cp = std::cos(pitch);
    float sr = std::sin(roll), cr = std::cos(roll);

    forward = {cp * cy, cp * sy, -sp};
    right = {-sr * sp * cy + -cr * -sy, -sr * sp * sy + -cr * cy, -sr * cp};
    up = {cr * sp * cy + -sr * -sy, cr * sp * sy + -sr * cy, cr * cp};
}

} // namespace as3d
