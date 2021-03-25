// Copyright (c) Improbable Worlds Ltd, All Rights Reserved

#pragma once

#include "EngineClasses/SpatialNetConnection.h"
#include "EngineClasses/SpatialNetDriver.h"

#include "Components/SceneComponent.h"
#include "Containers/UnrealString.h"
#include "Engine/EngineTypes.h"
// IMP-BEGIN Add ReplicationGraph utils function
#include "Engine/NetDriver.h"
// IMP-END
#include "GameFramework/Actor.h"
#include "GameFramework/Controller.h"
#include "GameFramework/GameMode.h"
#include "GameFramework/PlayerController.h"
#include "Math/Vector.h"

#if WITH_UNREAL_DEVELOPER_TOOLS || (!UE_BUILD_SHIPPING && !UE_BUILD_TEST)
#include "GameplayDebuggerCategoryReplicator.h"
#endif

namespace SpatialGDK
{
inline AActor* GetTopmostReplicatedOwner(const AActor* Actor)
{
	check(Actor != nullptr);

	AActor* Owner = Actor->GetOwner();
	if (Owner == nullptr || Owner->IsPendingKillPending() || !Owner->GetIsReplicated())
	{
		return nullptr;
	}

	while (Owner->GetOwner() != nullptr && !Owner->GetOwner()->IsPendingKillPending() && Owner->GetIsReplicated())
	{
		Owner = Owner->GetOwner();
	}

	return Owner;
}

inline AActor* GetReplicatedHierarchyRoot(const AActor* Actor)
{
	AActor* TopmostOwner = GetTopmostReplicatedOwner(Actor);
	return TopmostOwner != nullptr ? TopmostOwner : const_cast<AActor*>(Actor);
}

// Effectively, if this Actor is in a player hierarchy, get the PlayerController entity ID.
inline Worker_PartitionId GetConnectionOwningPartitionId(const AActor* Actor)
{
	if (const USpatialNetConnection* NetConnection = Cast<USpatialNetConnection>(Actor->GetNetConnection()))
	{
		return NetConnection->PlayerControllerEntity;
	}

	return SpatialConstants::INVALID_ENTITY_ID;
}

inline Worker_EntityId GetConnectionOwningClientSystemEntityId(const APlayerController* PC)
{
	const USpatialNetConnection* NetConnection = Cast<USpatialNetConnection>(PC->GetNetConnection());
	checkf(NetConnection != nullptr, TEXT("PlayerController did not have NetConnection when trying to find client system entity ID."));

	if (NetConnection->ConnectionClientWorkerSystemEntityId == SpatialConstants::INVALID_ENTITY_ID)
	{
		UE_LOG(LogTemp, Error,
			   TEXT("Client system entity ID was invalid on a PlayerController. "
					"This is expected after the PlayerController migrates, the client system entity ID is currently only "
					"used on the spawning server."));
	}

	return NetConnection->ConnectionClientWorkerSystemEntityId;
}

inline FVector GetActorSpatialPosition(const AActor* InActor)
{
	FVector Location = FVector::ZeroVector;

	// If the Actor is a Controller, use its Pawn's position,
	// Otherwise if the Actor is a PlayerController, use its last spectator sync location, otherwise its focal point
	// Otherwise if the Actor has an Owner, use its position.
	// Otherwise if the Actor has a well defined location then use that
	// Otherwise use the origin
	const AController* Controller = Cast<AController>(InActor);
	if (Controller != nullptr && Controller->GetPawn() != nullptr)
	{
		USceneComponent* PawnRootComponent = Controller->GetPawn()->GetRootComponent();
		Location = PawnRootComponent ? PawnRootComponent->GetComponentLocation() : FVector::ZeroVector;
	}
	else if (const APlayerController* PlayerController = Cast<APlayerController>(Controller))
	{
		if (PlayerController->IsInState(NAME_Spectating))
		{
			Location = PlayerController->LastSpectatorSyncLocation;
		}
		else
		{
			Location = PlayerController->GetFocalLocation();
		}
	}
	else if (InActor->GetOwner() != nullptr && InActor->GetOwner()->GetIsReplicated())
	{
		return GetActorSpatialPosition(InActor->GetOwner());
	}
	else if (USceneComponent* RootComponent = InActor->GetRootComponent())
	{
		Location = RootComponent->GetComponentLocation();
	}

	// Rebase location onto zero origin so actor is positioned correctly in SpatialOS.
	return FRepMovement::RebaseOntoZeroOrigin(Location, InActor);
}

inline bool DoesActorClassIgnoreVisibilityCheck(AActor* InActor)
{
	if (InActor->IsA(APlayerController::StaticClass()) || InActor->IsA(AGameModeBase::StaticClass())
#if WITH_UNREAL_DEVELOPER_TOOLS || (!UE_BUILD_SHIPPING && !UE_BUILD_TEST)
		|| InActor->IsA(AGameplayDebuggerCategoryReplicator::StaticClass())
#endif
	)

	{
		return true;
	}

	return false;
}

// IMP-BEGIN Add ReplicationGraph utils function
inline bool UsingSpatialReplicationGraph(const UObject* WorldContextObject)
{
	const UWorld* World = WorldContextObject->GetWorld();
	if (World == nullptr)
	{
		UE_LOG(LogTemp, Error, TEXT("Actor %s UsingSpatialRepGraph World==nullptr"), *GetNameSafe(WorldContextObject));
		return false;
	}

	if (USpatialNetDriver* SpatialNetDriver = Cast<USpatialNetDriver>(World->GetNetDriver()))
	{
		if (SpatialNetDriver->GetReplicationDriver() != nullptr)
		{
			return true;
		}
		UE_LOG(LogTemp, Error, TEXT("Actor %s UsingSpatialRepGraph SpatialNetDriver->GetReplicationDriver() != nullptr"),
			   *GetNameSafe(WorldContextObject));
		return false;
	}

	UE_LOG(LogTemp, Error, TEXT("Actor %s UsingSpatialRepGraph No net driver"), *GetNameSafe(WorldContextObject));

	return false;
}
// IMP-END

inline bool ShouldActorHaveVisibleComponent(AActor* InActor)
{
	// IMP-BEGIN Fix bHidden working with ReplicationGraph
	if (InActor->bAlwaysRelevant || !InActor->IsHidden() || UsingSpatialReplicationGraph(InActor)
		|| DoesActorClassIgnoreVisibilityCheck(InActor))
	// IMP-END
	{
		return true;
	}

	return false;
}

} // namespace SpatialGDK
