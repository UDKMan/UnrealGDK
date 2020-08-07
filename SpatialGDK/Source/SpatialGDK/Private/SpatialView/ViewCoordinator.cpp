// Copyright (c) Improbable Worlds Ltd, All Rights Reserved

#include "SpatialView/ViewCoordinator.h"
#include "SpatialView/OpList/ViewDeltaLegacyOpList.h"

namespace SpatialGDK
{
ViewCoordinator::ViewCoordinator(TUniquePtr<AbstractConnectionHandler> ConnectionHandler)
	: ConnectionHandler(MoveTemp(ConnectionHandler))
	, NextRequestId(1)
{
}

ViewCoordinator::~ViewCoordinator()
{
	FlushMessagesToSend();
}

OpList ViewCoordinator::Advance()
{
	ConnectionHandler->Advance();
	const uint32 OpListCount = ConnectionHandler->GetOpListCount();
	for (uint32 i = 0; i < OpListCount; ++i)
	{
		View.EnqueueOpList(ConnectionHandler->GetNextOpList());
	}
	return GetOpListFromViewDelta(View.GenerateViewDelta());
}

void ViewCoordinator::FlushMessagesToSend()
{
	ConnectionHandler->SendMessages(View.FlushLocalChanges());
}

void ViewCoordinator::SendAddComponent(Worker_EntityId EntityId, ComponentData Data, const TOptional<worker::c::Trace_SpanId>& SpanId)
{
	View.SendAddComponent(EntityId, MoveTemp(Data), SpanId);
}

void ViewCoordinator::SendComponentUpdate(Worker_EntityId EntityId, ComponentUpdate Update, const TOptional<worker::c::Trace_SpanId>& SpanId)
{
	View.SendComponentUpdate(EntityId, MoveTemp(Update), SpanId);
}

void ViewCoordinator::SendRemoveComponent(Worker_EntityId EntityId, Worker_ComponentId ComponentId, const TOptional<worker::c::Trace_SpanId>& SpanId)
{
	View.SendRemoveComponent(EntityId, ComponentId, SpanId);
}

Worker_RequestId ViewCoordinator::SendReserveEntityIdsRequest(uint32 NumberOfEntityIds, TOptional<uint32> TimeoutMillis)
{
	View.SendReserveEntityIdsRequest({ NextRequestId, NumberOfEntityIds, TimeoutMillis });
	return NextRequestId++;
}

Worker_RequestId ViewCoordinator::SendCreateEntityRequest(TArray<ComponentData> EntityComponents, TOptional<Worker_EntityId> EntityId,
														  TOptional<uint32> TimeoutMillis,
														  const TOptional<worker::c::Trace_SpanId>& SpanId)
{
	View.SendCreateEntityRequest({ NextRequestId, MoveTemp(EntityComponents), EntityId, TimeoutMillis, SpanId });
	return NextRequestId++;
}

Worker_RequestId ViewCoordinator::SendDeleteEntityRequest(Worker_EntityId EntityId, TOptional<uint32> TimeoutMillis)
{
	View.SendDeleteEntityRequest({ NextRequestId, EntityId, TimeoutMillis });
	return NextRequestId++;
}

Worker_RequestId ViewCoordinator::SendEntityQueryRequest(EntityQuery Query, TOptional<uint32> TimeoutMillis)
{
	View.SendEntityQueryRequest({ NextRequestId, MoveTemp(Query), TimeoutMillis });
	return NextRequestId++;
}

Worker_RequestId ViewCoordinator::SendEntityCommandRequest(Worker_EntityId EntityId, CommandRequest Request,
														   TOptional<uint32> TimeoutMillis)
{
	View.SendEntityCommandRequest({ EntityId, NextRequestId, MoveTemp(Request), TimeoutMillis });
	return NextRequestId++;
}

void ViewCoordinator::SendEntityCommandResponse(Worker_RequestId RequestId, CommandResponse Response, const TOptional<worker::c::Trace_SpanId>& SpanId)
{
	View.SendEntityCommandResponse({ RequestId, MoveTemp(Response), SpanId });
}

void ViewCoordinator::SendEntityCommandFailure(Worker_RequestId RequestId, FString Message, const TOptional<worker::c::Trace_SpanId>& SpanId)
{
	View.SendEntityCommandFailure({ RequestId, MoveTemp(Message), SpanId });
}

void ViewCoordinator::SendMetrics(SpatialMetrics Metrics)
{
	View.SendMetrics(MoveTemp(Metrics));
}

void ViewCoordinator::SendLogMessage(Worker_LogLevel Level, const FName& LoggerName, FString Message)
{
	View.SendLogMessage({ Level, LoggerName, MoveTemp(Message) });
}

const FString& ViewCoordinator::GetWorkerId() const
{
	return ConnectionHandler->GetWorkerId();
}

const TArray<FString>& ViewCoordinator::GetWorkerAttributes() const
{
	return ConnectionHandler->GetWorkerAttributes();
}

} // namespace SpatialGDK
