#pragma once

#define GT_COMMON_VECTOR_OP(TElement, ELEMENT_COUNT) \
Vector() { memset(elements, 0x0, sizeof(TElement) * ELEMENT_COUNT); } \
explicit Vector(TElement v) { for(size_t i = 0; i < ELEMENT_COUNT; ++i) { elements[i] = v; } } \
explicit operator TElement* () { return elements; } \
Vector(const Vector& other) = default; \
Vector& operator = (const Vector& other) = default; \
TElement& operator [] (size_t index) { return elements[index]; } \
TElement operator [] (size_t index) const { return elements[index]; } \
Vector& operator += (const Vector& other) { return *this = (*this + other); } \
Vector& operator -= (const Vector& other) { return *this = (*this - other); } \
Vector& operator *= (const Vector& other) { return *this = (*this * other); } \
Vector& operator /= (const Vector& other) { return *this = (*this / other); } \
Vector& operator += (TElement v) { return *this = (*this + v); } \
Vector& operator -= (TElement v) { return *this = (*this - v); } \
Vector& operator *= (TElement v) { return *this = (*this * v); } \
Vector& operator /= (TElement v) { return *this = (*this / v); } \

#include <math.h>

namespace fnd
{
    namespace math
    {

        static float Sqrt(float n)
        {
            return sqrtf(n);
        }
        static double Sqrt(double n)
        {
            return sqrt(n);
        }

        static float Sin(float n)
        {
            return sinf(n);
        }

        static float Cos(float n)
        {
            return cosf(n);
        }

        static double Sin(double n)
        {
            return sin(n);
        }

        static double Cos(double n)
        {
            return cos(n);
        }
    
        template <class TElement, size_t ELEMENT_COUNT>
        struct Vector
        {
            TElement elements[ELEMENT_COUNT];

            GT_COMMON_VECTOR_OP(TElement, ELEMENT_COUNT)
        };


        template <class TElement>
        struct Vector<TElement, 2>
        {
            union {
                TElement elements[2];
                struct { TElement x; TElement y; };
                struct { TElement r; TElement g; };
            };
            GT_COMMON_VECTOR_OP(TElement, 2)

            Vector(TElement a, TElement b) : x(a), y(b) {}
        };

        template <class TElement>
        struct Vector<TElement, 3>
        {
            union {
                TElement elements[3];
                struct { TElement x, y, z; };
                struct { TElement r, g, b; };
                Vector<TElement, 2> xy;
                Vector<TElement, 2> rg; 
            };

            GT_COMMON_VECTOR_OP(TElement, 3)

            Vector(Vector<TElement, 2> ab, TElement c) : xy(ab), z(c) {}
            Vector(TElement a, TElement b, TElement c) : x(a), y(b), z(c) {}
        };

        template <class TElement>
        struct Vector<TElement, 4>
        {
            union {
                TElement elements[4];
                struct { TElement x, y, z, w; };
                struct { TElement r, g, b, a; };
                Vector<TElement, 3> xyz;
                Vector<TElement, 2> xy;
                Vector<TElement, 3> rgb;
                Vector<TElement, 2> rg;
            };
            GT_COMMON_VECTOR_OP(TElement, 4)

            Vector(Vector<TElement, 3> abc, TElement d) : xyz(abc), w(d) {}
            Vector(TElement a, TElement b, TElement c, TElement d) : x(a), y(b), z(c), w(d) {}
        };


        template<class TElement, size_t ELEMENT_COUNT>
        Vector<TElement, ELEMENT_COUNT> Add(const Vector<TElement, ELEMENT_COUNT>& a, const Vector<TElement, ELEMENT_COUNT>& b)
        {
            Vector<TElement, ELEMENT_COUNT> result(a);
            for (size_t i = 0; i < ELEMENT_COUNT; ++i) { result[i] += b[i]; };
            return result;
        }

        template<class TElement, size_t ELEMENT_COUNT>
        Vector<TElement, ELEMENT_COUNT> Subtract(const Vector<TElement, ELEMENT_COUNT>& a, const Vector<TElement, ELEMENT_COUNT>& b)
        {
            Vector<TElement, ELEMENT_COUNT> result(a);
            for (size_t i = 0; i < ELEMENT_COUNT; ++i) { result[i] -= b[i]; };
            return result;
        }

        template<class TElement, size_t ELEMENT_COUNT>
        Vector<TElement, ELEMENT_COUNT> Multiply(const Vector<TElement, ELEMENT_COUNT>& a, const Vector<TElement, ELEMENT_COUNT>& b)
        {
            Vector<TElement, ELEMENT_COUNT> result(a);
            for (size_t i = 0; i < ELEMENT_COUNT; ++i) { result[i] *= b[i]; };
            return result;  
        }

        template<class TElement, size_t ELEMENT_COUNT>
        Vector<TElement, ELEMENT_COUNT> Divide(const Vector<TElement, ELEMENT_COUNT>& a, const Vector<TElement, ELEMENT_COUNT>& b)
        {
            Vector<TElement, ELEMENT_COUNT> result(a);
            for (size_t i = 0; i < ELEMENT_COUNT; ++i) { result[i] /= b[i]; };
            return result;
        }

        template<class TElement, size_t ELEMENT_COUNT>
        Vector<TElement, ELEMENT_COUNT> Add(const Vector<TElement, ELEMENT_COUNT>& a, TElement v)
        {
            Vector<TElement, ELEMENT_COUNT> result(a);
            for (size_t i = 0; i < ELEMENT_COUNT; ++i) { result[i] += v; };
            return result;
        }

        template<class TElement, size_t ELEMENT_COUNT>
        Vector<TElement, ELEMENT_COUNT> Subtract(const Vector<TElement, ELEMENT_COUNT>& a, TElement v)
        {
            Vector<TElement, ELEMENT_COUNT> result(a);
            for (size_t i = 0; i < ELEMENT_COUNT; ++i) { result[i] -= v; };
            return result;
        }

        template<class TElement, size_t ELEMENT_COUNT>
        Vector<TElement, ELEMENT_COUNT> Multiply(const Vector<TElement, ELEMENT_COUNT>& a, TElement v)
        {
            Vector<TElement, ELEMENT_COUNT> result(a);
            for (size_t i = 0; i < ELEMENT_COUNT; ++i) { result[i] *= v; };
            return result;
        }

        template<class TElement, size_t ELEMENT_COUNT>
        Vector<TElement, ELEMENT_COUNT> Divide(const Vector<TElement, ELEMENT_COUNT>& a, TElement v)
        {
            Vector<TElement, ELEMENT_COUNT> result(a);
            for (size_t i = 0; i < ELEMENT_COUNT; ++i) { result[i] /= v; };
            return result;
        }

        
        template <class TElement, size_t ELEMENT_COUNT>   // @TODO static_assert is ugly solution, really needs concepts or whatever
        TElement Dot(const Vector<TElement, ELEMENT_COUNT>& a, const Vector<TElement, ELEMENT_COUNT>& b) { static_assert(false, "Only float and double supported as element types") }


        template <size_t ELEMENT_COUNT>
        float Dot(const Vector<float, ELEMENT_COUNT>& a, const Vector<float, ELEMENT_COUNT>& b)
        {
            float result = 0.0f;
            for (size_t i = 0; i < ELEMENT_COUNT; ++i) { result += a[i] * b[i]; }
            return result;
        }

        template <size_t ELEMENT_COUNT>
        double Dot(const Vector<double, ELEMENT_COUNT>& a, const Vector<double, ELEMENT_COUNT>& b)
        {
            double result = 0.0f;
            for (size_t i = 0; i < ELEMENT_COUNT; ++i) { result += a[i] * b[i]; }
            return result;
        }

        template <class TElement, size_t ELEMENT_COUNT>
        TElement Length(const Vector<TElement, ELEMENT_COUNT>& v)
        {
            return Sqrt(Dot(v, v));
        }

        template <class TElement, size_t ELEMENT_COUNT>
        Vector<TElement, ELEMENT_COUNT> Normalize(const Vector<TElement, ELEMENT_COUNT>& v)
        {
            Vector<TElement, ELEMENT_COUNT> result(v);
            result /= Length(v);
            return result;
        }
       
        
        template <class TElement, size_t ELEMENT_COUNT>
        Vector<TElement, ELEMENT_COUNT> operator + (const Vector<TElement, ELEMENT_COUNT>& a, const Vector<TElement, ELEMENT_COUNT>& b)
        {
            return Add(a, b);
        }

        template <class TElement, size_t ELEMENT_COUNT>
        Vector<TElement, ELEMENT_COUNT> operator - (const Vector<TElement, ELEMENT_COUNT>& a, const Vector<TElement, ELEMENT_COUNT>& b)
        {
            return Subtract(a, b);
        }

        template <class TElement, size_t ELEMENT_COUNT>
        Vector<TElement, ELEMENT_COUNT> operator * (const Vector<TElement, ELEMENT_COUNT>& a, const Vector<TElement, ELEMENT_COUNT>& b)
        {
            return Multiply(a, b);
        }

        template <class TElement, size_t ELEMENT_COUNT>
        Vector<TElement, ELEMENT_COUNT> operator / (const Vector<TElement, ELEMENT_COUNT>& a, const Vector<TElement, ELEMENT_COUNT>& b)
        {
            return Divide(a, b);
        }

        template <class TElement, size_t ELEMENT_COUNT>
        Vector<TElement, ELEMENT_COUNT> operator + (const Vector<TElement, ELEMENT_COUNT>& a, TElement v)
        {
            return Add(a, v);
        }

        template <class TElement, size_t ELEMENT_COUNT>
        Vector<TElement, ELEMENT_COUNT> operator - (const Vector<TElement, ELEMENT_COUNT>& a, TElement v)
        {
            return Substract(a, v);
        }

        template <class TElement, size_t ELEMENT_COUNT>
        Vector<TElement, ELEMENT_COUNT> operator * (const Vector<TElement, ELEMENT_COUNT>& a, TElement v)
        {
            return Multiply(a, v);
        }

        template <class TElement, size_t ELEMENT_COUNT>
        Vector<TElement, ELEMENT_COUNT> operator / (const Vector<TElement, ELEMENT_COUNT>& a, TElement v)
        {
            return Divide(a, v);
        }


        typedef Vector<float, 2> float2;
        typedef Vector<float, 3> float3;
        typedef Vector<float, 4> float4;

        typedef Vector<double, 2> double2;
        typedef Vector<double, 3> double3;
        typedef Vector<double, 4> double4;

        typedef Vector<int, 2> int2;
        typedef Vector<int, 3> int3;
        typedef Vector<int, 4> int4;
    }
}



#include <cstring>
#undef near
#undef far

namespace util
{
    static bool Inverse4x4FloatMatrixCM(float* m, float* invOut)
    {
        int i;
        float det;
        float inv[16];

        inv[0] = m[5] * m[10] * m[15] -
            m[5] * m[11] * m[14] -
            m[9] * m[6] * m[15] +
            m[9] * m[7] * m[14] +
            m[13] * m[6] * m[11] -
            m[13] * m[7] * m[10];

        inv[4] = -m[4] * m[10] * m[15] +
            m[4] * m[11] * m[14] +
            m[8] * m[6] * m[15] -
            m[8] * m[7] * m[14] -
            m[12] * m[6] * m[11] +
            m[12] * m[7] * m[10];

        inv[8] = m[4] * m[9] * m[15] -
            m[4] * m[11] * m[13] -
            m[8] * m[5] * m[15] +
            m[8] * m[7] * m[13] +
            m[12] * m[5] * m[11] -
            m[12] * m[7] * m[9];

        inv[12] = -m[4] * m[9] * m[14] +
            m[4] * m[10] * m[13] +
            m[8] * m[5] * m[14] -
            m[8] * m[6] * m[13] -
            m[12] * m[5] * m[10] +
            m[12] * m[6] * m[9];

        inv[1] = -m[1] * m[10] * m[15] +
            m[1] * m[11] * m[14] +
            m[9] * m[2] * m[15] -
            m[9] * m[3] * m[14] -
            m[13] * m[2] * m[11] +
            m[13] * m[3] * m[10];

        inv[5] = m[0] * m[10] * m[15] -
            m[0] * m[11] * m[14] -
            m[8] * m[2] * m[15] +
            m[8] * m[3] * m[14] +
            m[12] * m[2] * m[11] -
            m[12] * m[3] * m[10];

        inv[9] = -m[0] * m[9] * m[15] +
            m[0] * m[11] * m[13] +
            m[8] * m[1] * m[15] -
            m[8] * m[3] * m[13] -
            m[12] * m[1] * m[11] +
            m[12] * m[3] * m[9];

        inv[13] = m[0] * m[9] * m[14] -
            m[0] * m[10] * m[13] -
            m[8] * m[1] * m[14] +
            m[8] * m[2] * m[13] +
            m[12] * m[1] * m[10] -
            m[12] * m[2] * m[9];

        inv[2] = m[1] * m[6] * m[15] -
            m[1] * m[7] * m[14] -
            m[5] * m[2] * m[15] +
            m[5] * m[3] * m[14] +
            m[13] * m[2] * m[7] -
            m[13] * m[3] * m[6];

        inv[6] = -m[0] * m[6] * m[15] +
            m[0] * m[7] * m[14] +
            m[4] * m[2] * m[15] -
            m[4] * m[3] * m[14] -
            m[12] * m[2] * m[7] +
            m[12] * m[3] * m[6];

        inv[10] = m[0] * m[5] * m[15] -
            m[0] * m[7] * m[13] -
            m[4] * m[1] * m[15] +
            m[4] * m[3] * m[13] +
            m[12] * m[1] * m[7] -
            m[12] * m[3] * m[5];

        inv[14] = -m[0] * m[5] * m[14] +
            m[0] * m[6] * m[13] +
            m[4] * m[1] * m[14] -
            m[4] * m[2] * m[13] -
            m[12] * m[1] * m[6] +
            m[12] * m[2] * m[5];

        inv[3] = -m[1] * m[6] * m[11] +
            m[1] * m[7] * m[10] +
            m[5] * m[2] * m[11] -
            m[5] * m[3] * m[10] -
            m[9] * m[2] * m[7] +
            m[9] * m[3] * m[6];

        inv[7] = m[0] * m[6] * m[11] -
            m[0] * m[7] * m[10] -
            m[4] * m[2] * m[11] +
            m[4] * m[3] * m[10] +
            m[8] * m[2] * m[7] -
            m[8] * m[3] * m[6];

        inv[11] = -m[0] * m[5] * m[11] +
            m[0] * m[7] * m[9] +
            m[4] * m[1] * m[11] -
            m[4] * m[3] * m[9] -
            m[8] * m[1] * m[7] +
            m[8] * m[3] * m[5];

        inv[15] = m[0] * m[5] * m[10] -
            m[0] * m[6] * m[9] -
            m[4] * m[1] * m[10] +
            m[4] * m[2] * m[9] +
            m[8] * m[1] * m[6] -
            m[8] * m[2] * m[5];

        det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];

        if (det == 0)
            return false;

        det = 1.0f / det;

        for (i = 0; i < 16; i++)
            invOut[i] = inv[i] * det;

        return true;
    }



    static void Copy4x4FloatMatrixCM(float* matFrom, float* matTo)
    {
        memcpy(matTo, matFrom, sizeof(float) * 16);
    }

    static float Get4x4FloatMatrixValueCM(float* mat, int column, int row)
    {
        int index = 4 * column + row;
        return mat[index];
    }

    static void Set4x4FloatMatrixValueCM(float* mat, int column, int row, float value)
    {
        int index = 4 * column + row;
        mat[index] = value;
    }

    static fnd::math::float3 TransformPositionCM(const fnd::math::float3& pos, float* mat)
    {
        fnd::math::float4 vec4(pos, 1.0f);
        fnd::math::float4 result;
        for (int i = 0; i < 4; ++i) {
            float accum = 0.0f;
            for (int j = 0; j < 4; ++j) {
                float x = Get4x4FloatMatrixValueCM(mat, j, i);
                accum += x * vec4[j];
            }
            result[i] = accum;
        }
        return result.xyz;
    }

    static fnd::math::float3 TransformDirectionCM(const fnd::math::float3& dir, float* mat)
    {
        fnd::math::float4 vec4(dir, 0.0f);
        fnd::math::float4 result;
        for (int i = 0; i < 4; ++i) {
            float accum = 0.0f;
            for (int j = 0; j < 4; ++j) {
                float x = Get4x4FloatMatrixValueCM(mat, j, i);
                accum += x * vec4[j];
            }
            result[i] = accum;
        }
        return result.xyz;
    }

    static void Make4x4FloatMatrixIdentity(float* mat)
    {
        memset(mat, 0x0, sizeof(float) * 16);
        for (int i = 0; i < 4; ++i) {
            Set4x4FloatMatrixValueCM(mat, i, i, 1.0f);
        }
    }


    /*void Make4x4FloatProjectionMatrixCMLH(float* mat, float fovInRadians, float aspect, float near, float far)
    {
    Make4x4FloatMatrixIdentity(mat);

    float tanHalfFovy = tanf(fovInRadians / 2.0f);

    Set4x4FloatMatrixValueCM(mat, 0, 0, 1.0f / (aspect * tanHalfFovy));
    Set4x4FloatMatrixValueCM(mat, 1, 1, 1.0f / tanHalfFovy);
    Set4x4FloatMatrixValueCM(mat, 2, 3, 1.0f);

    Set4x4FloatMatrixValueCM(mat, 2, 2, (far * near) / (far - near));
    Set4x4FloatMatrixValueCM(mat, 3, 2, -(2.0f * far * near) / (far - near));
    }*/
    static void Make4x4FloatProjectionMatrixCMLH(float* mat, float fovInRadians, float width, float height, float near, float far)
    {
        Make4x4FloatMatrixIdentity(mat);

        float yScale = 1 / tanf(fovInRadians / 2.0f);
        float xScale = yScale / (width / height);

        Set4x4FloatMatrixValueCM(mat, 0, 0, xScale);
        Set4x4FloatMatrixValueCM(mat, 1, 1, yScale);
        Set4x4FloatMatrixValueCM(mat, 2, 2, far / (far - near));
        Set4x4FloatMatrixValueCM(mat, 3, 2, -near * far / (far - near));
        Set4x4FloatMatrixValueCM(mat, 2, 3, 1.0f);
        Set4x4FloatMatrixValueCM(mat, 3, 3, 0.0f);
    }

    static void Make4x4FloatMatrixTranspose(float* mat, float* result)
    {
        for (int i = 0; i < 4; ++i) {
            for (int j = 0; j < 4; ++j) {
                Set4x4FloatMatrixValueCM(result, j, i, Get4x4FloatMatrixValueCM(mat, i, j));
            }
        }
    }

    static void Make4x4FloatScaleMatrixCM(float* mat, float scale)
    {
        Make4x4FloatMatrixIdentity(mat);
        Set4x4FloatMatrixValueCM(mat, 0, 0, scale);
        Set4x4FloatMatrixValueCM(mat, 1, 1, scale);
        Set4x4FloatMatrixValueCM(mat, 2, 2, scale);
        Set4x4FloatMatrixValueCM(mat, 3, 3, 1.0f);
    }

    static fnd::math::float4 Get4x4FloatMatrixColumnCM(float* mat, int column)
    {
        return {
            Get4x4FloatMatrixValueCM(mat, column, 0),
            Get4x4FloatMatrixValueCM(mat, column, 1),
            Get4x4FloatMatrixValueCM(mat, column, 2),
            Get4x4FloatMatrixValueCM(mat, column, 3),
        };
    }

    static void Set4x4FloatMatrixColumnCM(float* mat, int column, fnd::math::float4 value)
    {
        for (int i = 0; i < 4; ++i) {
            Set4x4FloatMatrixValueCM(mat, column, i, value[i]);
        }
    }

    static void Make4x4FloatRotationMatrixCMLH(float* mat, fnd::math::float3 axisIn, float rad)
    {
        float rotate[16];
        float base[16];
        Make4x4FloatMatrixIdentity(base);
        Make4x4FloatMatrixIdentity(rotate);
        Make4x4FloatMatrixIdentity(mat);

        float a = rad;
        float c = fnd::math::Cos(a);
        float s = fnd::math::Sin(a);

        auto axis = fnd::math::Normalize(axisIn);
        auto temp = axis * (1.0f - c);

        Set4x4FloatMatrixValueCM(rotate, 0, 0, c + temp[0] * axis[0]);
        Set4x4FloatMatrixValueCM(rotate, 0, 1, temp[0] * axis[1] + s * axis[2]);
        Set4x4FloatMatrixValueCM(rotate, 0, 2, temp[0] * axis[2] - s * axis[1]);

        Set4x4FloatMatrixValueCM(rotate, 1, 0, temp[1] * axis[0] - s * axis[2]);
        Set4x4FloatMatrixValueCM(rotate, 1, 1, c + temp[1] * axis[1]);
        Set4x4FloatMatrixValueCM(rotate, 1, 2, temp[1] * axis[2] + s * axis[0]);

        Set4x4FloatMatrixValueCM(rotate, 2, 0, temp[2] * axis[0] + s * axis[1]);
        Set4x4FloatMatrixValueCM(rotate, 2, 1, temp[2] * axis[1] - s * axis[0]);
        Set4x4FloatMatrixValueCM(rotate, 2, 2, c + temp[2] * axis[2]);

        fnd::math::float4 m0 = Get4x4FloatMatrixColumnCM(base, 0);
        fnd::math::float4 m1 = Get4x4FloatMatrixColumnCM(base, 1);
        fnd::math::float4 m2 = Get4x4FloatMatrixColumnCM(base, 2);
        fnd::math::float4 m3 = Get4x4FloatMatrixColumnCM(base, 3);

        float r00 = Get4x4FloatMatrixValueCM(rotate, 0, 0);
        float r11 = Get4x4FloatMatrixValueCM(rotate, 1, 1);
        float r12 = Get4x4FloatMatrixValueCM(rotate, 1, 2);
        float r01 = Get4x4FloatMatrixValueCM(rotate, 0, 1);
        float r02 = Get4x4FloatMatrixValueCM(rotate, 0, 2);

        float r10 = Get4x4FloatMatrixValueCM(rotate, 1, 0);
        float r20 = Get4x4FloatMatrixValueCM(rotate, 2, 0);
        float r21 = Get4x4FloatMatrixValueCM(rotate, 2, 1);
        float r22 = Get4x4FloatMatrixValueCM(rotate, 2, 2);

        for (int i = 0; i < 4; ++i) {
            Set4x4FloatMatrixValueCM(mat, i, 0, m0[i] * r00 + m1[i] * r01 + m2[i] * r02);
            Set4x4FloatMatrixValueCM(mat, i, 1, m0[i] * r10 + m1[i] * r11 + m2[i] * r12);
            Set4x4FloatMatrixValueCM(mat, i, 2, m0[i] * r20 + m1[i] * r21 + m2[i] * r22);
            Set4x4FloatMatrixValueCM(mat, i, 3, m3[i]);
        }
    }

    static void Make4x4FloatTranslationMatrixCM(float* mat, fnd::math::float3 t)
    {
        Make4x4FloatMatrixIdentity(mat);
        for (int i = 0; i < 3; ++i) {
            Set4x4FloatMatrixValueCM(mat, 3, i, t[i]);
        }
    }

    // result = matA * matB
    static void MultiplyMatricesCM(float* left, float* right, float* result)
    {
        Make4x4FloatMatrixIdentity(result);
        for (int i = 0; i < 4; ++i) {
            for (int j = 0; j < 4; ++j) {
                float acc = 0.0f;
                for (int k = 0; k < 4; ++k) {
                    acc += Get4x4FloatMatrixValueCM(left, k, i) * Get4x4FloatMatrixValueCM(right, j, k);
                }
                Set4x4FloatMatrixValueCM(result, j, i, acc);
            }
        }
    }
}