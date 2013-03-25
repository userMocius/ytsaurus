#include "stdafx.h"
#include "table_commands.h"
#include "config.h"

#include <ytlib/formats/format.h>

#include <ytlib/yson/yson_parser.h>
#include <ytlib/ytree/tree_visitor.h>

#include <ytlib/transaction_client/transaction_manager.h>

#include <ytlib/table_client/table_reader.h>
#include <ytlib/table_client/table_writer.h>
#include <ytlib/table_client/table_consumer.h>
#include <ytlib/table_client/table_producer.h>

#include <ytlib/chunk_client/block_cache.h>

namespace NYT {
namespace NDriver {

using namespace NYTree;
using namespace NFormats;
using namespace NTableClient;

////////////////////////////////////////////////////////////////////////////////

void TReadCommand::DoExecute()
{
    auto config = UpdateYsonSerializable(
        Context->GetConfig()->TableReader,
        Request->TableReaderConfig);

    Request->Path = Request->Path.Simplify();
    auto reader = New<TTableReader>(
        config,
        Context->GetMasterChannel(),
        GetTransaction(false),
        Context->GetBlockCache(),
        Request->Path);
    reader->Open();

    auto driverRequest = Context->GetRequest();
    auto writer = CreateConsumerForFormat(
        driverRequest->OutputFormat,
        EDataType::Tabular,
        driverRequest->OutputStream);
    ProduceYson(reader, ~writer);
}

//////////////////////////////////////////////////////////////////////////////////

void TWriteCommand::DoExecute()
{
    auto config = UpdateYsonSerializable(
        Context->GetConfig()->TableWriter,
        Request->TableWriterConfig);

    Request->Path = Request->Path.Simplify();
    auto writer = New<TTableWriter>(
        config,
        Context->GetMasterChannel(),
        GetTransaction(false),
        Context->GetTransactionManager(),
        Request->Path,
        Request->Path.Attributes().Find<TKeyColumns>("sorted_by"));
    writer->Open();

    TTableConsumer consumer(writer);

    auto driverRequest = Context->GetRequest();
    auto producer = CreateProducerForFormat(
        driverRequest->InputFormat,
        EDataType::Tabular,
        driverRequest->InputStream);
    producer.Run(&consumer);

    writer->Close();
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NDriver
} // namespace NYT
