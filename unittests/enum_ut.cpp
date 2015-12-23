#include "framework.h"

#include <yt/core/misc/common.h>
#include <yt/core/misc/enum.h>
#include <yt/core/misc/public.h>
#include <yt/core/misc/serialize.h>

namespace NYT {
namespace {

////////////////////////////////////////////////////////////////////////////////

DEFINE_ENUM(ESimple, (X)(Y)(Z));

DEFINE_ENUM(EColor,
    ((Red)  (10))
    ((Green)(20))
    ((Blue) (30))
     (Black)
     (White)
);

DEFINE_BIT_ENUM(EFlag,
    ((_1)(0x0001))
    ((_2)(0x0002))
    ((_3)(0x0004))
    ((_4)(0x0008))
);

////////////////////////////////////////////////////////////////////////////////

TEST(TEnumTest, Domain)
{
    EXPECT_EQ(3, TEnumTraits<ESimple>::GetDomainSize());
    std::vector<ESimple> v {
        ESimple::X,
        ESimple::Y,
        ESimple::Z
    };
    EXPECT_EQ(v, TEnumTraits<ESimple>::GetDomainValues());
    EXPECT_EQ(ESimple::X, TEnumTraits<ESimple>::GetMinValue());
    EXPECT_EQ(ESimple::Z, TEnumTraits<ESimple>::GetMaxValue());
}

TEST(TEnumTest, Basic)
{
    EXPECT_EQ(0, static_cast<int>(ESimple::X));
    EXPECT_EQ(1, static_cast<int>(ESimple::Y));
    EXPECT_EQ(2, static_cast<int>(ESimple::Z));

    EXPECT_EQ(0, static_cast<int>(EColor( )));
    EXPECT_EQ(5, static_cast<int>(EColor(5)));

    EXPECT_EQ(10, static_cast<int>(EColor::Red  ));
    EXPECT_EQ(20, static_cast<int>(EColor::Green));
    EXPECT_EQ(30, static_cast<int>(EColor::Blue ));
    EXPECT_EQ(31, static_cast<int>(EColor::Black));
    EXPECT_EQ(32, static_cast<int>(EColor::White));
}

TEST(TEnumTest, ToString)
{
    EXPECT_EQ("EColor(0)", ToString(EColor( )));
    EXPECT_EQ("EColor(5)", ToString(EColor(5)));

    EXPECT_EQ("Red",   ToString(EColor(EColor::Red  )));
    EXPECT_EQ("Green", ToString(EColor::Green));
    EXPECT_EQ("Blue",  ToString(EColor(EColor::Blue )));
    EXPECT_EQ("Black", ToString(EColor::Black));
    EXPECT_EQ("White", ToString(EColor::White));
}

TEST(TEnumTest, FromString)
{
    EXPECT_EQ(EColor::Red  , TEnumTraits<EColor>::FromString("Red"  ));
    EXPECT_EQ(EColor::Green, TEnumTraits<EColor>::FromString("Green"));
    EXPECT_EQ(EColor::Blue , TEnumTraits<EColor>::FromString("Blue" ));
    EXPECT_EQ(EColor::Black, TEnumTraits<EColor>::FromString("Black"));
    EXPECT_EQ(EColor::White, TEnumTraits<EColor>::FromString("White"));

    EXPECT_THROW(TEnumTraits<EColor>::FromString("Pink"), std::exception);

    EColor color;
    bool returnValue;

    returnValue = TEnumTraits<EColor>::FindValueByLiteral("Red", &color);
    EXPECT_EQ(EColor::Red, color);
    EXPECT_TRUE(returnValue);

    returnValue = TEnumTraits<EColor>::FindValueByLiteral("Pink", &color);
    EXPECT_EQ(EColor::Red, color);
    EXPECT_FALSE(returnValue);
}

TEST(TEnumTest, Ordering)
{
    ESimple a(ESimple::X);
    ESimple b(ESimple::Y);
    ESimple c(ESimple::Y);
    ESimple d(ESimple::Z);

    EXPECT_FALSE(a < a); EXPECT_FALSE(a > a);
    EXPECT_TRUE (a < b); EXPECT_TRUE (b > a);
    EXPECT_TRUE (a < c); EXPECT_TRUE (c > a);
    EXPECT_TRUE (a < d); EXPECT_TRUE (d > a);

    EXPECT_FALSE(b < a); EXPECT_FALSE(a > b);
    EXPECT_FALSE(b < b); EXPECT_FALSE(b > b);
    EXPECT_FALSE(b < c); EXPECT_FALSE(c > b);
    EXPECT_TRUE (b < d); EXPECT_TRUE (d > b);

    EXPECT_FALSE(c < a); EXPECT_FALSE(a > c);
    EXPECT_FALSE(c < b); EXPECT_FALSE(b > c);
    EXPECT_FALSE(c < c); EXPECT_FALSE(c > c);
    EXPECT_TRUE (c < d); EXPECT_TRUE (d > c);

    EXPECT_FALSE(d < a); EXPECT_FALSE(a > d);
    EXPECT_FALSE(d < b); EXPECT_FALSE(b > d);
    EXPECT_FALSE(d < c); EXPECT_FALSE(c > d);
    EXPECT_FALSE(d < d); EXPECT_FALSE(d > d);

    EXPECT_TRUE (a <= b);
    EXPECT_TRUE (b <= c);
    EXPECT_TRUE (c <= d);

    EXPECT_TRUE (a == a);
    EXPECT_FALSE(a == b);
    EXPECT_TRUE (b == c);
    EXPECT_FALSE(c == d);
    EXPECT_FALSE(d == a);

    EXPECT_FALSE(a != a);
    EXPECT_TRUE (a != b);
    EXPECT_FALSE(b != c);
    EXPECT_TRUE (c != d);
    EXPECT_TRUE (d != a);
}

TEST(TEnumTest, OrderingWithDomainValues)
{
    EColor color(EColor::Black);

    EXPECT_LT(EColor::Red, color);
    EXPECT_LT(color, EColor::White);

    EXPECT_GT(color, EColor::Red);
    EXPECT_GT(EColor::White, color);

    EXPECT_LE(EColor::Red, color);
    EXPECT_LE(color, EColor::White);

    EXPECT_GE(EColor::White, color);
    EXPECT_GE(color, EColor::Red);

    EXPECT_EQ(color, EColor::Black);
    EXPECT_EQ(EColor::Black, color);

    EXPECT_NE(color, EColor::Blue);
    EXPECT_NE(EColor::Blue, color);
}

TEST(TEnumTest, SaveAndLoad)
{
    TStringStream stream;

    TStreamSaveContext saveContext;
    saveContext.SetOutput(&stream);

    TStreamLoadContext loadContext;
    loadContext.SetInput(&stream);

    auto first = EColor::Red;
    auto second = EColor::Black;
    auto third = EColor(0);
    auto fourth = EColor(0);

    Save(saveContext, first);
    Save(saveContext, second);

    Load(loadContext, third);
    Load(loadContext, fourth);

    EXPECT_EQ(first, third);
    EXPECT_EQ(second, fourth);
}

TEST(TEnumTest, DomainSize)
{
    EXPECT_EQ(3, TEnumTraits<ESimple>::GetDomainSize());
    EXPECT_EQ(5, TEnumTraits<EColor>::GetDomainSize());
}

TEST(TEnumTest, DomainValues)
{
    std::vector<ESimple> simpleValues;
    simpleValues.push_back(ESimple::X);
    simpleValues.push_back(ESimple::Y);
    simpleValues.push_back(ESimple::Z);
    EXPECT_EQ(simpleValues, TEnumTraits<ESimple>::GetDomainValues());

    std::vector<EColor> colorValues;
    colorValues.push_back(EColor::Red);
    colorValues.push_back(EColor::Green);
    colorValues.push_back(EColor::Blue);
    colorValues.push_back(EColor::Black);
    colorValues.push_back(EColor::White);
    EXPECT_EQ(colorValues, TEnumTraits<EColor>::GetDomainValues());
}

TEST(TEnumTest, Decompose1)
{
    auto f = EFlag(0);
    std::vector<EFlag> ff { };
    EXPECT_EQ(TEnumTraits<EFlag>::Decompose(f), ff);
}

TEST(TEnumTest, Decompose2)
{
    auto f = EFlag::_1;
    std::vector<EFlag> ff {EFlag::_1};
    EXPECT_EQ(TEnumTraits<EFlag>::Decompose(f), ff);
}

TEST(TEnumTest, Decompose3)
{
    auto f = EFlag(EFlag::_1|EFlag::_2);
    std::vector<EFlag> ff{EFlag::_1, EFlag::_2};
    EXPECT_EQ(TEnumTraits<EFlag>::Decompose(f), ff);
}

TEST(TEnumTest, Decompose4)
{
    auto f = EFlag(EFlag::_2|EFlag::_4);
    std::vector<EFlag> ff{EFlag::_2, EFlag::_4};
    EXPECT_EQ(TEnumTraits<EFlag>::Decompose(f), ff);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace
} // namespace NYT

