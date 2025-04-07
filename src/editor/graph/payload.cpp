#include "graph/payload.hpp"
#include "erhe_verify/verify.hpp"

namespace editor {

auto operator+(const Payload& lhs, const Payload& rhs) -> Payload
{
    ERHE_VERIFY(lhs.format == rhs.format); // TODO implicit cast?
    return Payload{
        .format    = lhs.format,
        .int_value = {
            lhs.int_value[0] + rhs.int_value[0],
            lhs.int_value[1] + rhs.int_value[1],
            lhs.int_value[2] + rhs.int_value[2],
            lhs.int_value[3] + rhs.int_value[3]
        },
        .float_value = { 
            lhs.float_value[0] + rhs.float_value[0],
            lhs.float_value[1] + rhs.float_value[1],
            lhs.float_value[2] + rhs.float_value[2],
            lhs.float_value[3] + rhs.float_value[3]
        }
    };
}

auto operator-(const Payload& lhs, const Payload& rhs) -> Payload
{
    ERHE_VERIFY(lhs.format == rhs.format); // TODO implicit cast?
    return Payload{
        .format    = lhs.format,
        .int_value = {
            lhs.int_value[0] - rhs.int_value[0],
            lhs.int_value[1] - rhs.int_value[1],
            lhs.int_value[2] - rhs.int_value[2],
            lhs.int_value[3] - rhs.int_value[3]
        },
        .float_value = { 
            lhs.float_value[0] - rhs.float_value[0],
            lhs.float_value[1] - rhs.float_value[1],
            lhs.float_value[2] - rhs.float_value[2],
            lhs.float_value[3] - rhs.float_value[3]
        }
    };
}

auto operator*(const Payload& lhs, const Payload& rhs) -> Payload
{
    ERHE_VERIFY(lhs.format == rhs.format); // TODO implicit cast?
    return Payload{
        .format    = lhs.format,
        .int_value = {
            lhs.int_value[0] * rhs.int_value[0],
            lhs.int_value[1] * rhs.int_value[1],
            lhs.int_value[2] * rhs.int_value[2],
            lhs.int_value[3] * rhs.int_value[3]
        },
        .float_value = { 
            lhs.float_value[0] * rhs.float_value[0],
            lhs.float_value[1] * rhs.float_value[1],
            lhs.float_value[2] * rhs.float_value[2],
            lhs.float_value[3] * rhs.float_value[3]
        }
    };
}

auto operator/(const Payload& lhs, const Payload& rhs) -> Payload
{
    ERHE_VERIFY(lhs.format == rhs.format); // TODO implicit cast?
    return Payload{
        .format    = lhs.format,
        .int_value = {
            lhs.int_value[0] / ((rhs.int_value[0] != 0) ? rhs.int_value[0] : 1),
            lhs.int_value[1] / ((rhs.int_value[1] != 0) ? rhs.int_value[1] : 1),
            lhs.int_value[2] / ((rhs.int_value[2] != 0) ? rhs.int_value[2] : 1),
            lhs.int_value[3] / ((rhs.int_value[3] != 0) ? rhs.int_value[3] : 1)
        },
        .float_value = { 
            lhs.float_value[0] / ((rhs.float_value[0] != 0.0f) ? rhs.float_value[0] : 1.0f),
            lhs.float_value[1] / ((rhs.float_value[1] != 0.0f) ? rhs.float_value[1] : 1.0f),
            lhs.float_value[2] / ((rhs.float_value[2] != 0.0f) ? rhs.float_value[2] : 1.0f),
            lhs.float_value[3] / ((rhs.float_value[3] != 0.0f) ? rhs.float_value[3] : 1.0f)
        }
    };
}

auto Payload::operator+=(const Payload& rhs) -> Payload&
{
    int_value  [0] += rhs.int_value  [0];
    int_value  [1] += rhs.int_value  [1];
    int_value  [2] += rhs.int_value  [2];
    int_value  [3] += rhs.int_value  [3];
    float_value[0] += rhs.float_value[0];
    float_value[1] += rhs.float_value[1];
    float_value[2] += rhs.float_value[2];
    float_value[3] += rhs.float_value[3];
    return *this;
}

auto Payload::operator-=(const Payload& rhs) -> Payload&
{
    int_value  [0] -= rhs.int_value  [0];
    int_value  [1] -= rhs.int_value  [1];
    int_value  [2] -= rhs.int_value  [2];
    int_value  [3] -= rhs.int_value  [3];
    float_value[0] -= rhs.float_value[0];
    float_value[1] -= rhs.float_value[1];
    float_value[2] -= rhs.float_value[2];
    float_value[3] -= rhs.float_value[3];
    return *this;
}

auto Payload::operator*=(const Payload& rhs) -> Payload&
{
    int_value  [0] *= rhs.int_value  [0];
    int_value  [1] *= rhs.int_value  [1];
    int_value  [2] *= rhs.int_value  [2];
    int_value  [3] *= rhs.int_value  [3];
    float_value[0] *= rhs.float_value[0];
    float_value[1] *= rhs.float_value[1];
    float_value[2] *= rhs.float_value[2];
    float_value[3] *= rhs.float_value[3];
    return *this;
}

auto Payload::operator/=(const Payload& rhs) -> Payload&
{
    int_value  [0] /= (rhs.int_value[0] != 0) ? rhs.int_value[0] : 1;
    int_value  [1] /= (rhs.int_value[1] != 0) ? rhs.int_value[1] : 1;
    int_value  [2] /= (rhs.int_value[2] != 0) ? rhs.int_value[2] : 1;
    int_value  [3] /= (rhs.int_value[3] != 0) ? rhs.int_value[3] : 1;
    float_value[0] /= (rhs.float_value[0] != 0.0f) ? rhs.int_value[0] : 1.0f;
    float_value[1] /= (rhs.float_value[1] != 0.0f) ? rhs.int_value[1] : 1.0f;
    float_value[2] /= (rhs.float_value[2] != 0.0f) ? rhs.int_value[2] : 1.0f;
    float_value[3] /= (rhs.float_value[3] != 0.0f) ? rhs.int_value[3] : 1.0f;
    return *this;
}

} // namespace editor
