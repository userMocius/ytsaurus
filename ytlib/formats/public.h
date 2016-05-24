#pragma once

#include <yt/core/misc/public.h>

namespace NYT {
namespace NFormats {

////////////////////////////////////////////////////////////////////////////////

DECLARE_REFCOUNTED_CLASS(TYsonFormatConfig)
DECLARE_REFCOUNTED_CLASS(TJsonFormatConfig)
DECLARE_REFCOUNTED_CLASS(TTableFormatConfigBase)
DECLARE_REFCOUNTED_CLASS(TYamrFormatConfig)
DECLARE_REFCOUNTED_CLASS(TYamrFormatConfigBase)
DECLARE_REFCOUNTED_CLASS(TDsvFormatConfig)
DECLARE_REFCOUNTED_CLASS(TDsvFormatConfigBase)
DECLARE_REFCOUNTED_CLASS(TYamredDsvFormatConfig)
DECLARE_REFCOUNTED_CLASS(TSchemafulDsvFormatConfig)

DECLARE_REFCOUNTED_STRUCT(IYamrConsumer)

DECLARE_REFCOUNTED_STRUCT(ISchemalessFormatWriter)

DECLARE_REFCOUNTED_CLASS(TSchemalessWriterForDsv)
DECLARE_REFCOUNTED_CLASS(TSchemalessWriterForYamr)
DECLARE_REFCOUNTED_CLASS(TSchemalessWriterForYamredDsv)
DECLARE_REFCOUNTED_CLASS(TSchemalessWriterForSchemafulDsv)

struct IParser;

////////////////////////////////////////////////////////////////////////////////

} // namespace NFormats
} // namespace NYT
