#include "stdafx.h"
#include "schemaless_block_writer.h"

#include <core/misc/serialize.h>

namespace NYT {
namespace NVersionedTableClient {

using namespace NProto;

////////////////////////////////////////////////////////////////////////////////

int THorizontalSchemalessBlockWriter::FormatVersion = ETableChunkFormat::SchemalessHorizontal;

THorizontalSchemalessBlockWriter::THorizontalSchemalessBlockWriter()
    : RowCount_(0)
{ }

THorizontalSchemalessBlockWriter::THorizontalSchemalessBlockWriter(int /* keyColumns */)
    : RowCount_(0)
{ }

void THorizontalSchemalessBlockWriter::WriteRow(TUnversionedRow row)
{
    ++RowCount_;

    WritePod(Offsets_, static_cast<ui32>(Data_.GetSize()));

    int size = 0;
    for (auto it = row.Begin(); it != row.End(); ++it) {
        size += GetByteSize(*it);
    }

    char* begin = Data_.Allocate(size);
    char* current = begin;
    for (auto it = row.Begin(); it != row.End(); ++it) {
        current += WriteValue(current, *it);
    }

    Data_.Skip(current - begin);
}

TBlock THorizontalSchemalessBlockWriter::FlushBlock()
{
    std::vector<TSharedRef> blockParts;
    auto offsets = Offsets_.FlushBuffer();
    blockParts.insert(blockParts.end(), offsets.begin(), offsets.end());

    auto data = Data_.FlushBuffer();
    blockParts.insert(blockParts.end(), data.begin(), data.end());

    TBlockMeta meta;
    meta.set_row_count(RowCount_);
    meta.set_block_size(GetBlockSize());

    TBlock block;
    block.Data.swap(blockParts);
    block.Meta.Swap(&meta);

    return block;
}

int THorizontalSchemalessBlockWriter::GetBlockSize() const
{
    return Offsets_.GetSize() + Data_.GetSize();
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NVersionedTableClient
} // namespace NYT
