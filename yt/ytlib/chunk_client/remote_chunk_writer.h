#pragma once

#include "chunk_writer.h"

#include "../misc/lazy_ptr.h"
#include "../misc/config.h"
#include "../misc/semaphore.h"
#include "../misc/thread_affinity.h"
#include "../rpc/client.h"
#include "../chunk_client/common.h"
#include "../chunk_holder/chunk_holder_rpc.h"
#include "../actions/action_queue.h"

#include <util/generic/deque.h>

namespace NYT
{

///////////////////////////////////////////////////////////////////////////////

class TRemoteChunkWriter
    : public IChunkWriter
{
public:
    typedef TIntrusivePtr<TRemoteChunkWriter> TPtr;

    struct TConfig
    {
        //! Maximum number of blocks that may be concurrently present in the window.
        int WindowSize;
        
        //! Maximum group size (in bytes).
        int GroupSize;
        
        //! RPC requests timeout.
        /*!
         *  This timeout is especially useful for PutBlocks calls to ensure that
         *  uploading is not stalled.
         */
        TDuration RpcTimeout;

        //! Timeout specifying a maxmimum allowed period of time without RPC request to ChunkHolder
        /*!
         * If no activity occured during this period -- PingSession call will be send
         */
        TDuration SessionTimeout;

        TConfig()
            : WindowSize(16)
            , GroupSize(1024 * 1024)
            , RpcTimeout(TDuration::Seconds(30))
            , SessionTimeout(TDuration::Seconds(10))
        { }

        void Read(TJsonObject* config);
    };

    DECLARE_THREAD_AFFINITY_SLOT(ClientThread);
    DECLARE_THREAD_AFFINITY_SLOT(WriterThread);

    /*!
     * \note Thread Affinity: ClientThread.
     */
    TRemoteChunkWriter(
        const TConfig& config, 
        const TChunkId& chunkId,
        const yvector<Stroka>& addresses);

    /*!
     * \note Thread Affinity: ClientThread.
     */
    EResult AsyncWriteBlock(const TSharedRef& data, TAsyncResult<TVoid>::TPtr* ready);

    /*!
     * \note Thread Affinity: ClientThread.
     */
    TAsyncResult<EResult>::TPtr AsyncClose();


    /*!
     * \note Thread Affinity: Any thread.
     */
    void Cancel();

    ~TRemoteChunkWriter();

    /*!
     * \note Thread Affinity: Any thread.
     */
    static Stroka GetDebugInfo();

private:

    //! A group is a bunch of blocks that is sent in a single RPC request.
    class TGroup;
    typedef TIntrusivePtr<TGroup> TGroupPtr;

    struct TNode;
    typedef TIntrusivePtr<TNode> TNodePtr;
    
    typedef ydeque<TGroupPtr> TWindow;

    typedef NChunkHolder::TChunkHolderProxy TProxy;

    USE_RPC_PROXY_METHOD(TProxy, StartChunk);
    USE_RPC_PROXY_METHOD(TProxy, FinishChunk);
    USE_RPC_PROXY_METHOD(TProxy, PutBlocks);
    USE_RPC_PROXY_METHOD(TProxy, SendBlocks);
    USE_RPC_PROXY_METHOD(TProxy, FlushBlock);
    USE_RPC_PROXY_METHOD(TProxy, PingSession);

private:
    //! Manages all internal upload functionality, 
    //! sends out RPC requests, and handles responses.
    static TLazyPtr<TActionQueue> WriterThread;

    TChunkId ChunkId;
    const TConfig Config;

    DECLARE_ENUM(EWriterState,
        (Initializing)
        (Writing)
        (Closed)
        (Canceled)
    );

    //! Set in #WriterThread, read from client and writer threads
    EWriterState State;

    //! This flag is raised whenever #Close is invoked.
    //! All access to this flag happens from #WriterThread.
    bool IsCloseRequested;

    // Result of write session, set when session is completed.
    // Is returned from #AsyncClose
    TAsyncResult<EResult>::TPtr Result;

    TWindow Window;
    TSemaphore WindowSlots;

    yvector<TNodePtr> Nodes;

    //! Number of nodes that are still alive.
    int AliveNodeCount;

    //! A new group of blocks that is currently being filled in by the client.
    //! All access to this field happens from client thread.
    TGroupPtr CurrentGroup;

    //! Number of blocks that are already added via #AddBlock. 
    int BlockCount;

    TAsyncResult<TVoid>::TPtr WindowReady;

    static NRpc::TChannelCache ChannelCache;

    /* ToDo: implement metrics

    TMetric StartChunkTiming;
    TMetric PutBlocksTiming;
    TMetric SendBlocksTiming;
    TMetric FlushBlockTiming;
    TMetric FinishChunkTiming;*/

private:
    /*!
     * Invoked from #Close.
     * \note Thread Affinity: WriterThread
     * Sets #IsCloseRequested.
     */
    void DoClose();
    
    /*!
     * Invoked from #Cancel
     * \note Thread Affinity: WriterThread.
     */
    void DoCancel();

    /*!
     * \note Thread Affinity: WriterThread
     */
    void AddGroup(TGroupPtr group);

    /*!
     * \note Thread Affinity: WriterThread
     */
    void RegisterReadyEvent(TAsyncResult<TVoid>::TPtr windowReady);

    /*!
     * \note Thread Affinity: WriterThread
     */
    void OnNodeDied(int node);

    /*!
     * \note Thread Affinity: WriterThread
     */
    void ReleaseSlots(int count);

    /*!
     * \note Thread Affinity: WriterThread
     */
    void ShiftWindow();

    /*!
     * \note Thread Affinity: WriterThread
     */
    TInvFlushBlock::TPtr FlushBlock(int node, int blockIndex);

    /*!
     * \note Thread Affinity: WriterThread
     */
    void OnFlushedBlock(int node, int blockIndex);

    /*!
     * \note Thread Affinity: WriterThread
     */
    void OnWindowShifted(int blockIndex);

    /*!
     * \note Thread Affinity: ClientThread
     */
    void InitializeNodes(const yvector<Stroka>& addresses);

    /*!
     * \note Thread Affinity: ClientThread
     */
    void StartSession();

    /*!
     * \note Thread Affinity: ClientThread
     */
    TInvStartChunk::TPtr StartChunk(int node);

    /*!
     * \note Thread Affinity: WriterThread
     */
    void OnStartedChunk(int node);

    /*!
     * \note Thread Affinity: WriterThread
     */
    void OnSessionStarted();

    /*!
     * \note Thread Affinity: WriterThread
     */
    void CloseSession();

    /*!
     * \note Thread Affinity: WriterThread
     */
    TInvFinishChunk::TPtr FinishChunk(int node);

    /*!
     * \note Thread Affinity: WriterThread
     */
    void OnFinishedChunk(int node);

    /*!
     * \note Thread Affinity: WriterThread
     */
    void OnFinishedSession();

    /*!
     * \note Thread Affinity: WriterThread
     */
    void PingSession(int node);

    /*!
     * \note Thread Affinity: WriterThread
     */
    void SchedulePing(int node);

    /*!
     * \note Thread Affinity: WriterThread
     */
    void CancelPing(int node);

    /*!
     * \note Thread Affinity: WriterThread
     */
    void CancelAllPings();

    /*!
     * \note Thread Affinity: WriterThread
     */
    template<class TResponse>
    void CheckResponse(typename TResponse::TPtr rsp, int node, IAction::TPtr onSuccess);
};

///////////////////////////////////////////////////////////////////////////////

}

