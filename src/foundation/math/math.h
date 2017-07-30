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

        template <class TNumber>
        TNumber Sqrt(TNumber n)
        {
            static_assert(false, "TNumber doesn't support Sqrt()");
        }

        template <>
        float Sqrt(float n)
        {
            return sqrtf(n);
        }

        template <>
        double Sqrt(double n)
        {
            return sqrt(n);
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

        typedef Vector<int, 2> int2;
        typedef Vector<int, 3> int3;
        typedef Vector<int, 4> int4;
    }
}