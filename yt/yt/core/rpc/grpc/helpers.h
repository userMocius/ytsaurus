#pragma once

#include "private.h"

#include <yt/yt/core/concurrency/async_rw_lock.h>

#include <yt/yt/core/crypto/public.h>

#include <library/cpp/yt/memory/ref.h>

#include <library/cpp/yt/small_containers/compact_vector.h>

#include <contrib/libs/grpc/include/grpc/grpc.h>
#include <contrib/libs/grpc/include/grpc/grpc_security.h>
#include <contrib/libs/grpc/include/grpc/impl/codegen/grpc_types.h>
#include <contrib/libs/grpc/include/grpc/byte_buffer_reader.h>

namespace NYT::NRpc::NGrpc {

////////////////////////////////////////////////////////////////////////////////

using TGprString = std::unique_ptr<char, void(*)(void*)>;
TGprString MakeGprString(char* str);

TStringBuf ToStringBuf(const grpc_slice& slice);
TString ToString(const grpc_slice& slice);

////////////////////////////////////////////////////////////////////////////////

template <class T, void(*Dtor)(T*)>
class TGrpcObjectPtr
{
public:
    TGrpcObjectPtr();
    explicit TGrpcObjectPtr(T* obj);
    TGrpcObjectPtr(TGrpcObjectPtr&& other);
    ~TGrpcObjectPtr();

    TGrpcObjectPtr& operator=(TGrpcObjectPtr&& other);

    TGrpcObjectPtr& operator=(const TGrpcObjectPtr&) = delete;
    TGrpcObjectPtr(const TGrpcObjectPtr& other) = delete;

    T* Unwrap();
    const T* Unwrap() const;
    explicit operator bool() const;

    T** GetPtr();
    void Reset();

private:
    T* Ptr_;
};

using TGrpcByteBufferPtr = TGrpcObjectPtr<grpc_byte_buffer, grpc_byte_buffer_destroy>;
using TGrpcCallPtr = TGrpcObjectPtr<grpc_call, grpc_call_unref>;
using TGrpcChannelPtr = TGrpcObjectPtr<grpc_channel, grpc_channel_destroy>;
using TGrpcCompletionQueuePtr = TGrpcObjectPtr<grpc_completion_queue, grpc_completion_queue_destroy>;
using TGrpcServerPtr = TGrpcObjectPtr<grpc_server, grpc_server_destroy>;
using TGrpcChannelCredentialsPtr = TGrpcObjectPtr<grpc_channel_credentials, grpc_channel_credentials_release>;
using TGrpcServerCredentialsPtr = TGrpcObjectPtr<grpc_server_credentials, grpc_server_credentials_release>;
using TGrpcAuthContextPtr = TGrpcObjectPtr<grpc_auth_context, grpc_auth_context_release>;

////////////////////////////////////////////////////////////////////////////////

//! Completion queue must be accessed under read lock
//! in order to prohibit creating new requests after shutting completion queue down.
class TGuardedGrpcCompletitionQueuePtr
{
public:
    explicit TGuardedGrpcCompletitionQueuePtr(TGrpcCompletionQueuePtr completionQueuePtr);

    template <class TOperationTraits>
    class TLockGuard
        : public TNonCopyable
    {
    public:
        explicit TLockGuard(TGuardedGrpcCompletitionQueuePtr& guardedCompletionQueue)
            : GuardedCompletionQueue_(guardedCompletionQueue)
        {
            Guard_ = WaitFor(NConcurrency::TAsyncReaderWriterLockGuard<TOperationTraits>::Acquire(&GuardedCompletionQueue_.Lock_))
                .ValueOrThrow();
        }

        TLockGuard(TLockGuard&& guard)
            : GuardedCompletionQueue_(guard.GuardedCompletionQueue_)
            , Guard_(std::move(guard.Guard_))
        { }

        ~TLockGuard()
        {
            Unlock();
        }

        grpc_completion_queue* Unwrap()
        {
            return GuardedCompletionQueue_.UnwrapUnsafe();
        }

        void Unlock()
        {
            if (Guard_) {
                Guard_.Reset();
            }
        }

    private:
        TGuardedGrpcCompletitionQueuePtr& GuardedCompletionQueue_;
        TIntrusivePtr<NConcurrency::TAsyncReaderWriterLockGuard<TOperationTraits>> Guard_;
    };

    std::optional<TLockGuard<NConcurrency::TAsyncLockReaderTraits>> TryLock();

    void Shutdown();

    //! Must be done after Shutdown.
    void Reset();

    //! Access completion queue without taking locks and checking the state.
    //! Used in GrpcServer (lock is redundant there because it synchronizes
    //! with grpc_completion_queue shutdown via thread priorities)
    //! and in DispatcherThreads (operation grpc_completion_queue_next
    //! does not need to be synchronized).
    grpc_completion_queue* UnwrapUnsafe();

private:
    enum class EState
    {
        Opened = 1,
        Shutdown = 2
    };

    TGrpcCompletionQueuePtr CompletionQueuePtr_;
    NConcurrency::TAsyncReaderWriterLock Lock_;
    EState State_{EState::Opened};
};

////////////////////////////////////////////////////////////////////////////////

template <class T, void(*Ctor)(T*), void(*Dtor)(T*)>
class TGrpcObject
{
public:
    TGrpcObject();
    ~TGrpcObject();

    TGrpcObject(const TGrpcObject&) = delete;
    TGrpcObject(TGrpcObject&&) = delete;
    TGrpcObject& operator =(const TGrpcObject& other) = delete;
    TGrpcObject& operator =(TGrpcObject&& other) = delete;

    T* Unwrap();
    T* operator->();

protected:
    T Native_;

};

////////////////////////////////////////////////////////////////////////////////

class TGrpcMetadataArray
    : public TGrpcObject<grpc_metadata_array, grpc_metadata_array_init, grpc_metadata_array_destroy>
{
public:
    TStringBuf Find(const char* key) const;

};

////////////////////////////////////////////////////////////////////////////////

using TGrpcCallDetails = TGrpcObject<grpc_call_details, grpc_call_details_init, grpc_call_details_destroy>;

////////////////////////////////////////////////////////////////////////////////

class TGrpcMetadataArrayBuilder
{
public:
    void Add(const char* key, TString value);
    size_t GetSize() const;

    grpc_metadata* Unwrap();

private:
    static constexpr size_t TypicalSize = 4;
    TCompactVector<grpc_metadata, TypicalSize> NativeMetadata_;
    TCompactVector<TString, TypicalSize> Strings_;
};

////////////////////////////////////////////////////////////////////////////////

class TGrpcChannelArgs
{
public:
    explicit TGrpcChannelArgs(const THashMap<TString, NYTree::INodePtr>& args);

    grpc_channel_args* Unwrap();

private:
    grpc_channel_args Native_;
    std::vector<grpc_arg> Items_;
};

////////////////////////////////////////////////////////////////////////////////

class TGrpcByteBufferStream
    : public IInputStream
{
public:
    explicit TGrpcByteBufferStream(grpc_byte_buffer* buffer);
    ~TGrpcByteBufferStream();

    bool IsExhausted() const;

private:
    grpc_byte_buffer_reader Reader_;

    grpc_slice CurrentSlice_;
    size_t AvailableBytes_ = 0;
    size_t RemainingBytes_;

    virtual size_t DoRead(void* buf, size_t len) override;

    bool ReadNextSlice();
};

////////////////////////////////////////////////////////////////////////////////

class TGrpcPemKeyCertPair
{
public:
    TGrpcPemKeyCertPair(
        TString privateKey,
        TString certChain);

    grpc_ssl_pem_key_cert_pair* Unwrap();

private:
    TString PrivateKey_;
    TString CertChain_;
    grpc_ssl_pem_key_cert_pair Native_;
};

////////////////////////////////////////////////////////////////////////////////

struct TMessageWithAttachments
{
    TSharedRef Message;
    std::vector<TSharedRef> Attachments;
};

TMessageWithAttachments ByteBufferToMessageWithAttachments(
    grpc_byte_buffer* buffer,
    std::optional<ui32> messageBodySize);

TGrpcByteBufferPtr MessageWithAttachmentsToByteBuffer(
    const TMessageWithAttachments& messageWithAttachments);

TSharedRef ExtractMessageFromEnvelopedMessage(const TSharedRef& data);

////////////////////////////////////////////////////////////////////////////////

TErrorCode StatusCodeToErrorCode(grpc_status_code statusCode);

TString SerializeError(const TError& error);
TError DeserializeError(TStringBuf serializedError);

TGrpcPemKeyCertPair LoadPemKeyCertPair(const TSslPemKeyCertPairConfigPtr& config);
TGrpcChannelCredentialsPtr LoadChannelCredentials(const TChannelCredentialsConfigPtr& config);
TGrpcServerCredentialsPtr LoadServerCredentials(const TServerCredentialsConfigPtr& config);
std::optional<TString> ParseIssuerFromX509(TStringBuf x509String);

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NRpc::NGrpc

#define HELPERS_INL_H_
#include "helpers-inl.h"
#undef HELPERS_INL_H_
