#include "doctest.h"

#include "as3d/math.h"

using namespace as3d;

namespace {
bool approx(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) < eps; }
bool approx(const Vec3& a, const Vec3& b, float eps = 1e-4f) {
    return approx(a.x, b.x, eps) && approx(a.y, b.y, eps) && approx(a.z, b.z, eps);
}
} // namespace

TEST_CASE("Mat4 identity is a no-op") {
    Mat4 id = Mat4::identity();
    Vec3 p{1, 2, 3};
    CHECK(approx(transformPoint(id, p), p));
}

TEST_CASE("Mat4 multiply composes translation and rotation in the expected order") {
    Mat4 t = translation({5, 0, 0});
    Mat4 r = rotationZ(90.0f);
    // (t * r): rotate first, then translate -- rotating (1,0,0) by 90 degrees about Z
    // gives (0,1,0), then the translation shifts it to (5,1,0).
    Mat4 m = t * r;
    CHECK(approx(transformPoint(m, Vec3{1, 0, 0}), Vec3{5, 1, 0}));
}

TEST_CASE("inverseAffine round-trips translation * rotation * non-uniform scale") {
    Mat4 m = translation({3, -2, 7}) * rotationY(37.0f) * rotationX(12.0f) * scale({2, 3, 0.5f});
    Mat4 inv = inverseAffine(m);
    Vec3 points[] = {{0, 0, 0}, {1, 2, 3}, {-4, 5, -6}, {10, -10, 2.5f}};
    for (const Vec3& p : points) {
        Vec3 roundTripped = transformPoint(inv, transformPoint(m, p));
        CHECK(approx(roundTripped, p, 1e-3f));
    }
}

TEST_CASE("transpose is its own inverse operation") {
    Mat4 m = rotationX(20.0f) * rotationY(40.0f) * translation({1, 2, 3});
    Mat4 tt = transpose(transpose(m));
    for (int i = 0; i < 16; i++) CHECK(approx(tt.m[i], m.m[i]));
}

TEST_CASE("Mat3 inverse round-trips a non-orthonormal matrix") {
    Mat3 a;
    a.at(0, 0) = 2;
    a.at(1, 0) = 0.5f;
    a.at(2, 0) = 0;
    a.at(0, 1) = 0;
    a.at(1, 1) = 3;
    a.at(2, 1) = 1;
    a.at(0, 2) = 1;
    a.at(1, 2) = 0;
    a.at(2, 2) = 4;
    Mat3 inv = inverse(a);
    Mat3 shouldBeIdentity = a * inv;
    for (int c = 0; c < 3; c++)
        for (int r = 0; r < 3; r++) CHECK(approx(shouldBeIdentity.at(c, r), c == r ? 1.0f : 0.0f, 1e-3f));
}

TEST_CASE("anglesToAxis at zero angles matches Quake's identity basis") {
    Vec3 fwd, right, up;
    anglesToAxis({0, 0, 0}, fwd, right, up);
    CHECK(approx(fwd, Vec3{1, 0, 0}));
    // Documented quirk: `right` actually points to -Y here, not +Y.
    CHECK(approx(right, Vec3{0, -1, 0}));
    CHECK(approx(up, Vec3{0, 0, 1}));
}

TEST_CASE("anglesToAxis: yaw rotates forward in the XY plane") {
    Vec3 fwd, right, up;
    anglesToAxis({0, 90, 0}, fwd, right, up);
    CHECK(approx(fwd, Vec3{0, 1, 0}, 1e-3f));
    CHECK(approx(up, Vec3{0, 0, 1}, 1e-3f));
}

TEST_CASE("anglesToAxis: pitch tilts forward towards -Z (looking down is positive pitch)") {
    Vec3 fwd, right, up;
    anglesToAxis({90, 0, 0}, fwd, right, up);
    CHECK(approx(fwd, Vec3{0, 0, -1}, 1e-3f));
}

TEST_CASE("anglesToAxis produces an orthonormal frame at arbitrary angles") {
    Vec3 anglesToTry[] = {{0, 0, 0}, {30, 45, 10}, {-20, 200, 5}, {80, -130, 45}};
    for (const Vec3& angles : anglesToTry) {
        Vec3 fwd, right, up;
        anglesToAxis(angles, fwd, right, up);
        CHECK(approx(length(fwd), 1.0f, 1e-3f));
        CHECK(approx(length(right), 1.0f, 1e-3f));
        CHECK(approx(length(up), 1.0f, 1e-3f));
        CHECK(approx(dot(fwd, right), 0.0f, 1e-3f));
        CHECK(approx(dot(fwd, up), 0.0f, 1e-3f));
        CHECK(approx(dot(right, up), 0.0f, 1e-3f));
        // Documented quirk: this is a left-handed frame (forward x right == -up),
        // not a right-handed one.
        CHECK(approx(cross(fwd, right), -1.0f * up, 1e-3f));
    }
}

TEST_CASE("perspective maps the near/far planes to NDC z = -1/+1") {
    Mat4 p = perspective(90.0f, 1.0f, 1.0f, 10.0f);
    CHECK(approx(transformPoint(p, Vec3{0, 0, -1}).z, -1.0f, 1e-3f));
    CHECK(approx(transformPoint(p, Vec3{0, 0, -10}).z, 1.0f, 1e-3f));
}

TEST_CASE("ortho maps its box to the [-1,1]^3 NDC cube") {
    Mat4 o = ortho(-2, 2, -1, 1, 1, 10);
    CHECK(approx(transformPoint(o, Vec3{2, 1, -1}), Vec3{1, 1, -1}, 1e-3f));
    CHECK(approx(transformPoint(o, Vec3{-2, -1, -10}), Vec3{-1, -1, 1}, 1e-3f));
    CHECK(approx(transformPoint(o, Vec3{0, 0, -5.5f}), Vec3{0, 0, 0}, 1e-3f));
}

TEST_CASE("lookAt places the target on the camera's -Z axis") {
    Mat4 v = lookAt({0, 0, 5}, {0, 0, 0}, {0, 1, 0});
    CHECK(approx(transformPoint(v, Vec3{0, 0, 0}), Vec3{0, 0, -5}, 1e-3f));
    CHECK(approx(transformPoint(v, Vec3{0, 0, 5}), Vec3{0, 0, 0}, 1e-3f));
}

TEST_CASE("vec3_ma steps a point along a direction") {
    CHECK(approx(vec3_ma(Vec3{1, 1, 1}, Vec3{0, 1, 0}, 3.0f), Vec3{1, 4, 1}));
}
