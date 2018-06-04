#pragma once

#include "public.h"

#include <yt/core/misc/arithmetic_formula.h>

namespace NYT {
namespace NScheduler {

////////////////////////////////////////////////////////////////////////////////

class TSchedulingTagFilter
{
public:
    TSchedulingTagFilter();
    explicit TSchedulingTagFilter(const TBooleanFormula& formula);

    void Reload(const TBooleanFormula& formula);

    bool CanSchedule(const THashSet<TString>& nodeTags) const;

    bool IsEmpty() const;

    size_t GetHash() const;
    const TBooleanFormula& GetBooleanFormula() const;

private:
    TBooleanFormula BooleanFormula_;
    size_t Hash_;
};

extern const TSchedulingTagFilter EmptySchedulingTagFilter;

bool operator==(const TSchedulingTagFilter& lhs, const TSchedulingTagFilter& rhs);
bool operator!=(const TSchedulingTagFilter& lhs, const TSchedulingTagFilter& rhs);

void ToProto(TProtoStringType* protoFilter, const TSchedulingTagFilter& filter);
void FromProto(TSchedulingTagFilter* filter, const TProtoStringType& protoFilter);

void Serialize(const TSchedulingTagFilter& filter, NYson::IYsonConsumer* consumer);
void Deserialize(TSchedulingTagFilter& filter, NYTree::INodePtr node);

////////////////////////////////////////////////////////////////////////////////

} // namespace NScheduler
} // namespace NYT

////////////////////////////////////////////////////////////////////////////////

template <>
struct hash<NYT::NScheduler::TSchedulingTagFilter>
{
    size_t operator()(const NYT::NScheduler::TSchedulingTagFilter& filter) const;
};

////////////////////////////////////////////////////////////////////////////////
