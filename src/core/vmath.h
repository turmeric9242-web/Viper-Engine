#ifndef VIPER_VMATH_H
#define VIPER_VMATH_H
#include <math.h>
typedef struct { float x, y, z; } Vec3;
typedef struct { float m[16]; } Mat4; /* column-major: m[col*4+row] */
#define VIPER_PI 3.14159265358979f
#define DEG2RAD(d) ((d) * VIPER_PI / 180.0f)

static inline Vec3 v3(float x, float y, float z) { Vec3 v = {x, y, z}; return v; }
static inline Vec3 v3_add(Vec3 a, Vec3 b) { return v3(a.x + b.x, a.y + b.y, a.z + b.z); }
static inline Vec3 v3_sub(Vec3 a, Vec3 b) { return v3(a.x - b.x, a.y - b.y, a.z - b.z); }
static inline Vec3 v3_mul(Vec3 a, float s) { return v3(a.x * s, a.y * s, a.z * s); }
static inline float v3_dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline Vec3 v3_cross(Vec3 a, Vec3 b) {
    return v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}
static inline float v3_len(Vec3 a) { return sqrtf(v3_dot(a, a)); }
static inline Vec3 v3_norm(Vec3 a) {
    float l = v3_len(a);
    return l > 1e-8f ? v3_mul(a, 1.0f / l) : v3(0, 0, 0);
}

static inline Mat4 m4_identity(void) {
    Mat4 r = {{0}};
    r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
    return r;
}
static inline Mat4 m4_mul(Mat4 a, Mat4 b) {
    Mat4 r;
    for (int c = 0; c < 4; c++)
        for (int row = 0; row < 4; row++) {
            float s = 0;
            for (int k = 0; k < 4; k++) s += a.m[k * 4 + row] * b.m[c * 4 + k];
            r.m[c * 4 + row] = s;
        }
    return r;
}
static inline Mat4 m4_translate(Vec3 t) {
    Mat4 r = m4_identity();
    r.m[12] = t.x; r.m[13] = t.y; r.m[14] = t.z;
    return r;
}
static inline Mat4 m4_scale(Vec3 s) {
    Mat4 r = m4_identity();
    r.m[0] = s.x; r.m[5] = s.y; r.m[10] = s.z;
    return r;
}
static inline Mat4 m4_rot_y(float a) {
    Mat4 r = m4_identity(); float c = cosf(a), s = sinf(a);
    r.m[0] = c; r.m[2] = -s; r.m[8] = s; r.m[10] = c;
    return r;
}
static inline Mat4 m4_rot_x(float a) {
    Mat4 r = m4_identity(); float c = cosf(a), s = sinf(a);
    r.m[5] = c; r.m[6] = s; r.m[9] = -s; r.m[10] = c;
    return r;
}
static inline Mat4 m4_rot_z(float a) {
    Mat4 r = m4_identity(); float c = cosf(a), s = sinf(a);
    r.m[0] = c; r.m[1] = s; r.m[4] = -s; r.m[5] = c;
    return r;
}
static inline Mat4 m4_perspective(float fovy, float aspect, float n, float f) {
    Mat4 r = {{0}}; float t = 1.0f / tanf(fovy * 0.5f);
    r.m[0] = t / aspect; r.m[5] = t;
    r.m[10] = (f + n) / (n - f); r.m[11] = -1.0f;
    r.m[14] = (2.0f * f * n) / (n - f);
    return r;
}
static inline Mat4 m4_look_at(Vec3 eye, Vec3 center, Vec3 up) {
    Vec3 f = v3_norm(v3_sub(center, eye));
    Vec3 s = v3_norm(v3_cross(f, up));
    Vec3 u = v3_cross(s, f);
    Mat4 r = m4_identity();
    r.m[0] = s.x; r.m[4] = s.y; r.m[8] = s.z;
    r.m[1] = u.x; r.m[5] = u.y; r.m[9] = u.z;
    r.m[2] = -f.x; r.m[6] = -f.y; r.m[10] = -f.z;
    r.m[12] = -v3_dot(s, eye); r.m[13] = -v3_dot(u, eye); r.m[14] = v3_dot(f, eye);
    return r;
}
#endif
