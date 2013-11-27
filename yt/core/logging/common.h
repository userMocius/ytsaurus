#pragma once

#include <core/misc/common.h>

namespace NYT {
namespace NLog {

////////////////////////////////////////////////////////////////////////////////

// Any changes to this enum must be also propagated to FormatLevel.
DECLARE_ENUM(ELogLevel,
    (Minimum)
    (Trace)
    (Debug)
    (Info)
    (Warning)
    (Error)
    (Fatal)
    (Maximum)
);

struct TLogEvent
{
    static const i32 InvalidLine = -1;

    TLogEvent()
        : DateTime(TInstant::Now())
        , FileName(NULL)
        , Line(InvalidLine)
        , ThreadId(NConcurrency::InvalidThreadId)
        , Function(NULL)
    { }

    TLogEvent(const Stroka& category, ELogLevel level, const Stroka& message)
        : Category(category)
        , Level(level)
        , Message(message)
        , DateTime(TInstant::Now())
        , FileName(NULL)
        , Line(InvalidLine)
        , ThreadId(NConcurrency::InvalidThreadId)
        , Function(NULL)
    { }

    Stroka Category;
    ELogLevel Level;
    Stroka Message;
    TInstant DateTime;
    const char* FileName;
    i32 Line;
    NConcurrency::TThreadId ThreadId;
    const char* Function;

};

struct ILogWriter;
typedef TIntrusivePtr<ILogWriter> ILogWriterPtr;

////////////////////////////////////////////////////////////////////////////////

} // namespace NLog
} // namespace NYT
