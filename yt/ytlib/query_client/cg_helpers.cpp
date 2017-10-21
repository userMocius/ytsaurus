#include "cg_helpers.h"
#include "cg_fragment_compiler.h"

namespace NYT {
namespace NQueryClient {

////////////////////////////////////////////////////////////////////////////////

StringRef ToStringRef(const TString& stroka)
{
    return StringRef(stroka.c_str(), stroka.length());
}

StringRef ToStringRef(const TSharedRef& sharedRef)
{
    return StringRef(sharedRef.Begin(), sharedRef.Size());
}

////////////////////////////////////////////////////////////////////////////////

Value* TCGExprContext::GetFragmentResult(size_t index) const
{
    return Builder_->CreateInBoundsGEP(
        ExpressionClosurePtr,
        {
            Builder_->getInt32(0),
            Builder_->getInt32(TClosureTypeBuilder::Fields::FragmentResults),
            Builder_->getInt32(ExpressionFragments.Items[index].Index)
        },
        Twine("fragment#") + Twine(index));
}

Value* TCGExprContext::GetFragmentFlag(size_t index) const
{
    return Builder_->CreateInBoundsGEP(
        ExpressionClosurePtr,
        {
            Builder_->getInt32(0),
            Builder_->getInt32(TClosureTypeBuilder::Fields::FragmentFlags),
            Builder_->getInt32(ExpressionFragments.Items[index].Index)
        },
        Twine("flag#") + Twine(index));
}

TCGExprContext TCGExprContext::Make(
    const TCGBaseContext& builder,
    const TCodegenFragmentInfos& fragmentInfos,
    Value* expressionClosurePtr)
{
    Value* opaqueValues = builder->CreateLoad(builder->CreateStructGEP(
        nullptr,
        expressionClosurePtr,
        TClosureTypeBuilder::Fields::OpaqueValues,
        "opaqueValues"));

    return TCGExprContext(
        TCGOpaqueValuesContext(builder, opaqueValues),
        TCGExprData(
            fragmentInfos,
            builder->CreateLoad(builder->CreateStructGEP(
                nullptr,
                expressionClosurePtr,
                TClosureTypeBuilder::Fields::Buffer,
                "buffer")),
            builder->CreateLoad(builder->CreateStructGEP(
                nullptr,
                expressionClosurePtr,
                TClosureTypeBuilder::Fields::RowValues,
                "rowValues")),
            expressionClosurePtr
        ));
}

TCGExprContext TCGExprContext::Make(
    const TCGOpaqueValuesContext& builder,
    const TCodegenFragmentInfos& fragmentInfos,
    Value* row,
    Value* buffer)
{
    Value* rowValues = CodegenValuesPtrFromRow(builder, row);

    llvm::StructType* type = TClosureTypeBuilder::get(builder->getContext(), fragmentInfos.Functions.size());

    Value* expressionClosure = llvm::ConstantStruct::get(
        type,
        {
            llvm::UndefValue::get(TypeBuilder<TValue*, false>::get(builder->getContext())),
            llvm::UndefValue::get(TypeBuilder<void* const*, false>::get(builder->getContext())),
            llvm::UndefValue::get(TypeBuilder<TRowBuffer*, false>::get(builder->getContext())),
            llvm::ConstantAggregateZero::get(
                llvm::ArrayType::get(TypeBuilder<char, false>::get(
                    builder->getContext()),
                    fragmentInfos.Functions.size())),
            llvm::UndefValue::get(
                llvm::ArrayType::get(TypeBuilder<TValue, false>::get(
                    builder->getContext()),
                    fragmentInfos.Functions.size()))
        });

    expressionClosure = builder->CreateInsertValue(
        expressionClosure,
        rowValues,
        TClosureTypeBuilder::Fields::RowValues);

    expressionClosure = builder->CreateInsertValue(
        expressionClosure,
        builder.GetOpaqueValues(),
        TClosureTypeBuilder::Fields::OpaqueValues);

    expressionClosure = builder->CreateInsertValue(
        expressionClosure,
        buffer,
        TClosureTypeBuilder::Fields::Buffer);

    Value* expressionClosurePtr = builder->CreateAlloca(
        type,
        nullptr,
        "expressionClosurePtr");

    builder->CreateStore(expressionClosure, expressionClosurePtr);

    return TCGExprContext(
        builder,
        TCGExprData(
            fragmentInfos,
            buffer,
            rowValues,
            expressionClosurePtr
        ));
}

Value* TCGExprContext::GetExpressionClosurePtr()
{
    return ExpressionClosurePtr;
}

////////////////////////////////////////////////////////////////////////////////

TCodegenConsumer& TCGOperatorContext::operator[] (size_t index) const
{
    if (!(*Consumers_)[index]) {
        (*Consumers_)[index] = std::make_shared<TCodegenConsumer>();
    }
    return *(*Consumers_)[index];
}

////////////////////////////////////////////////////////////////////////////////
// Row manipulation helpers
//

Value* CodegenValuesPtrFromRow(const TCGIRBuilderPtr& builder, Value* row)
{
    auto name = row->getName();

    auto headerPtr = builder->CreateExtractValue(
        row,
        TypeBuilder<TRow, false>::Fields::Header,
        Twine(name).concat(".headerPtr"));

    auto valuesPtr = builder->CreatePointerCast(
        builder->CreateConstInBoundsGEP1_32(nullptr, headerPtr, 1, "valuesPtrUncasted"),
        TypeBuilder<TValue*, false>::get(builder->getContext()),
        Twine(name).concat(".valuesPtr"));

    return valuesPtr;
}

////////////////////////////////////////////////////////////////////////////////

TCGValue MakePhi(
    TCGIRBuilderPtr& builder,
    BasicBlock* thenBB,
    BasicBlock* elseBB,
    BasicBlock* endBB,
    TCGValue thenValue,
    TCGValue elseValue,
    Twine name)
{
    YCHECK(thenValue.GetStaticType() == elseValue.GetStaticType());
    EValueType type = thenValue.GetStaticType();

    builder->SetInsertPoint(thenBB);
    Value* thenNull = thenValue.IsNull(builder);
    Value* thenData = thenValue.GetData(builder);

    builder->SetInsertPoint(elseBB);
    Value* elseNull = elseValue.IsNull(builder);
    Value* elseData = elseValue.GetData(builder);

    builder->SetInsertPoint(endBB);

    Value* phiNull = [&] () -> Value* {
        if (llvm::Constant* constantThenNull = llvm::dyn_cast<llvm::Constant>(thenNull)) {
            if (llvm::Constant* constantElseNull = llvm::dyn_cast<llvm::Constant>(elseNull)) {
                if (constantThenNull->isNullValue() && constantElseNull->isNullValue()) {
                    return builder->getFalse();
                }
            }
        }

        Type* targetType = builder->getInt1Ty();

        PHINode* phiNull = builder->CreatePHI(targetType, 2, name + ".phiNull");
        phiNull->addIncoming(thenNull, thenBB);
        phiNull->addIncoming(elseNull, elseBB);
        return phiNull;
    }();


    YCHECK(thenData->getType() == elseData->getType());

    PHINode* phiData = builder->CreatePHI(thenData->getType(), 2, name + ".phiData");
    phiData->addIncoming(thenData, thenBB);
    phiData->addIncoming(elseData, elseBB);

    PHINode* phiLength = nullptr;
    if (IsStringLikeType(type)) {
        builder->SetInsertPoint(thenBB);
        Value* thenLength = thenValue.GetLength(builder);
        builder->SetInsertPoint(elseBB);
        Value* elseLength = elseValue.GetLength(builder);

        builder->SetInsertPoint(endBB);

        YCHECK(thenLength->getType() == elseLength->getType());

        phiLength = builder->CreatePHI(thenLength->getType(), 2, name + ".phiLength");
        phiLength->addIncoming(thenLength, thenBB);
        phiLength->addIncoming(elseLength, elseBB);
    }

    return TCGValue::CreateFromValue(builder, phiNull, phiLength, phiData, type, name);
}

Value* MakePhi(
    TCGIRBuilderPtr& builder,
    BasicBlock* thenBB,
    BasicBlock* elseBB,
    BasicBlock* endBB,
    Value* thenValue,
    Value* elseValue,
    Twine name)
{
    builder->SetInsertPoint(endBB);
    PHINode* phiValue = builder->CreatePHI(thenValue->getType(), 2, name + ".phiValue");
    phiValue->addIncoming(thenValue, thenBB);
    phiValue->addIncoming(elseValue, elseBB);
    return phiValue;
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NQueryClient
} // namespace NYT
