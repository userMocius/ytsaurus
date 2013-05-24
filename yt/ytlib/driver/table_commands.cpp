#include "stdafx.h"
#include "table_commands.h"
#include "config.h"

#include <ytlib/formats/format.h>

#include <ytlib/yson/parser.h>
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
        Request->TableReader);

    auto reader = New<TTableReader>(
        config,
        Context->GetMasterChannel(),
        GetTransaction(false, true),
        Context->GetBlockCache(),
        Request->Path);

    reader->Open();

    auto writer = Context->CreateOutputConsumer();
    ProduceYson(reader, ~writer);
}

//////////////////////////////////////////////////////////////////////////////////

void TWriteCommand::DoExecute()
{
    auto config = UpdateYsonSerializable(
        Context->GetConfig()->TableWriter,
        Request->TableWriter);

    auto writer = New<TTableWriter>(
        config,
        Context->GetMasterChannel(),
        GetTransaction(false, true),
        Context->GetTransactionManager(),
        Request->Path,
        Request->Path.Attributes().Find<TKeyColumns>("sorted_by"));

    writer->Open();

    TTableConsumer consumer(writer);
    auto producer = Context->CreateInputProducer();
    producer.Run(&consumer);

    writer->Close();
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NDriver
} // namespace NYT
