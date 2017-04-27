#include "functions_common.h"

namespace NYT {
namespace NQueryClient {

////////////////////////////////////////////////////////////////////////////////

EValueType TTypeSet::GetFront() const
{
    YCHECK(!IsEmpty());
    static const int MultiplyDeBruijnBitPosition[64] = {
        0,  1,  2, 53,  3,  7, 54, 27,  4, 38, 41,  8, 34, 55, 48, 28,
       62,  5, 39, 46, 44, 42, 22,  9, 24, 35, 59, 56, 49, 18, 29, 11,
       63, 52,  6, 26, 37, 40, 33, 47, 61, 45, 43, 21, 23, 58, 17, 10,
       51, 25, 36, 32, 60, 20, 57, 16, 50, 31, 19, 15, 30, 14, 13, 12
    };

    return EValueType(MultiplyDeBruijnBitPosition[((Value_ & -Value_) * 0x022fdd63cc95386d) >> 58]);
}

size_t TTypeSet::GetSize() const
{
    size_t result = 0;
    ui64 mask = 1;
    for (size_t index = 0; index < 8 * sizeof(ui64); ++index, mask <<= 1) {
        if (Value_ & mask) {
            ++result;
        }
    }
    return result;
}

TTypeSet operator | (const TTypeSet& lhs, const TTypeSet& rhs) {
    return TTypeSet(lhs.Value_ | rhs.Value_);
}

TTypeSet operator & (const TTypeSet& lhs, const TTypeSet& rhs) {
    return TTypeSet(lhs.Value_ & rhs.Value_);
}

Stroka ToString(const TTypeSet& typeSet)
{
    if (typeSet.GetSize() == 1) {
        return ToString(typeSet.GetFront());
    } else {
        Stroka unionString = "one of { ";
        bool isFirst = true;
        typeSet.ForEach([&] (EValueType type) {
            if (!isFirst) {
                unionString += ", ";
            } else {
                isFirst = false;
            }
            unionString += ToString(type);

        });

        return unionString + " }";
    }
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NQueryClient
} // namespace NYT
