﻿#pragma once

#include "public.h"

#include <core/misc/enum.h>
#include <core/misc/ref.h>

#include <ytlib/new_table_client/public.h>

#include <ytlib/api/public.h>

namespace NYT {
namespace NTabletClient {

///////////////////////////////////////////////////////////////////////////////

DECLARE_ENUM(EProtocolCommand,
    // Sentinels:

    ((End)(0))

    // Read commands:
    
    ((LookupRow)(1))
    // Finds a row with a given key and fetches its components.
    //
    // Input:
    //   * Key
    //   * Column filter
    //
    // Output:
    //   * Unversioned rowset containing 0 or 1 rows

    // Write commands:

    ((WriteRow)(2))
    // Inserts a new row or completely replaces an existing one with matching key.
    //
    // Input:
    //   * Unversioned row
    // Output:
    //   None

    ((DeleteRow)(3))
    // Deletes a row with a given key, if it exists.
    //
    // Input:
    //   * Key
    // Output:
    //   None

);

////////////////////////////////////////////////////////////////////////////////

class TWireProtocolWriter
{
public:
    TWireProtocolWriter();
    ~TWireProtocolWriter();

    void WriteCommand(EProtocolCommand command);

    void WriteColumnFilter(const NVersionedTableClient::TColumnFilter& filter);

    void WriteTableSchema(const NVersionedTableClient::TTableSchema& schema);

    void WriteMessage(const ::google::protobuf::MessageLite& message);

    void WriteUnversionedRow(NVersionedTableClient::TUnversionedRow row);
    void WriteUnversionedRow(const std::vector<NVersionedTableClient::TUnversionedValue>& row);
    void WriteUnversionedRowset(const std::vector<NVersionedTableClient::TUnversionedRow>& rowset);
    NVersionedTableClient::ISchemedWriterPtr CreateSchemedRowsetWriter();

    Stroka Finish();

private:
    class TImpl;
    class TSchemedRowsetWriter;

    std::unique_ptr<TImpl> Impl_;

};

///////////////////////////////////////////////////////////////////////////////

class TWireProtocolReader
{
public:
    explicit TWireProtocolReader(const Stroka& data); 
    ~TWireProtocolReader();

    EProtocolCommand ReadCommand();
    
    NVersionedTableClient::TColumnFilter ReadColumnFilter();

    NVersionedTableClient::TTableSchema ReadTableSchema();

    void ReadMessage(::google::protobuf::MessageLite* message);

    NVersionedTableClient::TUnversionedRow ReadUnversionedRow();
    void ReadUnversionedRowset(std::vector<NVersionedTableClient::TUnversionedRow>* rowset);
    NVersionedTableClient::ISchemedReaderPtr CreateSchemedRowsetReader();

private:
    class TImpl;
    class TSchemedRowsetReader;

    std::unique_ptr<TImpl> Impl_;

};

///////////////////////////////////////////////////////////////////////////////

} // namespace NTabletClient
} // namespace NYT

