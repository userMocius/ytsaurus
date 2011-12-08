#include "stdafx.h"
#include "file_reader.h"

#include "../misc/serialize.h"
#include "../misc/fs.h"

namespace NYT {
namespace NChunkClient {

///////////////////////////////////////////////////////////////////////////////

using namespace NChunkHolder::NProto;

///////////////////////////////////////////////////////////////////////////////

TFileReader::TFileReader(const Stroka& fileName)
    : FileName(fileName)
    , Opened(false)
    , InfoSize(-1)
    , DataSize(-1)
{ }

void TFileReader::Open()
{
    YASSERT(!Opened);

    Stroka chunkInfoFileName = FileName + ChunkMetaSuffix;
    TFile chunkMetaFile(
        chunkInfoFileName,
        OpenExisting | RdOnly | Seq);
    InfoSize = chunkMetaFile.GetLength();
    TBufferedFileInput chunkMetaInput(chunkMetaFile);
        
    TChunkMetaHeader metaHeader;
    Read(chunkMetaInput, &metaHeader);
    if (metaHeader.Signature != TChunkMetaHeader::ExpectedSignature) {
        ythrow yexception()
            << Sprintf("Incorrect signature in chunk meta header (FileName: %s, Expected: %" PRIx64 ", Found: %" PRIx64")",
                ~FileName,
                TChunkMetaHeader::ExpectedSignature,
                metaHeader.Signature);
    }

    Stroka chunkMetaBlob = chunkMetaInput.ReadAll();
    TRef chunkMetaRef(chunkMetaBlob.begin(), chunkMetaBlob.size());

    auto checksum = GetChecksum(chunkMetaRef);
    if (checksum != metaHeader.Checksum) {
        ythrow yexception()
            << Sprintf("Incorrect checksum in chunk meta file (FileName: %s, Expected: %" PRIx64 ", Found: %" PRIx64")",
                ~FileName,
                metaHeader.Checksum,
                checksum);
    }

    TChunkMeta chunkMeta;
    if (!DeserializeProtobuf(&chunkMeta, chunkMetaRef)) {
        ythrow yexception() << Sprintf("Failed to parse chunk meta (FileName: %s)",
            ~FileName);
    }

    ChunkInfo.set_id(chunkMeta.id());
    ChunkInfo.set_metachecksum(checksum);
    ChunkInfo.mutable_blocks()->MergeFrom(chunkMeta.blocks());
    ChunkInfo.mutable_attributes()->CopyFrom(chunkMeta.attributes());

    DataFile.Reset(new TFile(FileName, OpenExisting | RdOnly));
    DataSize = DataFile->GetLength();

    ChunkInfo.set_size(DataSize + InfoSize);

    Opened = true;
}

TFuture<IAsyncReader::TReadResult>::TPtr
TFileReader::AsyncReadBlocks(const yvector<int>& blockIndexes)
{
    YASSERT(Opened);

    yvector<TSharedRef> blocks;
    blocks.reserve(blockIndexes.ysize());

    for (int index = 0; index < blockIndexes.ysize(); ++index) {
        i32 blockIndex = blockIndexes[index];
        blocks.push_back(ReadBlock(blockIndex));
    }

    return ToFuture(TReadResult(MoveRV(blocks)));
}

TSharedRef TFileReader::ReadBlock(int blockIndex)
{
    YASSERT(Opened);

    i32 blockCount = ChunkInfo.blocks_size();

    if (blockIndex > blockCount || blockIndex < -blockCount) {
        return TSharedRef();
    }

    while (blockIndex < 0) {
        blockIndex += blockCount;
    }

    if (blockIndex >= blockCount) {
        return TSharedRef();
    }

    const TBlockInfo& blockInfo = ChunkInfo.blocks(blockIndex);
    TBlob data(blockInfo.size());
    i64 offset = blockInfo.offset();
    DataFile->Pread(data.begin(), data.size(), offset); 

    TSharedRef result(MoveRV(data));

    TChecksum checksum = GetChecksum(result);
    if (checksum != blockInfo.checksum()) {
        ythrow yexception()
            << Sprintf("Incorrect checksum in chunk block (FileName: %s, BlockIndex: %d, Expected: %" PRIx64 ", Found: %" PRIx64 ")",
                ~FileName,
                blockIndex,
                blockInfo.checksum(),
                checksum);
    }

    return result;
}

i64 TFileReader::GetMetaSize() const
{
    YASSERT(Opened);
    return InfoSize;
}

i64 TFileReader::GetDataSize() const
{
    YASSERT(Opened);
    return DataSize;
}

i64 TFileReader::GetFullSize() const
{
    YASSERT(Opened);
    return InfoSize + DataSize;
}

const TChunkInfo& TFileReader::GetChunkInfo() const
{
    YASSERT(Opened);
    return ChunkInfo;
}

TFuture<IAsyncReader::TGetInfoResult>::TPtr TFileReader::AsyncGetChunkInfo()
{
    return ToFuture(TGetInfoResult(GetChunkInfo()));
}

///////////////////////////////////////////////////////////////////////////////

} // namespace NChunkClient
} // namespace NYT

