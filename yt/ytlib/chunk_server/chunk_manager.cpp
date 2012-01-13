#include "stdafx.h"
#include "chunk_manager.h"
#include "chunk_manager.pb.h"
#include "chunk_placement.h"
#include "chunk_replication.h"
#include "holder_lease_tracker.h"
#include "holder_statistics.h"

#include <ytlib/transaction_server/transaction_manager.h>
#include <ytlib/meta_state/meta_state_manager.h>
#include <ytlib/meta_state/composite_meta_state.h>
#include <ytlib/meta_state/map.h>
#include <ytlib/misc/foreach.h>
#include <ytlib/misc/serialize.h>
#include <ytlib/misc/guid.h>
#include <ytlib/misc/assert.h>
#include <ytlib/misc/id_generator.h>
#include <ytlib/misc/string.h>

namespace NYT {
namespace NChunkServer {

using namespace NProto;
using namespace NMetaState;
using namespace NTransactionServer;
using namespace NChunkClient;
using namespace NChunkHolder;

////////////////////////////////////////////////////////////////////////////////

static NLog::TLogger& Logger = ChunkServerLogger;

////////////////////////////////////////////////////////////////////////////////

class TChunkManager::TImpl
    : public NMetaState::TMetaStatePart
{
public:
    typedef TIntrusivePtr<TImpl> TPtr;

    TImpl(
        TConfig* config,
        TChunkManager* chunkManager,
        NMetaState::IMetaStateManager* metaStateManager,
        NMetaState::TCompositeMetaState* metaState,
        TTransactionManager* transactionManager,
        IHolderRegistry* holderRegistry)
        : TMetaStatePart(metaStateManager, metaState)
        , Config(config)
        // TODO: this makes a cyclic reference, don't forget to drop it in Stop
        , ChunkManager(chunkManager)
        , TransactionManager(transactionManager)
        , HolderRegistry(holderRegistry)
        , ChunkIdGenerator(ChunkIdSeed)
        , ChunkListIdGenerator(ChunkListIdSeed)
    {
        YASSERT(chunkManager);
        YASSERT(transactionManager);
        YASSERT(holderRegistry);

        RegisterMethod(this, &TImpl::HeartbeatRequest);
        RegisterMethod(this, &TImpl::HeartbeatResponse);
        RegisterMethod(this, &TImpl::RegisterHolder);
        RegisterMethod(this, &TImpl::UnregisterHolder);
        RegisterMethod(this, &TImpl::CreateChunks);
        RegisterMethod(this, &TImpl::ConfirmChunks);
        RegisterMethod(this, &TImpl::CreateChunkLists);
        RegisterMethod(this, &TImpl::AttachChunkTrees);
        RegisterMethod(this, &TImpl::DetachChunkTrees);

        metaState->RegisterLoader(
            "ChunkManager.1",
            FromMethod(&TChunkManager::TImpl::Load, TPtr(this)));
        metaState->RegisterSaver(
            "ChunkManager.1",
            FromMethod(&TChunkManager::TImpl::Save, TPtr(this)));

        transactionManager->OnTransactionCommitted().Subscribe(FromMethod(
            &TThis::OnTransactionCommitted,
            TPtr(this)));
        transactionManager->OnTransactionAborted().Subscribe(FromMethod(
            &TThis::OnTransactionAborted,
            TPtr(this)));
    }


    TMetaChange<THolderId>::TPtr InitiateRegisterHolder(
        const TMsgRegisterHolder& message)
    {
        return CreateMetaChange(
            ~MetaStateManager,
            message,
            &TThis::RegisterHolder,
            this);
    }

    TMetaChange<TVoid>::TPtr  InitiateUnregisterHolder(
        const TMsgUnregisterHolder& message)
    {
        return CreateMetaChange(
            ~MetaStateManager,
            message,
            &TThis::UnregisterHolder,
            this);
    }

    TMetaChange<TVoid>::TPtr InitiateHeartbeatRequest(
        const TMsgHeartbeatRequest& message)
    {
        return CreateMetaChange(
            ~MetaStateManager,
            message,
            &TThis::HeartbeatRequest,
            this);
    }

    TMetaChange<TVoid>::TPtr InitiateHeartbeatResponse(
        const TMsgHeartbeatResponse& message)
    {
        return CreateMetaChange(
            ~MetaStateManager,
            message,
            &TThis::HeartbeatResponse,
            this);
    }

    TMetaChange< yvector<TChunkId> >::TPtr InitiateAllocateChunk(
        const TMsgCreateChunks& message)
    {
        return CreateMetaChange(
            ~MetaStateManager,
            message,
            &TThis::CreateChunks,
            this);
    }

    TMetaChange<TVoid>::TPtr InitiateConfirmChunks(
        const TMsgConfirmChunks& message)
    {
        return CreateMetaChange(
            ~MetaStateManager,
            message,
            &TThis::ConfirmChunks,
            this);
    }

    TMetaChange< yvector<TChunkListId> >::TPtr InitiateCreateChunkLists(
        const TMsgCreateChunkLists& message)
    {
        return CreateMetaChange(
            ~MetaStateManager,
            message,
            &TThis::CreateChunkLists,
            this);
    }

    TMetaChange<TVoid>::TPtr InitiateAttachChunkTrees(
        const TMsgAttachChunkTrees& message)
    {
        return CreateMetaChange(
            ~MetaStateManager,
            message,
            &TThis::AttachChunkTrees,
            this);
    }

    TMetaChange<TVoid>::TPtr InitiateDetachChunkTrees(
        const TMsgDetachChunkTrees& message)
    {
        return CreateMetaChange(
            ~MetaStateManager,
            message,
            &TThis::DetachChunkTrees,
            this);
    }


    DECLARE_METAMAP_ACCESSORS(Chunk, TChunk, TChunkId);
    DECLARE_METAMAP_ACCESSORS(ChunkList, TChunkList, TChunkListId);
    DECLARE_METAMAP_ACCESSORS(Holder, THolder, THolderId);
    DECLARE_METAMAP_ACCESSORS(JobList, TJobList, TChunkId);
    DECLARE_METAMAP_ACCESSORS(Job, TJob, TJobId);

    DEFINE_BYREF_RW_PROPERTY(TParamSignal<const THolder&>, HolderRegistered);
    DEFINE_BYREF_RW_PROPERTY(TParamSignal<const THolder&>, HolderUnregistered);


    const THolder* FindHolder(const Stroka& address)
    {
        auto it = HolderAddressMap.find(address);
        return it == HolderAddressMap.end() ? NULL : FindHolder(it->Second());
    }

    const TReplicationSink* FindReplicationSink(const Stroka& address)
    {
        auto it = ReplicationSinkMap.find(address);
        return it == ReplicationSinkMap.end() ? NULL : &it->Second();
    }

    yvector<THolderId> AllocateUploadTargets(int replicaCount)
    {
        auto holderIds = ChunkPlacement->GetUploadTargets(replicaCount);
        FOREACH(auto holderId, holderIds) {
            const auto& holder = GetHolder(holderId);
            ChunkPlacement->OnSessionHinted(holder);
        }
        return holderIds;
    }


    TChunkList& CreateChunkList()
    {
        auto chunkListId = ChunkListIdGenerator.Next();
        auto* chunkList = new TChunkList(chunkListId);
        ChunkListMap.Insert(chunkListId, chunkList);
        return *chunkList;
    }


    void RefChunkTree(const TChunkTreeId& treeId)
    {
        switch (GetChunkTreeKind(treeId)) {
            case EChunkTreeKind::Chunk:
                RefChunkTree(GetChunkForUpdate(treeId));
                break;
            case EChunkTreeKind::ChunkList:
                RefChunkTree(GetChunkListForUpdate(treeId));
                break;
            default:
                YUNREACHABLE();
        }
    }

    void UnrefChunkTree(const TChunkTreeId& treeId)
    {
        switch (GetChunkTreeKind(treeId)) {
            case EChunkTreeKind::Chunk:
                UnrefChunkTree(GetChunkForUpdate(treeId));
                break;
            case EChunkTreeKind::ChunkList:
                UnrefChunkTree(GetChunkListForUpdate(treeId));
                break;
            default:
                YUNREACHABLE();
        }
    }


    void RefChunkTree(TChunk& chunk)
    {
        int refCounter = chunk.Ref();
        LOG_DEBUG_IF(!IsRecovery(), "Chunk referenced (ChunkId: %s, RefCounter: %d)",
            ~chunk.GetId().ToString(),
            refCounter);
    }

    void UnrefChunkTree(TChunk& chunk)
    {
        int refCounter = chunk.Unref();
        LOG_DEBUG_IF(!IsRecovery(), "Chunk unreferenced (ChunkId: %s, RefCounter: %d)",
            ~chunk.GetId().ToString(),
            refCounter);

        if (refCounter == 0) {
            RemoveChunk(chunk);
        }
    }


    void RefChunkTree(TChunkList& chunkList)
    {
        int refCounter = chunkList.Ref();
        LOG_DEBUG_IF(!IsRecovery(), "Chunk list referenced (ChunkListId: %s, RefCounter: %d)",
            ~chunkList.GetId().ToString(),
            refCounter);
        
    }

    void UnrefChunkTree(TChunkList& chunkList)
    {
        int refCounter = chunkList.Unref();
        LOG_DEBUG_IF(!IsRecovery(), "Chunk list unreferenced (ChunkListId: %s, RefCounter: %d)",
            ~chunkList.GetId().ToString(),
            refCounter);

        if (refCounter == 0) {
            RemoveChunkList(chunkList);
        }
    }


    void RunJobControl(
        const THolder& holder,
        const yvector<TJobInfo>& runningJobs,
        yvector<TJobStartInfo>* jobsToStart,
        yvector<TJobId>* jobsToStop)
    {
        ChunkReplication->RunJobControl(
            holder,
            runningJobs,
            jobsToStart,
            jobsToStop);
    }

    const yhash_set<TChunkId>& LostChunkIds() const;
    const yhash_set<TChunkId>& OverreplicatedChunkIds() const;
    const yhash_set<TChunkId>& UnderreplicatedChunkIds() const;

private:
    typedef TImpl TThis;

    TConfig::TPtr Config;
    TChunkManager::TPtr ChunkManager;
    TTransactionManager::TPtr TransactionManager;
    IHolderRegistry::TPtr HolderRegistry;
    
    TChunkPlacement::TPtr ChunkPlacement;
    TChunkReplication::TPtr ChunkReplication;
    THolderLeaseTracker::TPtr HolderLeaseTracking;
    
    TIdGenerator<TChunkId> ChunkIdGenerator;
    TIdGenerator<TChunkId> ChunkListIdGenerator;
    TIdGenerator<THolderId> HolderIdGenerator;

    TMetaStateMap<TChunkId, TChunk> ChunkMap;
    TMetaStateMap<TChunkListId, TChunkList> ChunkListMap;
    TMetaStateMap<THolderId, THolder> HolderMap;
    yhash_map<Stroka, THolderId> HolderAddressMap;
    TMetaStateMap<TChunkId, TJobList> JobListMap;
    TMetaStateMap<TJobId, TJob> JobMap;
    yhash_map<Stroka, TReplicationSink> ReplicationSinkMap;


    yvector<TChunkId> CreateChunks(const TMsgCreateChunks& message)
    {
        auto transactionId = TTransactionId::FromProto(message.transaction_id());
        int chunkCount = message.chunk_count();

        yvector<TChunkId> chunkIds;
        chunkIds.reserve(chunkCount);
        for (int index = 0; index < chunkCount; ++index) {
            auto chunkId = ChunkIdGenerator.Next();
            auto* chunk = new TChunk(chunkId);
            ChunkMap.Insert(chunkId, chunk);

            // The newly created chunk form an unbound chunk tree, which is referenced by the transaction.
            auto& transaction = TransactionManager->GetTransactionForUpdate(transactionId);
            YVERIFY(transaction.UnboundChunkTreeIds().insert(chunkId).second);
            RefChunkTree(*chunk);

            chunkIds.push_back(chunkId);
        }

        LOG_INFO_IF(!IsRecovery(), "Chunks allocated (ChunkIds: [%s], TransactionId: %s)",
            ~JoinToString(chunkIds),
            ~transactionId.ToString());

        return chunkIds;
    }

    TVoid ConfirmChunks(const TMsgConfirmChunks& message)
    {
        auto transactionId = TTransactionId::FromProto(message.transaction_id());
        auto& transaction = TransactionManager->GetTransactionForUpdate(transactionId);
        
        FOREACH (const auto& chunkInfo, message.chunks()) {
            auto chunkId = TChunkId::FromProto(chunkInfo.chunk_id());
            auto& chunk = ChunkMap.GetForUpdate(chunkId);
            
            auto& holderAddresses = chunkInfo.holder_addresses();
            YASSERT(holderAddresses.size() != 0);

            FOREACH (const auto& address, holderAddresses) {
                auto it = HolderAddressMap.find(address);
                if (it == HolderAddressMap.end()) {
                    LOG_WARNING("Chunk is confirmed at unknown holder (ChunkId: %s, HolderAddress: %s)",
                        ~chunkId.ToString(),
                        ~address);
                    continue;
                }
                auto holderId = it->Second();
                chunk.AddLocation(holderId, false);

                auto& holder = HolderMap.GetForUpdate(holderId);
                // TODO: mark it unapproved
                holder.AddChunk(chunkId, false);

                if (IsLeader()) {
                    ChunkReplication->OnReplicaAdded(holder, chunk);
                }
            }

            // Skip chunks that are already confirmed.
            if (chunk.IsConfirmed()) {
                continue;
            }

            TBlob blob;
            if (!SerializeProtobuf(&chunkInfo.attributes(), &blob)) {
                LOG_FATAL("Error serializing chunk attributes (ChunkId: %s)", ~chunkId.ToString());
            }
            
            chunk.SetAttributes(TSharedRef(MoveRV(blob)));

            LOG_INFO_IF(!IsRecovery(), "Chunk confirmed (ChunkId: %s, TransactionId: %s)",
                ~chunkId.ToString(),
                ~transactionId.ToString());
        }

        return TVoid();
    }

    yvector<TChunkListId> CreateChunkLists(const TMsgCreateChunkLists& message)
    {
        auto transactionId = TTransactionId::FromProto(message.transaction_id());
        auto chunkListCount = message.chunk_list_count();

        auto& transaction = TransactionManager->GetTransactionForUpdate(transactionId);

        yvector<TChunkListId> chunkListIds;
        chunkListIds.reserve(chunkListCount);

        for (int index = 0; index < chunkListCount; ++index) {
            auto& chunkList = CreateChunkList();
            auto chunkListId = chunkList.GetId();
            YVERIFY(transaction.UnboundChunkTreeIds().insert(chunkListId).second);
            chunkListIds.push_back(chunkListId);
        }

        LOG_INFO_IF(!IsRecovery(), "Chunk lists created (ChunkListIds: [%s], TransactionId: %s)",
            ~JoinToString(chunkListIds),
            ~transactionId.ToString());

        return chunkListIds;
    }

    TVoid AttachChunkTrees(const TMsgAttachChunkTrees& message)
    {
        auto transactionId = TTransactionId::FromProto(message.transaction_id());
        auto& transaction = TransactionManager->GetTransactionForUpdate(transactionId);

        auto chunkTreeIds = FromProto<TChunkTreeId>(message.chunk_tree_ids());

        auto parentId = TChunkListId::FromProto(message.parent_id());
        auto& parent = GetChunkListForUpdate(parentId);

        FOREACH (const auto& chunkTreeId, chunkTreeIds) {
            // TODO(babenko): check that all attached chunks are confirmed.
            parent.ChildrenIds().push_back(chunkTreeId);
            if (transaction.UnboundChunkTreeIds().erase(chunkTreeId) != 1) {
                RefChunkTree(chunkTreeId);
            }
        }

        LOG_INFO_IF(!IsRecovery(), "Chunks trees attached (ChunkTreeIds: [%s], ParentId: %s, TransactionId: %s)",
            ~JoinToString(chunkTreeIds),
            ~parentId.ToString(),
            ~transactionId.ToString());

        return TVoid();
    }

    TVoid DetachChunkTrees(const TMsgDetachChunkTrees& message)
    {
        auto transactionId = TTransactionId::FromProto(message.transaction_id());
        auto& transaction = TransactionManager->GetTransactionForUpdate(transactionId);

        auto chunkTreeIdsList = FromProto<TChunkTreeId>(message.chunk_tree_ids());

        auto parentId = TChunkListId::FromProto(message.parent_id());
        if (parentId == NullChunkListId) {
            FOREACH (const auto& childId, chunkTreeIdsList) {
                if (transaction.UnboundChunkTreeIds().erase(childId) == 1) {
                    UnrefChunkTree(childId);
                }
            }
        } else {
            yhash_set<TChunkTreeId> chunkTreeIdsSet(chunkTreeIdsList.begin(), chunkTreeIdsList.end());
            auto& childrenIds = GetChunkListForUpdate(parentId).ChildrenIds();
            auto it = childrenIds.begin();
            auto jt = it;
            while (it != childrenIds.end()) {
                const auto& childId = *it;
                if (chunkTreeIdsSet.find(childId) == chunkTreeIdsSet.end()) {
                    *jt++ = *it;
                } else {
                    UnrefChunkTree(childId);
                }
            }
            childrenIds.erase(jt, childrenIds.end());
        }

        LOG_INFO_IF(!IsRecovery(), "Chunks trees detached (ChunkTreeIds: [%s], ParentId: %s, TransactionId: %s)",
            ~JoinToString(chunkTreeIdsList),
            ~parentId.ToString(),
            ~transactionId.ToString());

        return TVoid();
    }


    void RemoveChunk(TChunk& chunk)
    {
        auto chunkId = chunk.GetId();

        // Unregister chunk replicas from all known locations.
        FOREACH (auto holderId, chunk.StoredLocations()) {
            DoRemoveChunkFromLocation(chunk, false, holderId);
        }

        if (~chunk.CachedLocations()) {
            FOREACH (auto holderId, *chunk.CachedLocations()) {
                DoRemoveChunkFromLocation(chunk, true, holderId);
            }
        }

        ChunkMap.Remove(chunkId);

        LOG_INFO_IF(!IsRecovery(), "Chunk removed (ChunkId: %s)", ~chunkId.ToString());
    }

    void DoRemoveChunkFromLocation(TChunk& chunk, bool cached, THolderId holderId)
    {
        auto chunkId = chunk.GetId();
        auto& holder = GetHolderForUpdate(holderId);
        holder.RemoveChunk(chunkId, cached);

        if (!cached && IsLeader()) {
            ChunkReplication->ScheduleChunkRemoval(holder, chunkId);
        }
    }

    void RemoveChunkList(TChunkList& chunkList)
    {
        auto chunkListId = chunkList.GetId();

        // Drop references to children.
        FOREACH (const auto& treeId, chunkList.ChildrenIds()) {
            UnrefChunkTree(treeId);
        }

        ChunkListMap.Remove(chunkListId);

        LOG_INFO_IF(!IsRecovery(), "Chunk list removed (ChunkListId: %s)",
            ~chunkListId.ToString());
    }


    THolderId RegisterHolder(const TMsgRegisterHolder& message)
    {
        Stroka address = message.address();
        const auto& statistics = message.statistics();
    
        THolderId holderId = HolderIdGenerator.Next();
    
        const auto* existingHolder = FindHolder(address);
        if (existingHolder) {
            LOG_INFO_IF(!IsRecovery(), "Holder kicked out due to address conflict (Address: %s, HolderId: %d)",
                ~address,
                existingHolder->GetId());
            DoUnregisterHolder(*existingHolder);
        }

        LOG_INFO_IF(!IsRecovery(), "Holder registered (Address: %s, HolderId: %d, %s)",
            ~address,
            holderId,
            ~ToString(statistics));

        auto* newHolder = new THolder(
            holderId,
            address,
            EHolderState::Inactive,
            statistics);

        HolderMap.Insert(holderId, newHolder);
        HolderAddressMap.insert(MakePair(address, holderId)).Second();

        if (IsLeader()) {
            StartHolderTracking(*newHolder);
        }

        return holderId;
    }

    TVoid UnregisterHolder(const TMsgUnregisterHolder& message)
    { 
        auto holderId = message.holder_id();

        const auto& holder = GetHolder(holderId);

        DoUnregisterHolder(holder);

        return TVoid();
    }


    TVoid HeartbeatRequest(const TMsgHeartbeatRequest& message)
    {
        auto holderId = message.holder_id();
        const auto& statistics = message.statistics();

        auto& holder = GetHolderForUpdate(holderId);
        holder.Statistics() = statistics;

        if (IsLeader()) {
            HolderLeaseTracking->RenewHolderLease(holder);
            ChunkPlacement->OnHolderUpdated(holder);
        }

        FOREACH(const auto& chunkInfo, message.added_chunks()) {
            ProcessAddedChunk(holder, chunkInfo);
        }

        FOREACH(const auto& chunkInfo, message.removed_chunks()) {
            ProcessRemovedChunk(holder, chunkInfo);
        }

        bool isFirstHeartbeat = holder.GetState() == EHolderState::Inactive;
        if (isFirstHeartbeat) {
            holder.SetState(EHolderState::Active);
        }
        
        LOG_DEBUG_IF(!IsRecovery(), "Heartbeat request (Address: %s, HolderId: %d, IsFirst: %s, %s, ChunksAdded: %d, ChunksRemoved: %d)",
            ~holder.GetAddress(),
            holderId,
            ~::ToString(isFirstHeartbeat),
            ~ToString(statistics),
            static_cast<int>(message.added_chunks_size()),
            static_cast<int>(message.removed_chunks_size()));

        return TVoid();
    }

    TVoid HeartbeatResponse(const TMsgHeartbeatResponse& message)
    {
        auto holderId = message.holder_id();
        auto& holder = GetHolderForUpdate(holderId);

        FOREACH(const auto& startInfo, message.started_jobs()) {
            DoAddJob(holder, startInfo);
        }

        FOREACH(auto protoJobId, message.stopped_jobs()) {
            const auto& job = FindJob(TJobId::FromProto(protoJobId));
            if (job) {
                DoRemoveJob(holder, *job);
            }
        }

        LOG_DEBUG_IF(!IsRecovery(), "Heartbeat response (Address: %s, HolderId: %d, JobsStarted: %d, JobsStopped: %d)",
            ~holder.GetAddress(),
            holderId,
            static_cast<int>(message.started_jobs_size()),
            static_cast<int>(message.stopped_jobs_size()));

        return TVoid();
    }

    TFuture<TVoid>::TPtr Save(const TCompositeMetaState::TSaveContext& context)
    {
        auto* output = context.Output;
        auto invoker = context.Invoker;

        auto chunkIdGenerator = ChunkIdGenerator;
        auto chunkListIdGenerator = ChunkListIdGenerator;
        auto holderIdGenerator = HolderIdGenerator;
        invoker->Invoke(FromFunctor([=] ()
            {
                ::Save(output, chunkIdGenerator);
                ::Save(output, chunkListIdGenerator);
                ::Save(output, holderIdGenerator);
            }));
        
        ChunkMap.Save(invoker, output);
        ChunkListMap.Save(invoker, output);
        HolderMap.Save(invoker, output);
        JobMap.Save(invoker, output);
        return JobListMap.Save(invoker, output);
    }

    void Load(TInputStream* input)
    {
        ::Load(input, ChunkIdGenerator);
        ::Load(input, ChunkListIdGenerator);
        ::Load(input, HolderIdGenerator);
        
        ChunkMap.Load(input);
        ChunkListMap.Load(input);
        HolderMap.Load(input);
        JobMap.Load(input);
        JobListMap.Load(input);

        // Reconstruct HolderAddressMap.
        HolderAddressMap.clear();
        FOREACH(const auto& pair, HolderMap) {
            const auto* holder = pair.Second();
            YVERIFY(HolderAddressMap.insert(MakePair(holder->GetAddress(), holder->GetId())).Second());
        }

        // Reconstruct ReplicationSinkMap.
        ReplicationSinkMap.clear();
        FOREACH (const auto& pair, JobMap) {
            RegisterReplicationSinks(*pair.Second());
        }
    }

    virtual void Clear()
    {
        ChunkIdGenerator.Reset();
        ChunkListIdGenerator.Reset();
        HolderIdGenerator.Reset();
        ChunkMap.Clear();
        ChunkListMap.Clear();
        HolderMap.Clear();
        JobMap.Clear();
        JobListMap.Clear();

        HolderAddressMap.clear();
        ReplicationSinkMap.clear();
    }


    virtual void OnLeaderRecoveryComplete()
    {
        ChunkPlacement = New<TChunkPlacement>(~ChunkManager);

        ChunkReplication = New<TChunkReplication>(
            ~ChunkManager,
            ~ChunkPlacement,
            ~MetaStateManager->GetEpochStateInvoker());

        HolderLeaseTracking = New<THolderLeaseTracker>(
            ~Config,
            ~ChunkManager,
            ~MetaStateManager->GetEpochStateInvoker());

        FOREACH(const auto& pair, HolderMap) {
            StartHolderTracking(*pair.Second());
        }
    }

    virtual void OnStopLeading()
    {
        ChunkPlacement.Reset();
        ChunkReplication.Reset();
        HolderLeaseTracking.Reset();
    }


    virtual void OnTransactionCommitted(const TTransaction& transaction)
    {
        UnrefUnboundChunkTrees(transaction);
    }

    virtual void OnTransactionAborted(const TTransaction& transaction)
    {
        UnrefUnboundChunkTrees(transaction);
    }

    void UnrefUnboundChunkTrees(const TTransaction& transaction)
    {
        FOREACH(const auto& treeId, transaction.UnboundChunkTreeIds()) {
            UnrefChunkTree(treeId);
        }
    }


    void StartHolderTracking(const THolder& holder)
    {
        HolderLeaseTracking->OnHolderRegistered(holder);
        ChunkPlacement->OnHolderRegistered(holder);
        ChunkReplication->OnHolderRegistered(holder);

        HolderRegistered_.Fire(holder); 
    }

    void StopHolderTracking(const THolder& holder)
    {
        HolderLeaseTracking->OnHolderUnregistered(holder);
        ChunkPlacement->OnHolderUnregistered(holder);
        ChunkReplication->OnHolderUnregistered(holder);

        HolderUnregistered_.Fire(holder);
    }


    void DoUnregisterHolder(const THolder& holder)
    { 
        auto holderId = holder.GetId();

        LOG_INFO_IF(!IsRecovery(), "Holder unregistered (Address: %s, HolderId: %d)",
            ~holder.GetAddress(),
            holderId);

        if (IsLeader()) {
            StopHolderTracking(holder);
        }

        FOREACH(const auto& chunkId, holder.StoredChunkIds()) {
            auto& chunk = GetChunkForUpdate(chunkId);
            DoRemoveChunkReplicaAtDeadHolder(holder, chunk, false);
        }

        FOREACH(const auto& chunkId, holder.CachedChunkIds()) {
            auto& chunk = GetChunkForUpdate(chunkId);
            DoRemoveChunkReplicaAtDeadHolder(holder, chunk, true);
        }

        FOREACH(const auto& jobId, holder.JobIds()) {
            const auto& job = GetJob(jobId);
            DoRemoveJobAtDeadHolder(holder, job);
        }

        YVERIFY(HolderAddressMap.erase(holder.GetAddress()) == 1);
        HolderMap.Remove(holderId);
    }


    void DoAddChunkReplica(THolder& holder, TChunk& chunk, bool cached)
    {
        auto chunkId = chunk.GetId();
        auto holderId = holder.GetId();

        if (holder.HasChunk(chunkId, cached)) {
            LOG_WARNING_IF(!IsRecovery(), "Chunk replica is already added (ChunkId: %s, Cached: %s, Address: %s, HolderId: %d)",
                ~chunkId.ToString(),
                ~::ToString(cached),
                ~holder.GetAddress(),
                holderId);
            return;
        }

        holder.AddChunk(chunkId, cached);
        chunk.AddLocation(holderId, cached);

        LOG_INFO_IF(!IsRecovery(), "Chunk replica added (ChunkId: %s, Cached: %s, Address: %s, HolderId: %d)",
            ~chunkId.ToString(),
            ~::ToString(cached),
            ~holder.GetAddress(),
            holderId);

        if (!cached && IsLeader()) {
            ChunkReplication->OnReplicaAdded(holder, chunk);
        }
    }

    void DoRemoveChunkReplica(THolder& holder, TChunk& chunk, bool cached)
    {
        auto chunkId = chunk.GetId();
        auto holderId = holder.GetId();

        if (!holder.HasChunk(chunkId, cached)) {
            LOG_WARNING_IF(!IsRecovery(), "Chunk replica is already removed (ChunkId: %s, Cached: %s, Address: %s, HolderId: %d)",
                ~chunkId.ToString(),
                ~::ToString(cached),
                ~holder.GetAddress(),
                holderId);
            return;
        }

        holder.RemoveChunk(chunk.GetId(), cached);
        chunk.RemoveLocation(holder.GetId(), cached);

        LOG_INFO_IF(!IsRecovery(), "Chunk replica removed (ChunkId: %s, Cached: %s, Address: %s, HolderId: %d)",
             ~chunkId.ToString(),
             ~::ToString(cached),
             ~holder.GetAddress(),
             holderId);

        if (!cached && IsLeader()) {
            ChunkReplication->OnReplicaRemoved(holder, chunk);
        }
    }

    void DoRemoveChunkReplicaAtDeadHolder(const THolder& holder, TChunk& chunk, bool cached)
    {
        chunk.RemoveLocation(holder.GetId(), cached);

        LOG_INFO_IF(!IsRecovery(), "Chunk replica removed since holder is dead (ChunkId: %s, Cached: %s, Address: %s, HolderId: %d)",
             ~chunk.GetId().ToString(),
             ~::ToString(cached),
             ~holder.GetAddress(),
             holder.GetId());

        if (!cached && IsLeader()) {
            ChunkReplication->OnReplicaRemoved(holder, chunk);
        }
    }


    void DoAddJob(THolder& holder, const TRspHolderHeartbeat::TJobStartInfo& jobInfo)
    {
        auto chunkId = TChunkId::FromProto(jobInfo.chunk_id());
        auto jobId = TJobId::FromProto(jobInfo.job_id());
        auto targetAddresses = FromProto<Stroka>(jobInfo.target_addresses());
        auto jobType = EJobType(jobInfo.type());

        auto* job = new TJob(
            jobType,
            jobId,
            chunkId,
            holder.GetAddress(),
            targetAddresses,
            TInstant::Now());
        JobMap.Insert(jobId, job);

        auto& list = GetOrCreateJobListForUpdate(chunkId);
        list.AddJob(jobId);

        holder.AddJob(jobId);

        RegisterReplicationSinks(*job);

        LOG_INFO_IF(!IsRecovery(), "Job added (JobId: %s, Address: %s, HolderId: %d, JobType: %s, ChunkId: %s)",
            ~jobId.ToString(),
            ~holder.GetAddress(),
            holder.GetId(),
            ~jobType.ToString(),
            ~chunkId.ToString());
    }

    void DoRemoveJob(THolder& holder, const TJob& job)
    {
        auto jobId = job.GetJobId();

        auto& list = GetJobListForUpdate(job.GetChunkId());
        list.RemoveJob(jobId);
        MaybeDropJobList(list);

        holder.RemoveJob(jobId);

        UnregisterReplicationSinks(job);

        JobMap.Remove(job.GetJobId());

        LOG_INFO_IF(!IsRecovery(), "Job removed (JobId: %s, Address: %s, HolderId: %d)",
            ~jobId.ToString(),
            ~holder.GetAddress(),
            holder.GetId());
    }

    void DoRemoveJobAtDeadHolder(const THolder& holder, const TJob& job)
    {
        auto jobId = job.GetJobId();

        auto& list = GetJobListForUpdate(job.GetChunkId());
        list.RemoveJob(jobId);
        MaybeDropJobList(list);

        UnregisterReplicationSinks(job);

        JobMap.Remove(job.GetJobId());

        LOG_INFO_IF(!IsRecovery(), "Job removed since holder is dead (JobId: %s, Address: %s, HolderId: %d)",
            ~jobId.ToString(),
            ~holder.GetAddress(),
            holder.GetId());
    }


    void ProcessAddedChunk(
        THolder& holder,
        const TReqHolderHeartbeat::TChunkAddInfo& chunkInfo)
    {
        auto holderId = holder.GetId();
        auto chunkId = TChunkId::FromProto(chunkInfo.chunk_id());
        i64 size = chunkInfo.size();
        bool cached = chunkInfo.cached();

        auto* chunk = FindChunkForUpdate(chunkId);
        if (!chunk) {
            // Holders may still contain cached replicas of chunks that no longer exist.
            // Here we just silently ignore this case.
            if (cached) {
                return;
            }

            LOG_INFO_IF(!IsRecovery(), "Unknown chunk added at holder, removal scheduled (Address: %s, HolderId: %d, ChunkId: %s, Cached: %s, Size: %" PRId64 ")",
                ~holder.GetAddress(),
                holderId,
                ~chunkId.ToString(),
                ~::ToString(cached),
                size);
            if (IsLeader()) {
                ChunkReplication->ScheduleChunkRemoval(holder, chunkId);
            }
            return;
        }

        // Use the size reported by the holder, but check it for consistency first.
        if (chunk->GetSize() != TChunk::UnknownSize && chunk->GetSize() != size) {
            LOG_FATAL("Chunk size mismatch (ChunkId: %s, Cached: %s, KnownSize: %" PRId64 ", NewSize: %" PRId64 ", Address: %s, HolderId: %d",
                ~chunkId.ToString(),
                ~::ToString(cached),
                chunk->GetSize(),
                size,
                ~holder.GetAddress(),
                holder.GetId());
        }
        chunk->SetSize(size);

        DoAddChunkReplica(holder, *chunk, cached);
    }

    void ProcessRemovedChunk(
        THolder& holder,
        const TReqHolderHeartbeat::TChunkRemoveInfo& chunkInfo)
    {
        auto holderId = holder.GetId();
        auto chunkId = TChunkId::FromProto(chunkInfo.chunk_id());
        bool cached = chunkInfo.cached();

        auto* chunk = FindChunkForUpdate(chunkId);
        if (!chunk) {
            LOG_INFO_IF(!IsRecovery(), "Unknown chunk replica removed (ChunkId: %s, Cached: %s, Address: %s, HolderId: %d)",
                 ~chunkId.ToString(),
                 ~::ToString(cached),
                 ~holder.GetAddress(),
                 holderId);
            return;
        }

        DoRemoveChunkReplica(holder, *chunk, cached);
    }


    TJobList& GetOrCreateJobListForUpdate(const TChunkId& id)
    {
        auto* list = FindJobListForUpdate(id);
        if (list)
            return *list;

        JobListMap.Insert(id, new TJobList(id));
        return GetJobListForUpdate(id);
    }

    void MaybeDropJobList(const TJobList& list)
    {
        if (list.JobIds().empty()) {
            JobListMap.Remove(list.GetChunkId());
        }
    }


    void RegisterReplicationSinks(const TJob& job)
    {
        switch (job.GetType()) {
            case EJobType::Replicate: {
                FOREACH (const auto& address, job.TargetAddresses()) {
                    auto& sink = GetOrCreateReplicationSink(address);
                    YASSERT(sink.JobIds.insert(job.GetJobId()).Second());
                }
                break;
            }

            case EJobType::Remove:
                break;

            default:
                YUNREACHABLE();
        }
    }

    void UnregisterReplicationSinks(const TJob& job)
    {
        switch (job.GetType()) {
            case EJobType::Replicate: {
                FOREACH (const auto& address, job.TargetAddresses()) {
                    auto& sink = GetOrCreateReplicationSink(address);
                    YASSERT(sink.JobIds.erase(job.GetJobId()) == 1);
                    DropReplicationSinkIfEmpty(sink);
                }
                break;
            }

            case EJobType::Remove:
                break;

            default:
                YUNREACHABLE();
        }
    }

    TReplicationSink& GetOrCreateReplicationSink(const Stroka& address)
    {
        auto it = ReplicationSinkMap.find(address);
        if (it != ReplicationSinkMap.end())
            return it->Second();

        auto pair = ReplicationSinkMap.insert(MakePair(address, TReplicationSink(address)));
        YASSERT(pair.second);
        return pair.first->Second();
    }

    void DropReplicationSinkIfEmpty(const TReplicationSink& sink)
    {
        if (sink.JobIds.empty()) {
            // NB: Do not try to inline this variable! erase() will destroy the object
            // and will access the key afterwards.
            Stroka address = sink.Address;
            YVERIFY(ReplicationSinkMap.erase(address) == 1);
        }
    }

};

DEFINE_METAMAP_ACCESSORS(TChunkManager::TImpl, Chunk, TChunk, TChunkId, ChunkMap)
DEFINE_METAMAP_ACCESSORS(TChunkManager::TImpl, ChunkList, TChunkList, TChunkListId, ChunkListMap)
DEFINE_METAMAP_ACCESSORS(TChunkManager::TImpl, Holder, THolder, THolderId, HolderMap)
DEFINE_METAMAP_ACCESSORS(TChunkManager::TImpl, JobList, TJobList, TChunkId, JobListMap)
DEFINE_METAMAP_ACCESSORS(TChunkManager::TImpl, Job, TJob, TJobId, JobMap)

DELEGATE_BYREF_RO_PROPERTY(TChunkManager::TImpl, yhash_set<TChunkId>, LostChunkIds, *ChunkReplication);
DELEGATE_BYREF_RO_PROPERTY(TChunkManager::TImpl, yhash_set<TChunkId>, OverreplicatedChunkIds, *ChunkReplication);
DELEGATE_BYREF_RO_PROPERTY(TChunkManager::TImpl, yhash_set<TChunkId>, UnderreplicatedChunkIds, *ChunkReplication);

////////////////////////////////////////////////////////////////////////////////

TChunkManager::TChunkManager(
    TConfig* config,
    NMetaState::IMetaStateManager* metaStateManager,
    NMetaState::TCompositeMetaState* metaState,
    TTransactionManager* transactionManager,
    IHolderRegistry* holderRegistry)
    : Config(config)
    , Impl(New<TImpl>(
        config,
        this,
        metaStateManager,
        metaState,
        transactionManager,
        holderRegistry))
{
    metaState->RegisterPart(Impl);
}

const THolder* TChunkManager::FindHolder(const Stroka& address)
{
    return Impl->FindHolder(address);
}

const TReplicationSink* TChunkManager::FindReplicationSink(const Stroka& address)
{
    return Impl->FindReplicationSink(address);
}

yvector<THolderId> TChunkManager::AllocateUploadTargets(int replicaCount)
{
    return Impl->AllocateUploadTargets(replicaCount);
}

TMetaChange<THolderId>::TPtr TChunkManager::InitiateRegisterHolder(
    const TMsgRegisterHolder& message)
{
    return Impl->InitiateRegisterHolder(message);
}

TMetaChange<TVoid>::TPtr TChunkManager::InitiateUnregisterHolder(
    const TMsgUnregisterHolder& message)
{
    return Impl->InitiateUnregisterHolder(message);
}

TMetaChange<TVoid>::TPtr TChunkManager::InitiateHeartbeatRequest(
    const TMsgHeartbeatRequest& message)
{
    return Impl->InitiateHeartbeatRequest(message);
}

TMetaChange<TVoid>::TPtr TChunkManager::InitiateHeartbeatResponse(
    const TMsgHeartbeatResponse& message)
{
    return Impl->InitiateHeartbeatResponse(message);
}

TMetaChange< yvector<TChunkId> >::TPtr TChunkManager::InitiateCreateChunks(
    const TMsgCreateChunks& message)
{
    return Impl->InitiateAllocateChunk(message);
}

NMetaState::TMetaChange<TVoid>::TPtr TChunkManager::InitiateConfirmChunks(
    const NProto::TMsgConfirmChunks& message)
{
    return Impl->InitiateConfirmChunks(message);
}

TMetaChange< yvector<TChunkListId> >::TPtr TChunkManager::InitiateCreateChunkLists(
    const TMsgCreateChunkLists& message)
{
    return Impl->InitiateCreateChunkLists(message);
}

TMetaChange<TVoid>::TPtr TChunkManager::InitiateAttachChunkTrees(
    const TMsgAttachChunkTrees& message)
{
    return Impl->InitiateAttachChunkTrees(message);
}

TMetaChange<TVoid>::TPtr TChunkManager::InitiateDetachChunkTrees(
    const TMsgDetachChunkTrees& message)
{
    return Impl->InitiateDetachChunkTrees(message);
}

TChunkList& TChunkManager::CreateChunkList()
{
    return Impl->CreateChunkList();
}

void TChunkManager::RefChunkTree(const TChunkTreeId& treeId)
{
    Impl->RefChunkTree(treeId);
}

void TChunkManager::UnrefChunkTree(const TChunkTreeId& treeId)
{
    Impl->UnrefChunkTree(treeId);
}

void TChunkManager::RefChunkTree(TChunk& chunk)
{
    Impl->RefChunkTree(chunk);
}

void TChunkManager::UnrefChunkTree(TChunk& chunk)
{
    Impl->UnrefChunkTree(chunk);
}

void TChunkManager::RefChunkTree(TChunkList& chunkList)
{
    Impl->RefChunkTree(chunkList);
}

void TChunkManager::UnrefChunkTree(TChunkList& chunkList)
{
    Impl->UnrefChunkTree(chunkList);
}

void TChunkManager::RunJobControl(
    const THolder& holder,
    const yvector<TJobInfo>& runningJobs,
    yvector<TJobStartInfo>* jobsToStart,
    yvector<TJobId>* jobsToStop)
{
    Impl->RunJobControl(
        holder,
        runningJobs,
        jobsToStart,
        jobsToStop);
}

void TChunkManager::FillHolderAddresses(
    ::google::protobuf::RepeatedPtrField< TProtoStringType>* addresses,
    const TChunk& chunk)
{
    FOREACH(auto holderId, chunk.StoredLocations()) {
        const THolder& holder = GetHolder(holderId);
        addresses->Add()->assign(holder.GetAddress());
    }

    if (~chunk.CachedLocations()) {
        FOREACH(auto holderId, *chunk.CachedLocations()) {
            const THolder& holder = GetHolder(holderId);
            addresses->Add()->assign(holder.GetAddress());
        }
    }
}

DELEGATE_METAMAP_ACCESSORS(TChunkManager, Chunk, TChunk, TChunkId, *Impl)
DELEGATE_METAMAP_ACCESSORS(TChunkManager, ChunkList, TChunkList, TChunkListId, *Impl)
DELEGATE_METAMAP_ACCESSORS(TChunkManager, Holder, THolder, THolderId, *Impl)
DELEGATE_METAMAP_ACCESSORS(TChunkManager, JobList, TJobList, TChunkId, *Impl)
DELEGATE_METAMAP_ACCESSORS(TChunkManager, Job, TJob, TJobId, *Impl)

DELEGATE_BYREF_RW_PROPERTY(TChunkManager, TParamSignal<const THolder&>, HolderRegistered, *Impl);
DELEGATE_BYREF_RW_PROPERTY(TChunkManager, TParamSignal<const THolder&>, HolderUnregistered, *Impl);

DELEGATE_BYREF_RO_PROPERTY(TChunkManager, yhash_set<TChunkId>, LostChunkIds, *Impl);
DELEGATE_BYREF_RO_PROPERTY(TChunkManager, yhash_set<TChunkId>, OverreplicatedChunkIds, *Impl);
DELEGATE_BYREF_RO_PROPERTY(TChunkManager, yhash_set<TChunkId>, UnderreplicatedChunkIds, *Impl);

///////////////////////////////////////////////////////////////////////////////

} // namespace NChunkServer
} // namespace NYT
