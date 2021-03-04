// Copyright (c) Improbable Worlds Ltd, All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "SpatialEventTracingTest.h"

#include "ProcessRPCEventTracingTest.generated.h"

UCLASS()
class SPATIALGDKFUNCTIONALTESTS_API AProcessRPCEventTracingTest : public ASpatialEventTracingTest
{
	GENERATED_BODY()

public:
	AProcessRPCEventTracingTest();

private:
	virtual void FinishEventTraceTest() override;
};
