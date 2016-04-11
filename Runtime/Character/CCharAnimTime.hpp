#ifndef __PSHAG_CCHARANIMTIME_HPP__
#define __PSHAG_CCHARANIMTIME_HPP__

#include <algorithm>

namespace urde
{

class CCharAnimTime
{
    float m_time = 0.f;
    enum class Type
    {
        NonZero,
        ZeroIncreasing,
        ZeroSteady,
        ZeroDecreasing,
        Infinity
    } m_type = Type::ZeroSteady;
public:
    CCharAnimTime() = default;
    CCharAnimTime(float time)
        : m_time(time),
          m_type(m_time != 0.f ? Type::NonZero : Type::ZeroSteady) {}

    static CCharAnimTime Infinity();
    operator float() const {return m_time;}

    bool EqualsZero() const;
    bool GreaterThanZero() const;
    bool operator ==(const CCharAnimTime& other) const;
    bool operator !=(const CCharAnimTime& other) const;
    bool operator>=(const CCharAnimTime& other);
    bool operator<=(const CCharAnimTime& other);
    bool operator >(const CCharAnimTime& other) const;
    bool operator <(const CCharAnimTime& other) const;
    CCharAnimTime& operator*=(const CCharAnimTime& other);
    CCharAnimTime& operator+=(const CCharAnimTime& other);
    CCharAnimTime operator+(const CCharAnimTime& other);
    CCharAnimTime& operator-=(const CCharAnimTime& other);
    CCharAnimTime operator-(const CCharAnimTime& other);
    CCharAnimTime operator*(const CCharAnimTime& other);
    CCharAnimTime operator*(const float& other);
    float operator/(const CCharAnimTime& other);
};
}

#endif // __PSHAG_CCHARANIMTIME_HPP__
