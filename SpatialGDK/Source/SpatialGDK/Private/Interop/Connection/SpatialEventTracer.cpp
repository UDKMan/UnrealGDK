// Copyright (c) Improbable Worlds Ltd, All Rights Reserved

#include "Interop/Connection/SpatialEventTracer.h"

#include <inttypes.h>

#include "HAL/PlatformFile.h"
#include "HAL/PlatformFilemanager.h"
#include "Interop/Connection/SpatialTraceEventBuilder.h"
#include "SpatialGDKSettings.h"

DEFINE_LOG_CATEGORY(LogSpatialEventTracer);

namespace SpatialGDK
{
void SpatialEventTracer::TraceCallback(void* UserData, const Trace_Item* Item)
{
	SpatialEventTracer* EventTracer = static_cast<SpatialEventTracer*>(UserData);

	Io_Stream* Stream = EventTracer->Stream.Get();
	if (!ensure(Stream != nullptr))
	{
		return;
	}

	uint32_t ItemSize = Trace_GetSerializedItemSize(Item);
	if (EventTracer->BytesWrittenToStream + ItemSize <= EventTracer->MaxFileSize)
	{
		EventTracer->BytesWrittenToStream += ItemSize;
		int Code = Trace_SerializeItemToStream(Stream, Item, ItemSize);
		if (Code != 1)
		{
			UE_LOG(LogSpatialEventTracer, Error, TEXT("Failed to serialize to with error code %d (%s"), Code, Trace_GetLastError());
		}
	}
	else
	{
		EventTracer->BytesWrittenToStream = EventTracer->MaxFileSize;
	}
}

SpatialScopedActiveSpanId::SpatialScopedActiveSpanId(SpatialEventTracer* InEventTracer, const TOptional<Trace_SpanId>& InCurrentSpanId)
	: CurrentSpanId(InCurrentSpanId)
	, EventTracer(InEventTracer->GetWorkerEventTracer())
{
	if (InCurrentSpanId.IsSet())
	{
		Trace_EventTracer_SetActiveSpanId(EventTracer, CurrentSpanId.GetValue());
	}
}

SpatialScopedActiveSpanId::~SpatialScopedActiveSpanId()
{
	if (CurrentSpanId.IsSet())
	{
		Trace_EventTracer_ClearActiveSpanId(EventTracer);
	}
}

SpatialEventTracer::SpatialEventTracer(const FString& WorkerId)
	: SpanIdStack(this)
{
	if (const USpatialGDKSettings* Settings = GetDefault<USpatialGDKSettings>())
	{
		if (Settings->bEventTracingEnabled)
		{
			MaxFileSize = Settings->MaxEventTracingFileSizeBytes;
			Enable(WorkerId);
		}
	}
}

SpatialEventTracer::~SpatialEventTracer()
{
	if (IsEnabled())
	{
		UE_LOG(LogSpatialEventTracer, Log, TEXT("Spatial event tracing disabled."));
		Trace_EventTracer_Disable(EventTracer);
		Trace_EventTracer_Destroy(EventTracer);
	}
}

FString SpatialEventTracer::SpanIdToString(const Trace_SpanId& SpanId)
{
	FString HexStr;
	for (int i = 0; i < 16; i++)
	{
		HexStr += FString::Printf(TEXT("%02x"), SpanId.data[i]);
	}
	return HexStr;
}

FUserSpanId SpatialEventTracer::SpanIdToUserSpanId(const Trace_SpanId& SpanId)
{
	FUserSpanId UserSpanId;
	UserSpanId.Data.Reserve(16);

	for (int i = 0; i < 16; i++)
	{
		UserSpanId.Data.Add(static_cast<uint8>(SpanId.data[i]));
	}

	return UserSpanId;
}

TOptional<Trace_SpanId> SpatialEventTracer::UserSpanIdToSpanId(const FUserSpanId& UserSpanId)
{
	if (!UserSpanId.IsValid())
	{
		return {};
	}

	Trace_SpanId SpanId;
	for (int i = 0; i < 16; i++)
	{
		SpanId.data[i] = static_cast<unsigned char>(UserSpanId.Data[i]);
	}
	return SpanId;
}

TOptional<Trace_SpanId> SpatialEventTracer::CreateSpan() const
{
	if (!IsEnabled())
	{
		return {};
	}

	return Trace_EventTracer_AddSpan(EventTracer, nullptr, 0);
}

TOptional<Trace_SpanId> SpatialEventTracer::CreateSpan(const Trace_SpanId* Causes, int32 NumCauses) const
{
	if (!IsEnabled())
	{
		return {};
	}

	if (Causes != nullptr && NumCauses > 0)
	{
		return Trace_EventTracer_AddSpan(EventTracer, Causes, NumCauses);
	}

	return Trace_EventTracer_AddSpan(EventTracer, nullptr, 0);
}

void SpatialEventTracer::TraceEvent(const FSpatialTraceEvent& SpatialTraceEvent, const TOptional<Trace_SpanId>& OptionalSpanId)
{
	if (!IsEnabled())
	{
		return;
	}

	auto MessageSrc = StringCast<ANSICHAR>(*SpatialTraceEvent.Message);
	const ANSICHAR* Message = MessageSrc.Get();

	auto TypeSrc = StringCast<ANSICHAR>(*SpatialTraceEvent.Type);
	const ANSICHAR* Type = TypeSrc.Get();

	Trace_Event TraceEvent{ OptionalSpanId.GetValue(), /* unix_timestamp_millis: ignored */ 0, Message, Type, nullptr };
	if (!Trace_EventTracer_ShouldSampleEvent(EventTracer, &TraceEvent))
	{
		return;
	}

	Trace_EventData* EventData = Trace_EventData_Create();

	for (const auto& Pair : SpatialTraceEvent.Data)
	{
		auto KeySrc = StringCast<ANSICHAR>(*Pair.Key);
		const ANSICHAR* Key = KeySrc.Get();
		auto ValueSrc = StringCast<ANSICHAR>(*Pair.Value);
		const ANSICHAR* Value = ValueSrc.Get();
		Trace_EventData_AddStringFields(EventData, 1, &Key, &Value);
	}

	// Frame counter
	{
		const char* FrameCountStr = "FrameNum";
		char TmpBuffer[64];
		FCStringAnsi::Sprintf(TmpBuffer, "%" PRIu64, GFrameCounter);
		const char* TmpBufferPtr = TmpBuffer;
		Trace_EventData_AddStringFields(EventData, 1, &FrameCountStr, &TmpBufferPtr);
	}

	TraceEvent.data = EventData;
	Trace_EventTracer_AddEvent(EventTracer, &TraceEvent);
	Trace_EventData_Destroy(EventData);
}

bool SpatialEventTracer::IsEnabled() const
{
	return bEnabled;
}

void SpatialEventTracer::Enable(const FString& FileName)
{
	Trace_EventTracer_Parameters parameters = {};
	parameters.user_data = this;
	parameters.callback = &SpatialEventTracer::TraceCallback;
	EventTracer = Trace_EventTracer_Create(&parameters);
	Trace_EventTracer_Enable(EventTracer);
	bEnabled = true;

	UE_LOG(LogSpatialEventTracer, Log, TEXT("Spatial event tracing enabled."));

	// Open a local file
	FString EventTracePath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("EventTracing"));
	FString AbsLogPath;
	if (FParse::Value(FCommandLine::Get(), TEXT("eventLogPath="), AbsLogPath, false))
	{
		EventTracePath = FPaths::GetPath(AbsLogPath);
	}

	FolderPath = EventTracePath;
	const FString FullFileName = FString::Printf(TEXT("EventTrace_%s_%s.trace"), *FileName, *FDateTime::Now().ToString());
	const FString FilePath = FPaths::Combine(FolderPath, FullFileName);

	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	if (PlatformFile.CreateDirectoryTree(*FolderPath))
	{
		UE_LOG(LogSpatialEventTracer, Log, TEXT("Capturing trace to %s."), *FilePath);
		Stream.Reset(Io_CreateFileStream(TCHAR_TO_ANSI(*FilePath), Io_OpenMode::IO_OPEN_MODE_WRITE));
	}
}

void SpatialEventTracer::StreamDeleter::operator()(Io_Stream* StreamToDestroy) const
{
	Io_Stream_Destroy(StreamToDestroy);
}

void SpatialEventTracer::AddComponent(Worker_EntityId EntityId, Worker_ComponentId ComponentId, const Trace_SpanId& SpanId)
{
	if (Trace_SpanId_Equal(SpanId, Trace_SpanId_Null()))
	{
		return;
	}

	EntityComponentSpanIds.FindOrAdd({ EntityId, ComponentId }, SpanId);
}

void SpatialEventTracer::RemoveComponent(Worker_EntityId EntityId, Worker_ComponentId ComponentId)
{
	EntityComponentSpanIds.Remove({ EntityId, ComponentId });
}

void SpatialEventTracer::UpdateComponent(Worker_EntityId EntityId, Worker_ComponentId ComponentId, const Trace_SpanId& SpanId)
{
	if (Trace_SpanId_Equal(SpanId, Trace_SpanId_Null()))
	{
		return;
	}

	Trace_SpanId& StoredSpanId = EntityComponentSpanIds.FindChecked({ EntityId, ComponentId });

	Trace_SpanId MergeCauses[2] = { SpanId, StoredSpanId };
	TOptional<Trace_SpanId> NewSpanId = CreateSpan(MergeCauses, 2);
	TraceEvent(FSpatialTraceEventBuilder::CreateMergeComponentUpdate(EntityId, ComponentId), NewSpanId);

	StoredSpanId = NewSpanId.GetValue();
}

TOptional<Trace_SpanId> SpatialEventTracer::GetSpanId(const EntityComponentId& Id) const
{
	if (!IsEnabled())
	{
		return {};
	}

	const Trace_SpanId* SpanId = EntityComponentSpanIds.Find(Id);
	check(SpanId != nullptr)

		return *SpanId;
}

void SpatialEventTracer::AddLatentPropertyUpdateSpanId(const EntityComponentId& Id, const Trace_SpanId& SpanId)
{
	if (!IsEnabled())
	{
		return;
	}

	FSpatialSpanIdStack* Stack = EntityComponentSpanIdStacks.Find(Id);
	if (Stack == nullptr)
	{
		Stack = &EntityComponentSpanIdStacks.Add(Id);
		Stack->SetEventTracer(this);
	}

	Stack->Add(SpanId);
}

TOptional<Trace_SpanId> SpatialEventTracer::PopLatentPropertyUpdateSpanId(const EntityComponentId& Id)
{
	if (!IsEnabled())
	{
		return {};
	}

	FSpatialSpanIdStack* Stack = EntityComponentSpanIdStacks.Find(Id);
	if (Stack == nullptr)
	{
		return {};
	}

	TOptional<Trace_SpanId> SpanId = Stack->Pop();
	if (!Stack->HasSpanId())
	{
		EntityComponentSpanIdStacks.Remove(Id);
	}

	return SpanId;
}

} // namespace SpatialGDK
