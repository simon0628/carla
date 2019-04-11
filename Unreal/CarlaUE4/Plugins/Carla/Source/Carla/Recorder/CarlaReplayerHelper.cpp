// Copyright (c) 2017 Computer Vision Center (CVC) at the Universitat Autonoma
// de Barcelona (UAB).
//
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.

#include "CarlaReplayerHelper.h"
#include "Carla/Actor/ActorView.h"
#include "Carla/Actor/ActorDescription.h"

// create or reuse an actor for replaying
std::pair<int, FActorView>CarlaReplayerHelper::TryToCreateReplayerActor(
    FVector &Location,
    FVector &Rotation,
    FActorDescription &ActorDesc,
    uint32_t DesiredId)
{
  check(Episode != nullptr);

  FActorView view_empty;

  // check type of actor we need
  if (ActorDesc.Id.StartsWith("traffic."))
  {
    AActor *Actor = FindTrafficLightAt(Location);
    if (Actor != nullptr)
    {
      // actor found
      auto view = Episode->GetActorRegistry().Find(Actor);
      // reuse that actor
      // UE_LOG(LogCarla, Log, TEXT("TrafficLight found with id: %d"), view.GetActorId());
      return std::pair<int, FActorView>(2, view);
    }
    else
    {
      // actor not found
      UE_LOG(LogCarla, Log, TEXT("TrafficLight not found"));
      return std::pair<int, FActorView>(0, view_empty);
    }
  }
  else if (!ActorDesc.Id.StartsWith("sensor."))
  {
    // check if an actor of that type already exist with same id
    // UE_LOG(LogCarla, Log, TEXT("Trying to create actor: %s (%d)"), *ActorDesc.Id, DesiredId);
    if (Episode->GetActorRegistry().Contains(DesiredId))
    {
      auto view = Episode->GetActorRegistry().Find(DesiredId);
      const FActorDescription *desc = &view.GetActorInfo()->Description;
      // UE_LOG(LogCarla, Log, TEXT("actor '%s' already exist with id %d"), *(desc->Id), view.GetActorId());
      if (desc->Id == ActorDesc.Id)
      {
        // we don't need to create, actor of same type already exist
        return std::pair<int, FActorView>(2, view);
      }
    }
    // create the transform
    FRotator Rot = FRotator::MakeFromEuler(Rotation);
    // FTransform Trans(Rot, Location, FVector(1, 1, 1));
    FTransform Trans(Rot, FVector(0, 0, 100000), FVector(1, 1, 1));
    // create as new actor
    TPair<EActorSpawnResultStatus, FActorView> Result = Episode->SpawnActorWithInfo(Trans, ActorDesc, DesiredId);
    if (Result.Key == EActorSpawnResultStatus::Success)
    {
      // UE_LOG(LogCarla, Log, TEXT("Actor created by replayer with id %d"), Result.Value.GetActorId());
      // relocate
      FTransform Trans2(Rot, Location, FVector(1, 1, 1));
      Result.Value.GetActor()->SetActorTransform(Trans2, false, nullptr, ETeleportType::TeleportPhysics);
      // Result.Value.GetActor()->SetLocation(Trans2);
      return std::pair<int, FActorView>(1, Result.Value);
    }
    else
    {
      UE_LOG(LogCarla, Log, TEXT("Actor could't be created by replayer"));
      return std::pair<int, FActorView>(0, Result.Value);
    }
  }
  else
  {
    // actor ignored
    return std::pair<int, FActorView>(0, view_empty);
  }
}

AActor *CarlaReplayerHelper::FindTrafficLightAt(FVector Location)
{
  check(Episode != nullptr);
  auto World = Episode->GetWorld();
  check(World != nullptr);

  // get its position (truncated as int's)
  int x = static_cast<int>(Location.X);
  int y = static_cast<int>(Location.Y);
  int z = static_cast<int>(Location.Z);
  // UE_LOG(LogCarla, Log, TEXT("Trying to find traffic: [%d,%d,%d]"), x, y, z);

  // search an "traffic." actor at that position
  for (TActorIterator<ATrafficSignBase> It(World); It; ++It)
  {
    ATrafficSignBase *Actor = *It;
    check(Actor != nullptr);
    FVector vec = Actor->GetTransform().GetTranslation();
    int x2 = static_cast<int>(vec.X);
    int y2 = static_cast<int>(vec.Y);
    int z2 = static_cast<int>(vec.Z);
    // UE_LOG(LogCarla, Log, TEXT(" Checking with [%d,%d,%d]"), x2, y2, z2);
    if ((x2 == x) && (y2 == y) && (z2 == z))
    {
      // actor found
      return static_cast<AActor *>(Actor);
    }
  }
  // actor not found
  return nullptr;
}

// enable / disable physics for an actor
bool CarlaReplayerHelper::SetActorSimulatePhysics(FActorView &ActorView, bool bEnabled)
{
  if (!ActorView.IsValid())
  {
    return false;
  }
  auto RootComponent = Cast<UPrimitiveComponent>(ActorView.GetActor()->GetRootComponent());
  if (RootComponent == nullptr)
  {
    return false;
  }
  RootComponent->SetSimulatePhysics(bEnabled);

  return true;
}

// enable / disable autopilot for an actor
bool CarlaReplayerHelper::SetActorAutopilot(FActorView &ActorView, bool bEnabled)
{
  if (!ActorView.IsValid())
  {
    return false;
  }
  auto Vehicle = Cast<ACarlaWheeledVehicle>(ActorView.GetActor());
  if (Vehicle == nullptr)
  {
    return false;
  }
  auto Controller = Cast<AWheeledVehicleAIController>(Vehicle->GetController());
  if (Controller == nullptr)
  {
    return false;
  }
  Controller->SetAutopilot(bEnabled);

  return true;
}

// replay event for creating actor
std::pair<int, uint32_t> CarlaReplayerHelper::ProcessReplayerEventAdd(
    FVector Location,
    FVector Rotation,
    CarlaRecorderActorDescription Description,
    uint32_t DesiredId)
{
  check(Episode != nullptr);
  FActorDescription ActorDesc;

  // prepare actor description
  ActorDesc.UId = Description.UId;
  ActorDesc.Id = Description.Id;
  for (const auto &Item : Description.Attributes)
  {
    FActorAttribute Attr;
    Attr.Type = static_cast<EActorAttributeType>(Item.Type);
    Attr.Id = Item.Id;
    Attr.Value = Item.Value;
    ActorDesc.Variations.Add(Attr.Id, std::move(Attr));
  }

  auto result = TryToCreateReplayerActor(Location, Rotation, ActorDesc, DesiredId);

  if (result.first != 0)
  {
    // disable physics
    // SetActorSimulatePhysics(result.second, false);
    // disable autopilot
    SetActorAutopilot(result.second, false);
  }

  return std::make_pair(result.first, result.second.GetActorId());
}

// replay event for removing actor
bool CarlaReplayerHelper::ProcessReplayerEventDel(uint32_t DatabaseId)
{
  check(Episode != nullptr);
  auto actor = Episode->GetActorRegistry().Find(DatabaseId).GetActor();
  if (actor == nullptr)
  {
    UE_LOG(LogCarla, Log, TEXT("Actor %d not found to destroy"), DatabaseId);
    return false;
  }
  Episode->DestroyActor(actor);
  return true;
}

// replay event for parenting actors
bool CarlaReplayerHelper::ProcessReplayerEventParent(uint32_t ChildId, uint32_t ParentId)
{
  check(Episode != nullptr);
  AActor *child = Episode->GetActorRegistry().Find(ChildId).GetActor();
  AActor *parent = Episode->GetActorRegistry().Find(ParentId).GetActor();
  if (child && parent)
  {
    child->AttachToActor(parent, FAttachmentTransformRules::KeepRelativeTransform);
    child->SetOwner(parent);
    // UE_LOG(LogCarla, Log, TEXT("Parenting Actor"));
    return true;
  }
  else
  {
    UE_LOG(LogCarla, Log, TEXT("Parenting Actors not found"));
    return false;
  }
}

// reposition actors
bool CarlaReplayerHelper::ProcessReplayerPosition(CarlaRecorderPosition Pos1, CarlaRecorderPosition Pos2, double Per)
{
  check(Episode != nullptr);
  AActor *Actor = Episode->GetActorRegistry().Find(Pos1.DatabaseId).GetActor();
  if (Actor  && !Actor->IsPendingKill())
  {
    // check to assign first position or interpolate between both
    if (Per == 0.0)
    {
      // assign position 1
      FTransform Trans(FRotator::MakeFromEuler(Pos1.Rotation), FVector(Pos1.Location), FVector(1, 1, 1));
      Actor->SetActorTransform(Trans, false, nullptr, ETeleportType::TeleportPhysics);
    }
    else
    {
      // interpolate positions
      FVector Location = FMath::Lerp(FVector(Pos1.Location), FVector(Pos2.Location), Per);
      FRotator Rotation = FMath::Lerp(FRotator::MakeFromEuler(Pos1.Rotation), FRotator::MakeFromEuler(Pos2.Rotation), Per);
      FTransform Trans(Rotation, Location, FVector(1, 1, 1));
      Actor->SetActorTransform(Trans, false, nullptr, ETeleportType::TeleportPhysics);
    }
    // reset velocities
    ResetVelocities(Actor);
    return true;
  }
  return false;
}

// reset velocity vectors on actor
void CarlaReplayerHelper::ResetVelocities(AActor *Actor)
{
  if (Actor  && !Actor->IsPendingKill())
  {
    auto RootComponent = Cast<UPrimitiveComponent>(Actor->GetRootComponent());
    if (RootComponent != nullptr)
    {
      FVector Vector(0, 0, 0);
      // reset velocities
      RootComponent->SetPhysicsLinearVelocity(Vector, false, "None");
      RootComponent->SetPhysicsAngularVelocityInDegrees(Vector, false, "None");
    }
  }
}

// reposition the camera
bool CarlaReplayerHelper::SetCameraPosition(uint32_t Id, FVector Offset, FQuat Rotation)
{
  check(Episode != nullptr);

  // get specator pawn
  APawn *Spectator = Episode->GetSpectatorPawn();
  // get the actor to follow
  AActor *Actor = Episode->FindActor(Id).GetActor();

  // check
  if (!Spectator || !Actor)
   return false;

  // set the new position
  FQuat ActorRot = Actor->GetActorTransform().GetRotation();
  FVector Pos = Actor->GetActorTransform().GetTranslation() + (ActorRot.RotateVector(Offset));
  Spectator->SetActorLocation(Pos);
  Spectator->SetActorRotation(ActorRot * Rotation);

  // UE_LOG(LogCarla, Log, TEXT("Set camera at [%d,%d,%d]"), Pos.X, Pos.Y, Pos.Z);
  return true;
}

bool CarlaReplayerHelper::ProcessReplayerStateTrafficLight(CarlaRecorderStateTrafficLight State)
{
  check(Episode != nullptr);
  AActor *Actor = Episode->GetActorRegistry().Find(State.DatabaseId).GetActor();
  if (Actor && !Actor->IsPendingKill())
  {
    auto TrafficLight = Cast<ATrafficLightBase>(Actor);
    if (TrafficLight != nullptr)
    {
      TrafficLight->SetTrafficLightState(static_cast<ETrafficLightState>(State.State));
      TrafficLight->SetTimeIsFrozen(State.IsFrozen);
      TrafficLight->SetElapsedTime(State.ElapsedTime);
    }
    return true;
  }
  return false;
}

// replay finish
bool CarlaReplayerHelper::ProcessReplayerFinish(bool bApplyAutopilot)
{
  if (!bApplyAutopilot)
  {
    return false;
  }
  // set autopilot to all AI vehicles
  auto registry = Episode->GetActorRegistry();
  for (auto ActorView : registry)
  {
    SetActorAutopilot(ActorView, true);
  }
  return true;
}

