#pragma once

#include <ytlib/fibers/coroutine.h>
#include "yson_detail.h"

namespace NYT {
namespace NYson {

////////////////////////////////////////////////////////////////////////////////

namespace NDetail {
/*! \internal */
////////////////////////////////////////////////////////////////////////////////

template <class TConsumer, class TBlockStream, bool EnableLinePositionInfo>
class TParser
    : public TLexerBase<TBlockStream, EnableLinePositionInfo>
{
private:
    typedef TLexerBase<TBlockStream, EnableLinePositionInfo> TBase;
    TConsumer* Consumer;

public:
    TParser(const TBlockStream& blockStream, TConsumer* consumer) 
        : TBase(blockStream)
        , Consumer(consumer)
    { }
    
    void DoParse(EYsonType ParsingMode)
    {
        switch (ParsingMode) {
            case EYsonType::Node:
                ParseNode<true>();
                break;

            case EYsonType::ListFragment:
                ParseListFragment<true>(EndSymbol);
                break;

            case EYsonType::MapFragment:
                ParseMapFragment<true>(EndSymbol);
                break;

            default:
                YUNREACHABLE();
        }

        while (!(TBase::IsFinished() && TBase::IsEmpty())) {
            if (TBase::template SkipSpaceAndGetChar<true>() != EndSymbol) {
                THROW_ERROR_EXCEPTION("Input is already parsed but unexpected symbol %s found (%s)",
                    ~Stroka(*TBase::Begin()).Quote(),
                    ~TBase::GetPositionInfo());
            } else if (!TBase::IsEmpty()) {
                TBase::Advance(1);
            }
        }
    }

    /// Parse routines
    void ParseAttributes()
    {
        Consumer->OnBeginAttributes();
        ParseMapFragment(EndAttributesSymbol);
        TBase::SkipCharToken(EndAttributesSymbol);
        Consumer->OnEndAttributes();
    }

    void ParseMap()
    {
        Consumer->OnBeginMap();
        ParseMapFragment(EndMapSymbol); 
        TBase::SkipCharToken(EndMapSymbol);
        Consumer->OnEndMap();
    }
    
    void ParseList()
    {
        Consumer->OnBeginList();
        ParseListFragment(EndListSymbol);
        TBase::SkipCharToken(EndListSymbol);
        Consumer->OnEndList();
    }

    template <bool AllowFinish>
    void ParseNode()
    {
        return ParseNode<AllowFinish>(TBase::SkipSpaceAndGetChar());
    }

    template <bool AllowFinish>
    void ParseNode(char ch)
    {
        if (ch == BeginAttributesSymbol) {
            TBase::Advance(1);
            ParseAttributes();
            ch = TBase::SkipSpaceAndGetChar();
        }
        
        switch (ch) { 
            case BeginMapSymbol: 
                TBase::Advance(1);
                ParseMap(); 
                break;

            case BeginListSymbol: 
                TBase::Advance(1);
                ParseList();
                break;

            case '"': {
                TBase::Advance(1);
                TStringBuf value;
                TBase::ReadQuotedString(&value);
                Consumer->OnStringScalar(value);
                break;
            }
            case StringMarker: {
                TBase::Advance(1);
                TStringBuf value;
                TBase::ReadBinaryString(&value);
                Consumer->OnStringScalar(value);
                break;
            }
            case IntegerMarker:{
                TBase::Advance(1);
                i64 value;
                TBase::ReadBinaryInteger(&value);
                Consumer->OnIntegerScalar(value);
                break;
            }
            case DoubleMarker: {
                TBase::Advance(1);
                double value;
                TBase::ReadBinaryDouble(&value);
                Consumer->OnDoubleScalar(value);
                break;
            }
            case EntitySymbol:
                TBase::Advance(1);
                Consumer->OnEntity();
                break;

            default: {
                if (isdigit(ch) || ch == '-' || ch == '+') { // case of '+' is handled in AfterPlus state
                    ReadNumeric<AllowFinish>();
                } else if (isalpha(ch) || ch == '_' || ch == '%') {
                    TStringBuf value;
                    TBase::template ReadUnquotedString<AllowFinish>(&value);
                    Consumer->OnStringScalar(value);
                } else {
                    THROW_ERROR_EXCEPTION("Unexpected character %s while parsing Node (%s)",
                        ~Stroka(ch).Quote(),
                        ~TBase::GetPositionInfo());
                }
            }
        }
    }

    void ParseKey()
    {
        return ParseKey(TBase::SkipSpaceAndGetChar());
    }

    void ParseKey(char ch)
    {        
        switch (ch) { 
            case '"': {
                TBase::Advance(1);
                TStringBuf value;
                TBase::ReadQuotedString(&value);
                Consumer->OnKeyedItem(value);
                break;
            }
            case StringMarker: {
                TBase::Advance(1);
                TStringBuf value;
                TBase::ReadBinaryString(&value);
                Consumer->OnKeyedItem(value);
                break;
            }
            default: {
                if (isalpha(ch) || ch == '_' || ch == '%') {
                    TStringBuf value;
                    TBase::ReadUnquotedString(&value);
                    Consumer->OnKeyedItem(value);
                } else {
                    THROW_ERROR_EXCEPTION("Unexpected character %s while parsing Key (%s)",
                        ~Stroka(ch).Quote(),
                        ~TBase::GetPositionInfo());
                }
            }
        }
    }
    
    template <bool AllowFinish>
    void ParseMapFragment(char endSymbol)
    {
        char ch = TBase::template SkipSpaceAndGetChar<AllowFinish>();
        while (ch != endSymbol)
        {
            ParseKey(ch);
            ch = TBase::template SkipSpaceAndGetChar<AllowFinish>();
            if (ch == KeyValueSeparatorSymbol) {
                TBase::Advance(1);
            } else {
                THROW_ERROR_EXCEPTION("Expected '%c' but character %s found (%s)",
                    KeyValueSeparatorSymbol,
                    ~Stroka(ch).Quote(),
                    ~TBase::GetPositionInfo());
            }
            ParseNode<AllowFinish>();
            ch = TBase::template SkipSpaceAndGetChar<AllowFinish>();
            if (ch == KeyedItemSeparatorSymbol) {
                TBase::Advance(1);
                ch = TBase::template SkipSpaceAndGetChar<AllowFinish>();
            } else if (ch != endSymbol) {
                THROW_ERROR_EXCEPTION("Expected '%c' or %s but character %s found (%s)",
                    KeyedItemSeparatorSymbol,
                    ~Stroka(endSymbol).Quote(),
                    ~Stroka(ch).Quote(),
                    ~TBase::GetPositionInfo());
            }
            
        }
    }

    void ParseMapFragment(char endSymbol)
    {
        ParseMapFragment<false>(endSymbol);
    }

    template <bool AllowFinish>
    void ParseListFragment(char endSymbol)
    {
        char ch = TBase::template SkipSpaceAndGetChar<AllowFinish>();
        while (ch != endSymbol)
        {
            Consumer->OnListItem();
            ParseNode<AllowFinish>(ch);
            ch = TBase::template SkipSpaceAndGetChar<AllowFinish>();
            if (ch == ListItemSeparatorSymbol) {
                TBase::Advance(1);
                ch = TBase::template SkipSpaceAndGetChar<AllowFinish>();
            } else if (ch != endSymbol) {
                THROW_ERROR_EXCEPTION("Expected '%c' or %s but character %s found (%s)",
                    ListItemSeparatorSymbol,
                    ~Stroka(endSymbol).Quote(),
                    ~Stroka(ch).Quote(),
                    ~TBase::GetPositionInfo());
            }            
        }
    }

    void ParseListFragment(char endSymbol)
    {
        ParseListFragment<false>(endSymbol);
    }
    
    template <bool AllowFinish>
    void ReadNumeric()
    {
        TStringBuf valueBuffer;
        bool isDouble = TBase::template ReadNumeric<AllowFinish>(&valueBuffer);

        if (isDouble) {
            double value;
            try {
                value = FromString<double>(valueBuffer);
            } catch (const std::exception& ex) {
                // This exception is wrapped in parser
                THROW_ERROR_EXCEPTION("Failed to parse Double literal %s (%s)",
                    ~Stroka(valueBuffer).Quote(),
                    ~TBase::GetPositionInfo());
            }
            Consumer->OnDoubleScalar(value);
        } else {
            i64 value;
            try {
                value = FromString<i64>(valueBuffer);
            } catch (const std::exception& ex) {
                // This exception is wrapped in parser
                THROW_ERROR_EXCEPTION("Failed to parse Integer literal %s (%s)",
                    ~Stroka(valueBuffer).Quote(),
                    ~TBase::GetPositionInfo());
            }
            Consumer->OnIntegerScalar(value);
        }
    } 
};

////////////////////////////////////////////////////////////////////////////////
/*! \endinternal */
} // namespace NDetail

template <class TConsumer, class TBlockStream>
void ParseStreamImpl(const TBlockStream& blockStream, IYsonConsumer* consumer, EYsonType parsingMode = EYsonType::Node, bool enableLinePositionInfo = false)
{
    if (enableLinePositionInfo) {
        typedef NDetail::TParser<TConsumer, TBlockStream, true> TImpl;
        TImpl rapidYsonParserImpl(blockStream, consumer);
        rapidYsonParserImpl.DoParse(parsingMode);
    } else {
        typedef NDetail::TParser<TConsumer, TBlockStream, false> TImpl;
        TImpl rapidYsonParserImpl(blockStream, consumer);
        rapidYsonParserImpl.DoParse(parsingMode);
    }
}

class TStatelessYsonParserImplBase
{
public:
    virtual void Parse(const TStringBuf& data, EYsonType type = EYsonType::Node) = 0;
};

template <class TConsumer, bool EnableLinePositionInfo>
class TStatelessYsonParserImpl : public TStatelessYsonParserImplBase
{
private:
    typedef NDetail::TParser<TConsumer, TStringReader, EnableLinePositionInfo> TParser;
    TParser Parser;

public:
    TStatelessYsonParserImpl(TConsumer* consumer)
        : Parser(TStringReader(), consumer)
    { }
    
    void Parse(const TStringBuf& data, EYsonType type = EYsonType::Node) override
    {
        Parser.SetBuffer(data.begin(), data.end());
        Parser.DoParse(type);
    }
};

template <class TConsumer>
class TYsonParserImpl
{   
private:
    typedef TCoroutine<int(const char* begin, const char* end, bool finish) > TParserCoroutine;
    
    class TBlockReader
    {
    private:
        TParserCoroutine& Coroutine;

        const char* BeginPtr;
        const char* EndPtr;
        bool FinishFlag;

    public:
        TBlockReader(TParserCoroutine& coroutine, const char* begin, const char* end, bool finish)
            : Coroutine(coroutine)
            , BeginPtr(begin)
            , EndPtr(end)
            , FinishFlag(finish)
        { }

        const char* Begin() const
        {
            return BeginPtr;
        }

        const char* End() const
        {
            return EndPtr;
        }

        void RefreshBlock()
        {
            std::tie(BeginPtr, EndPtr, FinishFlag) = Coroutine.Yield(0);
        }

        void Advance(size_t amount)
        {
            BeginPtr += amount;
        }
    
        bool IsFinished() const
        {
            return FinishFlag;
        }
    };

    TParserCoroutine ParserCoroutine;

public:
    TYsonParserImpl(
        TConsumer* consumer, 
        EYsonType parsingMode = EYsonType::Node, 
        bool enableLinePositionInfo = false) 
    {
        typedef typename TYsonParserImpl<TConsumer>::TBlockReader TBlockReader;
        ParserCoroutine.Reset(BIND([=] (TParserCoroutine& self, const char* begin, const char* end, bool finish) {
            ParseStreamImpl<TConsumer, TBlockReader>(
                TBlockReader(self, begin, end, finish),
                consumer,
                parsingMode,
                enableLinePositionInfo);
        }));
    }

    void Read(const char* begin, const char* end, bool finish = false)
    {
        if (ParserCoroutine.GetState() != EFiberState::Terminated) {
            ParserCoroutine.Run(begin, end, finish);
        } else {
            THROW_ERROR_EXCEPTION("Input is already parsed");
        }
    }

    void Read(const TStringBuf& data, bool finish = false)
    {
        Read(data.begin(), data.end(), finish);
    }

    void Finish()
    {
        Read(0, 0, true);
    }
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NYson
} // namespace NYT
