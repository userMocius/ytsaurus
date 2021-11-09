// Code generated by yt-gen-error-code. DO NOT EDIT.
package yterrors

import "fmt"

const (
	CodeOK                                     ErrorCode = 0
	CodeGeneric                                ErrorCode = 1
	CodeCanceled                               ErrorCode = 2
	CodeTimeout                                ErrorCode = 3
	CodeFutureCombinerFailure                  ErrorCode = 4
	CodeFutureCombinerShortcut                 ErrorCode = 5
	CodeTransportError                         ErrorCode = 100
	CodeProtocolError                          ErrorCode = 101
	CodeNoSuchService                          ErrorCode = 102
	CodeNoSuchMethod                           ErrorCode = 103
	CodeUnavailable                            ErrorCode = 105
	CodePoisonPill                             ErrorCode = 106
	CodeRPCRequestQueueSizeLimitExceeded       ErrorCode = 108
	CodeRPCAuthenticationError                 ErrorCode = 109
	CodeInvalidCsrfToken                       ErrorCode = 110
	CodeInvalidCredentials                     ErrorCode = 111
	CodeStreamingNotSupported                  ErrorCode = 112
	CodeUnsupportedClientFeature               ErrorCode = 113
	CodeUnsupportedServerFeature               ErrorCode = 114
	CodeNoSuchOperation                        ErrorCode = 200
	CodeInvalidOperationState                  ErrorCode = 201
	CodeTooManyOperations                      ErrorCode = 202
	CodeNoSuchJob                              ErrorCode = 203
	CodeOperationFailedOnJobRestart            ErrorCode = 210
	CodeOperationFailedWithInconsistentLocking ErrorCode = 211
	CodeOperationControllerCrashed             ErrorCode = 212
	CodeTestingError                           ErrorCode = 213
	CodePoolTreesAreUnspecified                ErrorCode = 214
	CodeMaxFailedJobsLimitExceeded             ErrorCode = 215
	CodeOperationFailedToPrepare               ErrorCode = 216
	CodeWatcherHandlerFailed                   ErrorCode = 217
	CodeMasterDisconnected                     ErrorCode = 218
	CodeSortOrderViolation                     ErrorCode = 301
	CodeInvalidDoubleValue                     ErrorCode = 302
	CodeIncomparableType                       ErrorCode = 303
	CodeUnhashableType                         ErrorCode = 304
	CodeCorruptedNameTable                     ErrorCode = 305
	CodeUniqueKeyViolation                     ErrorCode = 306
	CodeSchemaViolation                        ErrorCode = 307
	CodeRowWeightLimitExceeded                 ErrorCode = 308
	CodeInvalidColumnFilter                    ErrorCode = 309
	CodeInvalidColumnRenaming                  ErrorCode = 310
	CodeIncompatibleKeyColumns                 ErrorCode = 311
	CodeReaderDeadlineExpired                  ErrorCode = 312
	CodeTimestampOutOfRange                    ErrorCode = 313
	CodeInvalidSchemaValue                     ErrorCode = 314
	CodeFormatCannotRepresentRow               ErrorCode = 315
	CodeIncompatibleSchemas                    ErrorCode = 316
	CodeSameTransactionLockConflict            ErrorCode = 400
	CodeDescendantTransactionLockConflict      ErrorCode = 401
	CodeConcurrentTransactionLockConflict      ErrorCode = 402
	CodePendingLockConflict                    ErrorCode = 403
	CodeLockDestroyed                          ErrorCode = 404
	CodeResolveError                           ErrorCode = 500
	CodeAlreadyExists                          ErrorCode = 501
	CodeMaxChildCountViolation                 ErrorCode = 502
	CodeMaxStringLengthViolation               ErrorCode = 503
	CodeMaxAttributeSizeViolation              ErrorCode = 504
	CodeMaxKeyLengthViolation                  ErrorCode = 505
	CodeNoSuchSnapshot                         ErrorCode = 600
	CodeNoSuchChangelog                        ErrorCode = 601
	CodeInvalidEpoch                           ErrorCode = 602
	CodeInvalidVersion                         ErrorCode = 603
	CodeOutOfOrderMutations                    ErrorCode = 609
	CodeInvalidSnapshotVersion                 ErrorCode = 610
	CodeReadOnlySnapshotBuilt                  ErrorCode = 611
	CodeReadOnlySnapshotBuildFailed            ErrorCode = 612
	CodeBrokenChangelog                        ErrorCode = 613
	CodeChangelogIOError                       ErrorCode = 614
	CodeInvalidChangelogState                  ErrorCode = 615
	CodeAllTargetNodesFailed                   ErrorCode = 700
	CodeSendBlocksFailed                       ErrorCode = 701
	CodeNoSuchSession                          ErrorCode = 702
	CodeSessionAlreadyExists                   ErrorCode = 703
	CodeChunkAlreadyExists                     ErrorCode = 704
	CodeWindowError                            ErrorCode = 705
	CodeBlockContentMismatch                   ErrorCode = 706
	CodeNoSuchBlock                            ErrorCode = 707
	CodeNoSuchChunk                            ErrorCode = 708
	CodeNoLocationAvailable                    ErrorCode = 710
	CodeIOError                                ErrorCode = 711
	CodeMasterCommunicationFailed              ErrorCode = 712
	CodeNoSuchChunkTree                        ErrorCode = 713
	CodeMasterNotConnected                     ErrorCode = 714
	CodeChunkUnavailable                       ErrorCode = 716
	CodeNoSuchChunkList                        ErrorCode = 717
	CodeWriteThrottlingActive                  ErrorCode = 718
	CodeNoSuchMedium                           ErrorCode = 719
	CodeOptimisticLockFailure                  ErrorCode = 720
	CodeInvalidBlockChecksum                   ErrorCode = 721
	CodeBlockOutOfRange                        ErrorCode = 722
	CodeMissingExtension                       ErrorCode = 724
	CodeBandwidthThrottlingFailed              ErrorCode = 725
	CodeReaderTimeout                          ErrorCode = 726
	CodeNoSuchChunkView                        ErrorCode = 727
	CodeIncorrectChunkFileChecksum             ErrorCode = 728
	CodeIncorrectChunkFileHeaderSignature      ErrorCode = 729
	CodeIncorrectLayerFileSize                 ErrorCode = 730
	CodeNoSpaceLeftOnDevice                    ErrorCode = 731
	CodeConcurrentChunkUpdate                  ErrorCode = 732
	CodeInvalidElectionState                   ErrorCode = 800
	CodeInvalidLeader                          ErrorCode = 801
	CodeInvalidElectionEpoch                   ErrorCode = 802
	CodeAuthenticationError                    ErrorCode = 900
	CodeAuthorizationError                     ErrorCode = 901
	CodeAccountLimitExceeded                   ErrorCode = 902
	CodeUserBanned                             ErrorCode = 903
	CodeRequestQueueSizeLimitExceeded          ErrorCode = 904
	CodeNoSuchAccount                          ErrorCode = 905
	CodeSafeModeEnabled                        ErrorCode = 906
	CodeNoSuchSubject                          ErrorCode = 907
	CodePrerequisiteCheckFailed                ErrorCode = 1000
	CodeInvalidObjectLifeStage                 ErrorCode = 1001
	CodeCrossCellAdditionalPath                ErrorCode = 1002
	CodeCrossCellRevisionPrerequisitePath      ErrorCode = 1003
	CodeForwardedRequestFailed                 ErrorCode = 1004
	CodeCannotCacheMutatingRequest             ErrorCode = 1005
	CodeConfigCreationFailed                   ErrorCode = 1100
	CodeAbortByScheduler                       ErrorCode = 1101
	CodeResourceOverdraft                      ErrorCode = 1102
	CodeWaitingJobTimeout                      ErrorCode = 1103
	CodeSlotNotFound                           ErrorCode = 1104
	CodeJobEnvironmentDisabled                 ErrorCode = 1105
	CodeJobProxyConnectionFailed               ErrorCode = 1106
	CodeArtifactCopyingFailed                  ErrorCode = 1107
	CodeNodeDirectoryPreparationFailed         ErrorCode = 1108
	CodeSlotLocationDisabled                   ErrorCode = 1109
	CodeQuotaSettingFailed                     ErrorCode = 1110
	CodeRootVolumePreparationFailed            ErrorCode = 1111
	CodeNotEnoughDiskSpace                     ErrorCode = 1112
	CodeArtifactDownloadFailed                 ErrorCode = 1113
	CodeJobProxyPreparationTimeout             ErrorCode = 1114
	CodeJobPreparationTimeout                  ErrorCode = 1115
	CodeJobProxyFailed                         ErrorCode = 1120
	CodeSetupCommandFailed                     ErrorCode = 1121
	CodeGpuLayerNotFetched                     ErrorCode = 1122
	CodeGpuJobWithoutLayers                    ErrorCode = 1123
	CodeTmpfsOverflow                          ErrorCode = 1124
	CodeGpuCheckCommandFailed                  ErrorCode = 1125
	CodeGpuCheckCommandIncorrect               ErrorCode = 1126
	CodeMemoryLimitExceeded                    ErrorCode = 1200
	CodeMemoryCheckFailed                      ErrorCode = 1201
	CodeJobTimeLimitExceeded                   ErrorCode = 1202
	CodeUnsupportedJobType                     ErrorCode = 1203
	CodeJobNotPrepared                         ErrorCode = 1204
	CodeUserJobFailed                          ErrorCode = 1205
	CodeUserJobProducedCoreFiles               ErrorCode = 1206
	CodeLocalChunkReaderFailed                 ErrorCode = 1300
	CodeLayerUnpackingFailed                   ErrorCode = 1301
	CodeNodeDecommissioned                     ErrorCode = 1401
	CodeNodeBanned                             ErrorCode = 1402
	CodeNodeTabletSlotsDisabled                ErrorCode = 1403
	CodeNodeFilterMismatch                     ErrorCode = 1404
	CodeCellDidNotAppearWithinTimeout          ErrorCode = 1405
	CodeAborted                                ErrorCode = 1500
	CodeResolveTimedOut                        ErrorCode = 1501
	CodeNoSuchNode                             ErrorCode = 1600
	CodeInvalidState                           ErrorCode = 1601
	CodeNoSuchNetwork                          ErrorCode = 1602
	CodeNoSuchRack                             ErrorCode = 1603
	CodeNoSuchDataCenter                       ErrorCode = 1604
	CodeTransactionLockConflict                ErrorCode = 1700
	CodeNoSuchTablet                           ErrorCode = 1701
	CodeTabletNotMounted                       ErrorCode = 1702
	CodeAllWritesDisabled                      ErrorCode = 1703
	CodeInvalidMountRevision                   ErrorCode = 1704
	CodeTableReplicaAlreadyExists              ErrorCode = 1705
	CodeInvalidTabletState                     ErrorCode = 1706
	CodeTableMountInfoNotReady                 ErrorCode = 1707
	CodeTabletSnapshotExpired                  ErrorCode = 1708
	CodeQueryInputRowCountLimitExceeded        ErrorCode = 1709
	CodeQueryOutputRowCountLimitExceeded       ErrorCode = 1710
	CodeQueryExpressionDepthLimitExceeded      ErrorCode = 1711
	CodeRowIsBlocked                           ErrorCode = 1712
	CodeBlockedRowWaitTimeout                  ErrorCode = 1713
	CodeNoSyncReplicas                         ErrorCode = 1714
	CodeTableMustNotBeReplicated               ErrorCode = 1715
	CodeTableMustBeSorted                      ErrorCode = 1716
	CodeTooManyRowsInTransaction               ErrorCode = 1717
	CodeUpstreamReplicaMismatch                ErrorCode = 1718
	CodeNoSuchDynamicStore                     ErrorCode = 1719
	CodeShellExited                            ErrorCode = 1800
	CodeShellManagerShutDown                   ErrorCode = 1801
	CodeTooManyConcurrentRequests              ErrorCode = 1900
	CodeJobArchiveUnavailable                  ErrorCode = 1910
	CodeRetriableArchiveError                  ErrorCode = 1911
	CodeAPINoSuchOperation                     ErrorCode = 1915
	CodeAPINoSuchJob                           ErrorCode = 1916
	CodeNoSuchAttribute                        ErrorCode = 1920
	CodeDataSliceLimitExceeded                 ErrorCode = 2000
	CodeMaxDataWeightPerJobExceeded            ErrorCode = 2001
	CodeMaxPrimaryDataWeightPerJobExceeded     ErrorCode = 2002
	CodeProxyBanned                            ErrorCode = 2100
	CodeSubqueryDataWeightLimitExceeded        ErrorCode = 2200
	CodeParticipantUnregistered                ErrorCode = 2201
	CodeNoSuchGroup                            ErrorCode = 2300
	CodeNoSuchMember                           ErrorCode = 2301
	CodeNoSuchThrottler                        ErrorCode = 2400
	CodeUnexpectedThrottlerMode                ErrorCode = 2401
	CodeUnrecognizedConfigOption               ErrorCode = 2500
	CodeFailedToFetchDynamicConfig             ErrorCode = 2501
	CodeDuplicateMatchingDynamicConfigs        ErrorCode = 2502
	CodeUnrecognizedDynamicConfigOption        ErrorCode = 2503
	CodeFailedToApplyDynamicConfig             ErrorCode = 2504
	CodeInvalidDynamicConfig                   ErrorCode = 2505
	CodeAgentCallFailed                        ErrorCode = 4400
	CodeNoOnlineNodeToScheduleJob              ErrorCode = 4410
	CodeMaterializationFailed                  ErrorCode = 4415
	CodeNoSuchTransaction                      ErrorCode = 11000
	CodeNestedExternalTransactionExists        ErrorCode = 11001
	CodeTransactionDepthLimitReached           ErrorCode = 11002
	CodeInvalidTransactionState                ErrorCode = 11003
	CodeParticipantFailedToPrepare             ErrorCode = 11004
	CodeSomeParticipantsAreDown                ErrorCode = 11005
	CodeAlienTransactionsForbidden             ErrorCode = 11006
	CodeMalformedAlienTransaction              ErrorCode = 11007
	CodeInvalidTransactionAtomicity            ErrorCode = 11008
	CodeUploadTransactionCannotHaveNested      ErrorCode = 11009
	CodeForeignParentTransaction               ErrorCode = 11010
	CodeForeignPrerequisiteTransaction         ErrorCode = 11011
	CodeFailedToStartContainer                 ErrorCode = 14000
	CodeJobIsNotRunning                        ErrorCode = 17000
)

func (e ErrorCode) String() string {
	switch e {
	case CodeOK:
		return "OK"
	case CodeGeneric:
		return "Generic"
	case CodeCanceled:
		return "Canceled"
	case CodeTimeout:
		return "Timeout"
	case CodeFutureCombinerFailure:
		return "FutureCombinerFailure"
	case CodeFutureCombinerShortcut:
		return "FutureCombinerShortcut"
	case CodeTransportError:
		return "TransportError"
	case CodeProtocolError:
		return "ProtocolError"
	case CodeNoSuchService:
		return "NoSuchService"
	case CodeNoSuchMethod:
		return "NoSuchMethod"
	case CodeUnavailable:
		return "Unavailable"
	case CodePoisonPill:
		return "PoisonPill"
	case CodeRPCRequestQueueSizeLimitExceeded:
		return "RPCRequestQueueSizeLimitExceeded"
	case CodeRPCAuthenticationError:
		return "RPCAuthenticationError"
	case CodeInvalidCsrfToken:
		return "InvalidCsrfToken"
	case CodeInvalidCredentials:
		return "InvalidCredentials"
	case CodeStreamingNotSupported:
		return "StreamingNotSupported"
	case CodeUnsupportedClientFeature:
		return "UnsupportedClientFeature"
	case CodeUnsupportedServerFeature:
		return "UnsupportedServerFeature"
	case CodeNoSuchOperation:
		return "NoSuchOperation"
	case CodeInvalidOperationState:
		return "InvalidOperationState"
	case CodeTooManyOperations:
		return "TooManyOperations"
	case CodeNoSuchJob:
		return "NoSuchJob"
	case CodeOperationFailedOnJobRestart:
		return "OperationFailedOnJobRestart"
	case CodeOperationFailedWithInconsistentLocking:
		return "OperationFailedWithInconsistentLocking"
	case CodeOperationControllerCrashed:
		return "OperationControllerCrashed"
	case CodeTestingError:
		return "TestingError"
	case CodePoolTreesAreUnspecified:
		return "PoolTreesAreUnspecified"
	case CodeMaxFailedJobsLimitExceeded:
		return "MaxFailedJobsLimitExceeded"
	case CodeOperationFailedToPrepare:
		return "OperationFailedToPrepare"
	case CodeWatcherHandlerFailed:
		return "WatcherHandlerFailed"
	case CodeMasterDisconnected:
		return "MasterDisconnected"
	case CodeSortOrderViolation:
		return "SortOrderViolation"
	case CodeInvalidDoubleValue:
		return "InvalidDoubleValue"
	case CodeIncomparableType:
		return "IncomparableType"
	case CodeUnhashableType:
		return "UnhashableType"
	case CodeCorruptedNameTable:
		return "CorruptedNameTable"
	case CodeUniqueKeyViolation:
		return "UniqueKeyViolation"
	case CodeSchemaViolation:
		return "SchemaViolation"
	case CodeRowWeightLimitExceeded:
		return "RowWeightLimitExceeded"
	case CodeInvalidColumnFilter:
		return "InvalidColumnFilter"
	case CodeInvalidColumnRenaming:
		return "InvalidColumnRenaming"
	case CodeIncompatibleKeyColumns:
		return "IncompatibleKeyColumns"
	case CodeReaderDeadlineExpired:
		return "ReaderDeadlineExpired"
	case CodeTimestampOutOfRange:
		return "TimestampOutOfRange"
	case CodeInvalidSchemaValue:
		return "InvalidSchemaValue"
	case CodeFormatCannotRepresentRow:
		return "FormatCannotRepresentRow"
	case CodeIncompatibleSchemas:
		return "IncompatibleSchemas"
	case CodeSameTransactionLockConflict:
		return "SameTransactionLockConflict"
	case CodeDescendantTransactionLockConflict:
		return "DescendantTransactionLockConflict"
	case CodeConcurrentTransactionLockConflict:
		return "ConcurrentTransactionLockConflict"
	case CodePendingLockConflict:
		return "PendingLockConflict"
	case CodeLockDestroyed:
		return "LockDestroyed"
	case CodeResolveError:
		return "ResolveError"
	case CodeAlreadyExists:
		return "AlreadyExists"
	case CodeMaxChildCountViolation:
		return "MaxChildCountViolation"
	case CodeMaxStringLengthViolation:
		return "MaxStringLengthViolation"
	case CodeMaxAttributeSizeViolation:
		return "MaxAttributeSizeViolation"
	case CodeMaxKeyLengthViolation:
		return "MaxKeyLengthViolation"
	case CodeNoSuchSnapshot:
		return "NoSuchSnapshot"
	case CodeNoSuchChangelog:
		return "NoSuchChangelog"
	case CodeInvalidEpoch:
		return "InvalidEpoch"
	case CodeInvalidVersion:
		return "InvalidVersion"
	case CodeOutOfOrderMutations:
		return "OutOfOrderMutations"
	case CodeInvalidSnapshotVersion:
		return "InvalidSnapshotVersion"
	case CodeReadOnlySnapshotBuilt:
		return "ReadOnlySnapshotBuilt"
	case CodeReadOnlySnapshotBuildFailed:
		return "ReadOnlySnapshotBuildFailed"
	case CodeBrokenChangelog:
		return "BrokenChangelog"
	case CodeChangelogIOError:
		return "ChangelogIOError"
	case CodeInvalidChangelogState:
		return "InvalidChangelogState"
	case CodeAllTargetNodesFailed:
		return "AllTargetNodesFailed"
	case CodeSendBlocksFailed:
		return "SendBlocksFailed"
	case CodeNoSuchSession:
		return "NoSuchSession"
	case CodeSessionAlreadyExists:
		return "SessionAlreadyExists"
	case CodeChunkAlreadyExists:
		return "ChunkAlreadyExists"
	case CodeWindowError:
		return "WindowError"
	case CodeBlockContentMismatch:
		return "BlockContentMismatch"
	case CodeNoSuchBlock:
		return "NoSuchBlock"
	case CodeNoSuchChunk:
		return "NoSuchChunk"
	case CodeNoLocationAvailable:
		return "NoLocationAvailable"
	case CodeIOError:
		return "IOError"
	case CodeMasterCommunicationFailed:
		return "MasterCommunicationFailed"
	case CodeNoSuchChunkTree:
		return "NoSuchChunkTree"
	case CodeMasterNotConnected:
		return "MasterNotConnected"
	case CodeChunkUnavailable:
		return "ChunkUnavailable"
	case CodeNoSuchChunkList:
		return "NoSuchChunkList"
	case CodeWriteThrottlingActive:
		return "WriteThrottlingActive"
	case CodeNoSuchMedium:
		return "NoSuchMedium"
	case CodeOptimisticLockFailure:
		return "OptimisticLockFailure"
	case CodeInvalidBlockChecksum:
		return "InvalidBlockChecksum"
	case CodeBlockOutOfRange:
		return "BlockOutOfRange"
	case CodeMissingExtension:
		return "MissingExtension"
	case CodeBandwidthThrottlingFailed:
		return "BandwidthThrottlingFailed"
	case CodeReaderTimeout:
		return "ReaderTimeout"
	case CodeNoSuchChunkView:
		return "NoSuchChunkView"
	case CodeIncorrectChunkFileChecksum:
		return "IncorrectChunkFileChecksum"
	case CodeIncorrectChunkFileHeaderSignature:
		return "IncorrectChunkFileHeaderSignature"
	case CodeIncorrectLayerFileSize:
		return "IncorrectLayerFileSize"
	case CodeNoSpaceLeftOnDevice:
		return "NoSpaceLeftOnDevice"
	case CodeConcurrentChunkUpdate:
		return "ConcurrentChunkUpdate"
	case CodeInvalidElectionState:
		return "InvalidElectionState"
	case CodeInvalidLeader:
		return "InvalidLeader"
	case CodeInvalidElectionEpoch:
		return "InvalidElectionEpoch"
	case CodeAuthenticationError:
		return "AuthenticationError"
	case CodeAuthorizationError:
		return "AuthorizationError"
	case CodeAccountLimitExceeded:
		return "AccountLimitExceeded"
	case CodeUserBanned:
		return "UserBanned"
	case CodeRequestQueueSizeLimitExceeded:
		return "RequestQueueSizeLimitExceeded"
	case CodeNoSuchAccount:
		return "NoSuchAccount"
	case CodeSafeModeEnabled:
		return "SafeModeEnabled"
	case CodeNoSuchSubject:
		return "NoSuchSubject"
	case CodePrerequisiteCheckFailed:
		return "PrerequisiteCheckFailed"
	case CodeInvalidObjectLifeStage:
		return "InvalidObjectLifeStage"
	case CodeCrossCellAdditionalPath:
		return "CrossCellAdditionalPath"
	case CodeCrossCellRevisionPrerequisitePath:
		return "CrossCellRevisionPrerequisitePath"
	case CodeForwardedRequestFailed:
		return "ForwardedRequestFailed"
	case CodeCannotCacheMutatingRequest:
		return "CannotCacheMutatingRequest"
	case CodeConfigCreationFailed:
		return "ConfigCreationFailed"
	case CodeAbortByScheduler:
		return "AbortByScheduler"
	case CodeResourceOverdraft:
		return "ResourceOverdraft"
	case CodeWaitingJobTimeout:
		return "WaitingJobTimeout"
	case CodeSlotNotFound:
		return "SlotNotFound"
	case CodeJobEnvironmentDisabled:
		return "JobEnvironmentDisabled"
	case CodeJobProxyConnectionFailed:
		return "JobProxyConnectionFailed"
	case CodeArtifactCopyingFailed:
		return "ArtifactCopyingFailed"
	case CodeNodeDirectoryPreparationFailed:
		return "NodeDirectoryPreparationFailed"
	case CodeSlotLocationDisabled:
		return "SlotLocationDisabled"
	case CodeQuotaSettingFailed:
		return "QuotaSettingFailed"
	case CodeRootVolumePreparationFailed:
		return "RootVolumePreparationFailed"
	case CodeNotEnoughDiskSpace:
		return "NotEnoughDiskSpace"
	case CodeArtifactDownloadFailed:
		return "ArtifactDownloadFailed"
	case CodeJobProxyPreparationTimeout:
		return "JobProxyPreparationTimeout"
	case CodeJobPreparationTimeout:
		return "JobPreparationTimeout"
	case CodeJobProxyFailed:
		return "JobProxyFailed"
	case CodeSetupCommandFailed:
		return "SetupCommandFailed"
	case CodeGpuLayerNotFetched:
		return "GpuLayerNotFetched"
	case CodeGpuJobWithoutLayers:
		return "GpuJobWithoutLayers"
	case CodeTmpfsOverflow:
		return "TmpfsOverflow"
	case CodeGpuCheckCommandFailed:
		return "GpuCheckCommandFailed"
	case CodeGpuCheckCommandIncorrect:
		return "GpuCheckCommandIncorrect"
	case CodeMemoryLimitExceeded:
		return "MemoryLimitExceeded"
	case CodeMemoryCheckFailed:
		return "MemoryCheckFailed"
	case CodeJobTimeLimitExceeded:
		return "JobTimeLimitExceeded"
	case CodeUnsupportedJobType:
		return "UnsupportedJobType"
	case CodeJobNotPrepared:
		return "JobNotPrepared"
	case CodeUserJobFailed:
		return "UserJobFailed"
	case CodeUserJobProducedCoreFiles:
		return "UserJobProducedCoreFiles"
	case CodeLocalChunkReaderFailed:
		return "LocalChunkReaderFailed"
	case CodeLayerUnpackingFailed:
		return "LayerUnpackingFailed"
	case CodeNodeDecommissioned:
		return "NodeDecommissioned"
	case CodeNodeBanned:
		return "NodeBanned"
	case CodeNodeTabletSlotsDisabled:
		return "NodeTabletSlotsDisabled"
	case CodeNodeFilterMismatch:
		return "NodeFilterMismatch"
	case CodeCellDidNotAppearWithinTimeout:
		return "CellDidNotAppearWithinTimeout"
	case CodeAborted:
		return "Aborted"
	case CodeResolveTimedOut:
		return "ResolveTimedOut"
	case CodeNoSuchNode:
		return "NoSuchNode"
	case CodeInvalidState:
		return "InvalidState"
	case CodeNoSuchNetwork:
		return "NoSuchNetwork"
	case CodeNoSuchRack:
		return "NoSuchRack"
	case CodeNoSuchDataCenter:
		return "NoSuchDataCenter"
	case CodeTransactionLockConflict:
		return "TransactionLockConflict"
	case CodeNoSuchTablet:
		return "NoSuchTablet"
	case CodeTabletNotMounted:
		return "TabletNotMounted"
	case CodeAllWritesDisabled:
		return "AllWritesDisabled"
	case CodeInvalidMountRevision:
		return "InvalidMountRevision"
	case CodeTableReplicaAlreadyExists:
		return "TableReplicaAlreadyExists"
	case CodeInvalidTabletState:
		return "InvalidTabletState"
	case CodeTableMountInfoNotReady:
		return "TableMountInfoNotReady"
	case CodeTabletSnapshotExpired:
		return "TabletSnapshotExpired"
	case CodeQueryInputRowCountLimitExceeded:
		return "QueryInputRowCountLimitExceeded"
	case CodeQueryOutputRowCountLimitExceeded:
		return "QueryOutputRowCountLimitExceeded"
	case CodeQueryExpressionDepthLimitExceeded:
		return "QueryExpressionDepthLimitExceeded"
	case CodeRowIsBlocked:
		return "RowIsBlocked"
	case CodeBlockedRowWaitTimeout:
		return "BlockedRowWaitTimeout"
	case CodeNoSyncReplicas:
		return "NoSyncReplicas"
	case CodeTableMustNotBeReplicated:
		return "TableMustNotBeReplicated"
	case CodeTableMustBeSorted:
		return "TableMustBeSorted"
	case CodeTooManyRowsInTransaction:
		return "TooManyRowsInTransaction"
	case CodeUpstreamReplicaMismatch:
		return "UpstreamReplicaMismatch"
	case CodeNoSuchDynamicStore:
		return "NoSuchDynamicStore"
	case CodeShellExited:
		return "ShellExited"
	case CodeShellManagerShutDown:
		return "ShellManagerShutDown"
	case CodeTooManyConcurrentRequests:
		return "TooManyConcurrentRequests"
	case CodeJobArchiveUnavailable:
		return "JobArchiveUnavailable"
	case CodeRetriableArchiveError:
		return "RetriableArchiveError"
	case CodeAPINoSuchOperation:
		return "APINoSuchOperation"
	case CodeAPINoSuchJob:
		return "APINoSuchJob"
	case CodeNoSuchAttribute:
		return "NoSuchAttribute"
	case CodeDataSliceLimitExceeded:
		return "DataSliceLimitExceeded"
	case CodeMaxDataWeightPerJobExceeded:
		return "MaxDataWeightPerJobExceeded"
	case CodeMaxPrimaryDataWeightPerJobExceeded:
		return "MaxPrimaryDataWeightPerJobExceeded"
	case CodeProxyBanned:
		return "ProxyBanned"
	case CodeSubqueryDataWeightLimitExceeded:
		return "SubqueryDataWeightLimitExceeded"
	case CodeParticipantUnregistered:
		return "ParticipantUnregistered"
	case CodeNoSuchGroup:
		return "NoSuchGroup"
	case CodeNoSuchMember:
		return "NoSuchMember"
	case CodeNoSuchThrottler:
		return "NoSuchThrottler"
	case CodeUnexpectedThrottlerMode:
		return "UnexpectedThrottlerMode"
	case CodeUnrecognizedConfigOption:
		return "UnrecognizedConfigOption"
	case CodeFailedToFetchDynamicConfig:
		return "FailedToFetchDynamicConfig"
	case CodeDuplicateMatchingDynamicConfigs:
		return "DuplicateMatchingDynamicConfigs"
	case CodeUnrecognizedDynamicConfigOption:
		return "UnrecognizedDynamicConfigOption"
	case CodeFailedToApplyDynamicConfig:
		return "FailedToApplyDynamicConfig"
	case CodeInvalidDynamicConfig:
		return "InvalidDynamicConfig"
	case CodeAgentCallFailed:
		return "AgentCallFailed"
	case CodeNoOnlineNodeToScheduleJob:
		return "NoOnlineNodeToScheduleJob"
	case CodeMaterializationFailed:
		return "MaterializationFailed"
	case CodeNoSuchTransaction:
		return "NoSuchTransaction"
	case CodeNestedExternalTransactionExists:
		return "NestedExternalTransactionExists"
	case CodeTransactionDepthLimitReached:
		return "TransactionDepthLimitReached"
	case CodeInvalidTransactionState:
		return "InvalidTransactionState"
	case CodeParticipantFailedToPrepare:
		return "ParticipantFailedToPrepare"
	case CodeSomeParticipantsAreDown:
		return "SomeParticipantsAreDown"
	case CodeAlienTransactionsForbidden:
		return "AlienTransactionsForbidden"
	case CodeMalformedAlienTransaction:
		return "MalformedAlienTransaction"
	case CodeInvalidTransactionAtomicity:
		return "InvalidTransactionAtomicity"
	case CodeUploadTransactionCannotHaveNested:
		return "UploadTransactionCannotHaveNested"
	case CodeForeignParentTransaction:
		return "ForeignParentTransaction"
	case CodeForeignPrerequisiteTransaction:
		return "ForeignPrerequisiteTransaction"
	case CodeFailedToStartContainer:
		return "FailedToStartContainer"
	case CodeJobIsNotRunning:
		return "JobIsNotRunning"
	default:
		return fmt.Sprintf("UnknownCode%d", int(e))
	}
}
