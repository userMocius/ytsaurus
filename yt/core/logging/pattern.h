#pragma once

#include "common.h"

#include <core/misc/raw_formatter.h>

namespace NYT {
namespace NLogging {

////////////////////////////////////////////////////////////////////////////////

const int MessageBufferSize = 65556;
typedef TRawFormatter<MessageBufferSize> TMessageBuffer;

void FormatMessage(TMessageBuffer* out, const Stroka& message);
void FormatDateTime(TMessageBuffer* out, TInstant dateTime);
void FormatLevel(TMessageBuffer* out, ELogLevel level);

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT
} // namespace NLogging
