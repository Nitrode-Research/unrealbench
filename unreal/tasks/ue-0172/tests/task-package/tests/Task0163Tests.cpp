#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Editor.h"
#include "Tests/AutomationEditorCommon.h"
#include "Editor/LinkAssetTools.h"
#include "Rules/LinkSceneCoordinates.h"
#include "Rules/LinkSeededChain.h"
#include "Gameplay/LinkSceneLayout.h"
#include "Gameplay/LinkChain.h"
#include "Gameplay/LinkCharacter.h"
#include "Gameplay/LinkPlayerController.h"
#include "Gameplay/LinkSourceItemDisplay.h"
#include "Gameplay/LinkSession.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/GameInstance.h"
#include "EngineUtils.h"
#include "Components/StaticMeshComponent.h"
#include "ProceduralMeshComponent.h"
#include "PhysicsEngine/BodySetup.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "NavModifierComponent.h"
#include "NavAreas/NavArea_Null.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformTime.h"

namespace Task0170
{
constexpr auto Flags=EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter;
TArray<double> Identity(){return {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};}
FVector V(const TSharedPtr<FJsonValue>& Value){const auto& A=Value->AsArray();return FVector(A[0]->AsNumber(),A[1]->AsNumber(),A[2]->AsNumber());}
// Evaluator conversion deliberately does not call the submitted coordinate helpers.
FVector Cm(const FVector& P){return FVector(P.Z,P.X,P.Y)*100.;}
TSharedPtr<FJsonObject> Read(const FString& File)
{
    FString Text;TSharedPtr<FJsonObject> Out;
    if(!FFileHelper::LoadFileToString(Text,*File)||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Out)){return nullptr;}return Out;
}
UStaticMesh* Build(const TArray<double>& Matrix,bool Collision=true)
{
    const FString File=FPaths::ProjectSavedDir()/TEXT("Task0170Fixture.json");
    // Off-centre triangle; two UV channels and nonwhite colours expose attribute loss.
    const FString Json=TEXT(R"({"vertices":[[1,0,2],[3,0,2],[1,0,5]],"normals":[[0,1,0],[0,1,0],[0,1,0]],"tangents":[[1,0,0,-1],[1,0,0,-1],[1,0,0,-1]],"colors":[[0.2,0.4,0.6,1],[0.3,0.5,0.7,1],[0.4,0.6,0.8,1]],"uv":[[[0.1,0.2],[0.3,0.4],[0.5,0.6]],[[0.7,0.8],[0.8,0.9],[0.9,1]]],"submeshes":[{"topology":"Triangles","indices":[0,1,2]}]})");
    if(!FFileHelper::SaveStringToFile(Json,*File)){return nullptr;}
    const FString Asset=TEXT("/Game/Task0170Tests/M_")+FGuid::NewGuid().ToString(EGuidFormats::Digits);
    return ULinkAssetTools::BuildSourceMesh(File,Asset,Matrix,Collision);
}
}
using namespace Task0170;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTask0170Coordinates,"Task0172.Headless.Scene.Coordinates",Flags)
bool FTask0170Coordinates::RunTest(const FString&)
{
    TestTrue(TEXT("Units and cyclic axes"),LinkSceneCoordinates::Position(FVector(1,-2,3)).Equals(FVector(300,100,-200),1e-9));
    TestTrue(TEXT("Directions are not centimetre-scaled"),LinkSceneCoordinates::Direction(FVector(1,-2,3)).Equals(FVector(3,1,-2),1e-9));
    const TArray<double> A={-2,.3,0,7,0,3,.7,-4,.2,0,4,9,0,0,0,1};FMatrix M;
    if(!TestTrue(TEXT("Finite affine shear accepted"),LinkSceneCoordinates::Matrix(A,M))){return false;}
    const FVector P(.7,-1,3),Unity(-2*P.X+.3*P.Y+7,3*P.Y+.7*P.Z-4,.2*P.X+4*P.Z+9);
    TestTrue(TEXT("Full affine transform, not lossy TRS"),FVector(M.TransformPosition(Cm(P))).Equals(Cm(Unity),1e-7));
    TestTrue(TEXT("Reflection is explicit"),LinkSceneCoordinates::ReversesBakedWinding(M));
    TestFalse(TEXT("Permutation is not reflection"),LinkSceneCoordinates::ReversesBakedWinding(FMatrix::Identity));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTask0170Invalid,"Task0172.Headless.Scene.InvalidAffine",Flags)
bool FTask0170Invalid::RunTest(const FString&)
{
    FMatrix Out;TestFalse(TEXT("Wrong size"),LinkSceneCoordinates::Matrix(TArray<double>{1,2},Out));
    auto A=Identity();A[12]=.1;TestFalse(TEXT("Perspective rejected"),LinkSceneCoordinates::Matrix(A,Out));
    A=Identity();A[0]=0;TestNull(TEXT("Singular mesh transform rejected"),Build(A));
    TestFalse(TEXT("Absent actor rejected"),ULinkAssetTools::ApplySourceBoxTransform(nullptr,Identity()));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTask0170Mesh,"Task0172.Headless.Scene.MeshAttributes",Flags)
bool FTask0170Mesh::RunTest(const FString&)
{
    auto A=Identity();A[0]=2;A[2]=.5;A[5]=3;A[10]=4;A[3]=99;A[7]=-88;A[11]=77;
    auto* Mesh=Build(A);if(!TestNotNull(TEXT("Fresh mesh generated"),Mesh)){return false;}
    auto* D=Mesh->GetMeshDescription(0);if(!TestNotNull(TEXT("Real native geometry"),D)){return false;}
    FStaticMeshConstAttributes Attr(*D);TestEqual(TEXT("Vertex count"),D->Vertices().Num(),3);TestEqual(TEXT("Triangle count"),D->Triangles().Num(),1);
    TestEqual(TEXT("UV channels"),Attr.GetVertexInstanceUVs().GetNumChannels(),2);
    TestEqual(TEXT("Material slots"),Mesh->GetStaticMaterials().Num(),1);
    const FVector Expected[]={FVector(800,300,0),FVector(800,700,0),FVector(2000,450,0)};
    int32 I=0;for(auto ID:D->Vertices().GetElementIDs()){TestTrue(TEXT("Baked linear geometry excludes actor pivot"),FVector(Attr.GetVertexPositions()[ID]).Equals(Expected[I++],.001));}
    for(auto ID:D->VertexInstances().GetElementIDs())
    {
        const int32 Vertex=D->GetVertexInstanceVertex(ID).GetValue();
        TestTrue(TEXT("Source normals preserved"),FVector(Attr.GetVertexInstanceNormals()[ID]).Equals(FVector(0,0,1),.00001));
        TestTrue(TEXT("UV0 not vertically flipped"),FMath::IsNearlyEqual(Attr.GetVertexInstanceUVs().Get(ID,0).Y,float(.2+Vertex*.2),.00001f));
        TestTrue(TEXT("UV1 retained"),FMath::IsNearlyEqual(Attr.GetVertexInstanceUVs().Get(ID,1).X,float(.7+Vertex*.1),.00001f));
        TestTrue(TEXT("Vertex paint retained"),FMath::IsNearlyEqual(Attr.GetVertexInstanceColors()[ID].X,float(.2+Vertex*.1),.00001f));
        TestEqual(TEXT("Tangent handedness retained"),Attr.GetVertexInstanceBinormalSigns()[ID],-1.f);
    }
    TestNotNull(TEXT("Collision body exists"),Mesh->GetBodySetup());
    if(Mesh->GetBodySetup()){TestTrue(TEXT("Mesh collision uses geometry"),Mesh->GetBodySetup()->CollisionTraceFlag==CTF_UseComplexAsSimple);TestTrue(TEXT("Cooked collision exists"),Mesh->GetBodySetup()->TriMeshGeometries.Num()>0);}return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTask0170Winding,"Task0172.Headless.Scene.ReflectionWinding",Flags)
bool FTask0170Winding::RunTest(const FString&)
{
    for(double Sign:{1.,-1.})
    {
        auto A=Identity();A[0]=Sign;A[4]=.5;auto* Mesh=Build(A);
        if(!TestNotNull(TEXT("Mirrored/sheared fresh geometry"),Mesh)){continue;}
        auto* D=Mesh->GetMeshDescription(0);if(!TestNotNull(TEXT("Geometry description"),D)){continue;}
        FStaticMeshConstAttributes Attr(*D);const auto IDs=D->GetTriangleVertexInstances(*D->Triangles().GetElementIDs().begin());
        FVector P[3];for(int32 I=0;I<3;++I){P[I]=FVector(Attr.GetVertexPositions()[D->GetVertexInstanceVertex(IDs[I])]);}
        // This test fixture has a +Y normal but the authored triangle cross is -Y;
        // inspect the reference-relative ordering explicitly, independently of normals.
        TestTrue(TEXT("Mesh-format plus reflection ordering"),FVector::CrossProduct(P[1]-P[0],P[2]-P[0]).Z>0);
        const FVector ExpectedNormal=FVector(0,-.5/Sign,1).GetSafeNormal();
        // Shear y by x tilts the surface; applying the forward matrix to normals fails.
        for(auto ID:D->VertexInstances().GetElementIDs())
        {
            TestTrue(TEXT("Inverse-transpose normal remains perpendicular"),FVector(Attr.GetVertexInstanceNormals()[ID]).Equals(ExpectedNormal,1e-5));
            TestEqual(TEXT("Mirroring flips tangent sign once"),Attr.GetVertexInstanceBinormalSigns()[ID],float(-Sign));
        }
    }return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTask0170Box,"Task0172.Headless.Scene.BoxPlacement",Flags)
bool FTask0170Box::RunTest(const FString&)
{
    auto* World=FAutomationEditorCommonUtils::CreateNewMap();auto* Actor=World?World->SpawnActor<AStaticMeshActor>():nullptr;
    if(!TestNotNull(TEXT("Box fixture"),Actor)){return false;}
    Actor->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
    auto A=Identity();A[0]=2;A[5]=3;A[10]=4;A[3]=5;A[7]=6;A[11]=7;
    TestTrue(TEXT("Box import"),ULinkAssetTools::ApplySourceBoxTransform(Actor,A));
    TestTrue(TEXT("Authored box centre"),Actor->GetActorLocation().Equals(FVector(700,500,600),.001));
    FVector Centre,Extent;Actor->GetActorBounds(false,Centre,Extent);TestTrue(TEXT("Authored dimensions"),Extent.Equals(FVector(200,100,150),.001));
    A[1]=.7;TestFalse(TEXT("Sheared box cannot silently lose affine information"),ULinkAssetTools::ApplySourceBoxTransform(Actor,A));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTask0170Navigation,"Task0172.Headless.Scene.NavigationPolicy",Flags)
bool FTask0170Navigation::RunTest(const FString&)
{
    auto* World=FAutomationEditorCommonUtils::CreateNewMap();auto* Actor=World?World->SpawnActor<AStaticMeshActor>():nullptr;
    if(!TestNotNull(TEXT("Navigation fixture"),Actor)){return false;}
    ULinkAssetTools::SetSourceNavigation(Actor,false,false);TestFalse(TEXT("Composition excluded from nav"),Actor->GetStaticMeshComponent()->CanEverAffectNavigation());
    ULinkAssetTools::SetSourceNavigation(Actor,true,true);TestTrue(TEXT("Obstacle contributes"),Actor->GetStaticMeshComponent()->CanEverAffectNavigation());
    auto* Modifier=Actor->FindComponentByClass<UNavModifierComponent>();if(TestNotNull(TEXT("Navigation exclusion exists"),Modifier)){TestTrue(TEXT("Exclusion uses nonwalkable area"),Modifier->AreaClass==UNavArea_Null::StaticClass());}return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTask0170Chain,"Task0172.Headless.Scene.AuthoredChain",Flags)
bool FTask0170Chain::RunTest(const FString&)
{
    TArray<FVector> Centres;for(int32 I=0;I<82;++I){Centres.Emplace(5+10*I,0,200);}
    FLinkSeededChain Chain;TestFalse(TEXT("Incomplete curve rejected"),Chain.Initialize(FVector::ZeroVector,FVector::ZeroVector,{}));
    if(!TestTrue(TEXT("Authored curve initialized"),Chain.Initialize(FVector(0,0,200),FVector(820,0,200),Centres))){return false;}
    if(!TestEqual(TEXT("Two cuff endpoints plus82 centres"),Chain.Points.Num(),84)){return false;}
    TestEqual(TEXT("83constraints"),Chain.Lengths.Num(),83);double Sum=0;for(double Length:Chain.Lengths){Sum+=Length;}TestEqual(TEXT("Rest length"),Sum,820.);
    TestTrue(TEXT("Authored input order"),Chain.Points[17].Equals(Centres[16],1e-9));
    Chain.Integrate(.005,FVector(0,0,-1000),FVector(0,0,200),FVector(820,0,200));
    TestTrue(TEXT("Gravity integrates real interior point"),Chain.Points[40].Z<200);Chain.Constrain(20);
    TestTrue(TEXT("Start fixed"),Chain.Points[0].Equals(FVector(0,0,200),1e-9));TestTrue(TEXT("End fixed"),Chain.Points.Last().Equals(FVector(820,0,200),1e-9));
    double MaxError=0;for(int32 I=0;I<Chain.Lengths.Num();++I){MaxError=FMath::Max(MaxError,FMath::Abs(FVector::Distance(Chain.Points[I],Chain.Points[I+1])-Chain.Lengths[I]));}
    TestTrue(TEXT("Constraint converges"),MaxError<.1);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTask0170Layout,"Task0172.Headless.Scene.Layout",Flags)
bool FTask0170Layout::RunTest(const FString&)
{
    auto* World=FAutomationEditorCommonUtils::CreateNewMap();auto* Layout=World?World->SpawnActor<ALinkSceneLayout>():nullptr;
    if(!TestNotNull(TEXT("Layout actor"),Layout)){return false;}
    TestEqual(TEXT("Discover real layout"),ALinkSceneLayout::Find(World),Layout);
    FTransform Left,Right;TestFalse(TEXT("Invalid identity"),Layout->GetPlayerStart(-1,Left));
    if(TestTrue(TEXT("Both authored starts"),Layout->GetPlayerStart(0,Left)&&Layout->GetPlayerStart(1,Right)))
    {
        TestTrue(TEXT("LT authored pose"),Left.GetLocation().Equals(FVector(-4251.7334,-1448.87314,61.81716),.001));
        TestTrue(TEXT("RT authored pose"),Right.GetLocation().Equals(FVector(-4191.25328,-1690.10315,69.55112),.001));
    }
    TestEqual(TEXT("Full authored centreline"),Layout->GetChainCentres().Num(),82);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTask0170NativeChain,"Task0172.Headless.Scene.NativeAuthoredChain",Flags)
bool FTask0170NativeChain::RunTest(const FString&)
{
    auto* World=FAutomationEditorCommonUtils::CreateNewMap();if(!TestNotNull(TEXT("Native chain world"),World)){return false;}
    auto* Left=World->SpawnActor<ALinkCharacter>(FVector(0,0,200),FRotator::ZeroRotator);
    auto* Right=World->SpawnActor<ALinkCharacter>(FVector(820,0,200),FRotator::ZeroRotator);
    auto* Chain=World->SpawnActor<ALinkChain>();if(!TestTrue(TEXT("Native actors"),Left&&Right&&Chain)){return false;}
    Chain->Initialize(Left,Right);TArray<FVector> Centres;for(int32 I=0;I<82;++I){Centres.Emplace(5+10*I,0,200);}
    if(!TestTrue(TEXT("Native authored branch accepts curve"),Chain->InitializeAuthoredCurve(Centres))){return false;}
    TArray<FVector> Points;Chain->GetPoints(Points);TestEqual(TEXT("Native84points"),Points.Num(),84);
    auto* Tube=Chain->FindComponentByClass<UProceduralMeshComponent>();if(!TestNotNull(TEXT("Procedural tube"),Tube)){return false;}
    auto* Section=Tube->GetProcMeshSection(0);if(!TestNotNull(TEXT("Fresh tube section"),Section)){return false;}
    TestEqual(TEXT("Six vertices per ring"),Section->ProcVertexBuffer.Num(),84*6);
    TestEqual(TEXT("Connected two-triangle quads"),Section->ProcIndexBuffer.Num(),83*6*6);
    Chain->Tick(.01f);Chain->GetPoints(Points);TestEqual(TEXT("Tick keeps authored topology"),Points.Num(),84);
    if(Points.Num()==84){TestTrue(TEXT("Physical fixed-step branch updates interior"),!Points[40].Equals(Centres[39],1e-5));}return true;
}

class FTask0170WorldCheck : public IAutomationLatentCommand
{
public:
    FTask0170WorldCheck(FAutomationTestBase* InTest,bool InComposition):Test(InTest),bComposition(InComposition){}
    bool Update() override
    {
        const double Now=FPlatformTime::Seconds();if(Start<0){Start=Now;}
        if(Now-Start>90){Test->AddError(TEXT("Fresh-world setup/navigation timeout: INVALID_VERIFICATION"));return true;}
        UWorld* World=GEditor?GEditor->PlayWorld:nullptr;if(!World||World->GetTimeSeconds()<2){return false;}
        auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);if(!Nav||Nav->IsNavigationBuildInProgress()){return false;}
        if(bComposition){return Composition(World);}
        auto Oracle=Read(FPaths::ProjectContentDir()/TEXT("Data/Tests/EntryTerrain.json"));if(!Oracle){Test->AddError(TEXT("Missing evaluator terrain fixture: INVALID_VERIFICATION"));return true;}
        FCollisionQueryParams Params(SCENE_QUERY_STAT(Task0170Ground),true);
        for(TActorIterator<AActor> It(World);It;++It){if(!It->ActorHasTag(TEXT("SourceGround"))){Params.AddIgnoredActor(*It);}}
        int32 Rays=0,NavCases=0;
        for(const auto& Value:Oracle->GetArrayField(TEXT("samples")))
        {
            auto E=Value->AsObject();const FVector Ray=Cm(V(E->TryGetField(TEXT("input"))));FHitResult Hit;
            bool Ground=World->LineTraceSingleByChannel(Hit,Ray,Ray-FVector(0,0,3000),ECC_Visibility,Params);
            if(E->GetBoolField(TEXT("ground")))
            {
                ++Rays;Test->TestTrue(TEXT("Real fresh mesh collision ray"),Ground);
                if(Ground){Test->TestTrue(TEXT("Surface agrees with capture within1mm"),Hit.ImpactPoint.Equals(Cm(V(E->TryGetField(TEXT("point")))),.1));const FVector N=Cm(V(E->TryGetField(TEXT("normal")))).GetSafeNormal();Test->TestTrue(TEXT("Surface normal agrees within0.3degrees"),FVector::DotProduct(N,Hit.ImpactNormal)>FMath::Cos(FMath::DegreesToRadians(.3)));}
            }
            if(E->GetBoolField(TEXT("stableNavigationCase")))
            {
                ++NavCases;FNavLocation P;FVector Query=E->GetBoolField(TEXT("ground"))?Cm(V(E->TryGetField(TEXT("point")))):Ray-FVector(0,0,1000);
                Test->TestEqual(TEXT("Captured navigation footprint"),Nav->ProjectPointToNavigation(Query,P,FVector(15,15,200)),E->GetBoolField(TEXT("navigable")));
            }
        }
        Test->TestTrue(TEXT("Nonvacuous physical fixtures"),Rays>100);Test->TestEqual(TEXT("Stable boundary-filtered navigation cases"),NavCases,184);
        auto* Controller=Cast<ALinkPlayerController>(World->GetFirstPlayerController());
        if(!Controller||!Controller->GetCharacterAt(0)||!Controller->GetCharacterAt(1)){Test->AddError(TEXT("Generated scene has no playable pair"));return true;}
        for(int32 I=0;I<2;++I)
        {
            auto Player=Oracle->GetArrayField(TEXT("players"))[I]->AsObject();auto* Crew=Controller->GetCharacterAt(I);const FVector Expected=Cm(V(Player->TryGetField(TEXT("position"))));
            Test->TestTrue(TEXT("New-game authored horizontal spawn"),FVector::Dist2D(Crew->GetActorLocation(),Expected)<1);
            Test->TestTrue(TEXT("Source-sized capsule settles within3cm"),FMath::Abs(Crew->GetActorLocation().Z-Expected.Z)<3);
        }
        int32 Paths=0;for(const auto& Value:Oracle->GetArrayField(TEXT("waypoints")))
        {
            auto Waypoint=Value->AsObject();auto* Crew=Controller->GetCharacterAt(0);auto* Path=UNavigationSystemV1::FindPathToLocationSynchronously(World,Crew->GetNavAgentLocation(),Cm(V(Waypoint->TryGetField(TEXT("position")))),Crew);
            ++Paths;Test->TestTrue(TEXT("Complete source approach path"),Path&&Path->IsValid()&&!Path->IsPartial());
        }Test->TestEqual(TEXT("All authored approaches"),Paths,8);
        return true;
    }
    bool Composition(UWorld* World)
    {
        auto Expected=Read(FPaths::ProjectContentDir()/TEXT("Data/Tests/EntryComposition.json"));
        if(!Expected){Test->AddError(TEXT("Missing evaluator composition fixture: INVALID_VERIFICATION"));return true;}
        auto* Session=World->GetGameInstance()?World->GetGameInstance()->GetSubsystem<ULinkSession>():nullptr;
        if(!Session){Test->AddError(TEXT("Missing session fixture: INVALID_VERIFICATION"));return true;}
        TMap<FString,TArray<AStaticMeshActor*>> Actors;
        for(TActorIterator<AStaticMeshActor> It(World);It;++It){for(FName Tag:It->Tags){FString ID=Tag.ToString();if(ID.RemoveFromStart(TEXT("SourceId."))){Actors.FindOrAdd(ID).Add(*It);}}}
        if(Phase==0)
        {
            for(const auto& Value:Expected->GetArrayField(TEXT("environment")))
            {
                auto E=Value->AsObject();auto* Match=Actors.Find(E->GetStringField(TEXT("sourceId")));
                if(!Match||Match->Num()!=1){Test->AddError(TEXT("Missing or duplicate source renderer"));continue;}
                auto* Component=(*Match)[0]->GetStaticMeshComponent();UStaticMesh* Mesh=Component->GetStaticMesh();if(!Mesh){Test->AddError(TEXT("Missing fresh mesh"));continue;}
                auto Bounds=E->GetObjectField(TEXT("bounds"));const FBox Actual=Mesh->GetBoundingBox().TransformBy(Component->GetComponentTransform());
                Test->TestTrue(TEXT("World geometry centre"),Actual.GetCenter().Equals(Cm(V(Bounds->TryGetField(TEXT("center")))),.1));
                Test->TestTrue(TEXT("World geometry size"),Actual.GetSize().Equals(Cm(V(Bounds->TryGetField(TEXT("size")))),.1));
            }
            int32 Count=0;for(const auto& Value:Expected->GetArrayField(TEXT("additions")))
            {
                auto E=Value->AsObject();auto* Match=Actors.Find(E->GetStringField(TEXT("sourceId")));if(!Match||Match->Num()!=1){Test->AddError(TEXT("Missing composition part"));continue;}++Count;
                auto* Actor=(*Match)[0];auto* Component=Actor->GetStaticMeshComponent();
                Test->TestEqual(TEXT("Visual-only collision remains disabled after reload"),Component->GetCollisionEnabled(),ECollisionEnabled::NoCollision);
                Test->TestFalse(TEXT("Visual-only actors do not affect navigation"),Component->CanEverAffectNavigation());
                Test->TestEqual(TEXT("Initial visibility"),!Actor->IsHidden(),E->GetBoolField(TEXT("initiallyVisible")));
            }Test->TestEqual(TEXT("Nonempty complete composition"),Count,73);
            Session->GetInteractionFacts(TEXT("EntryGate.Kiosk"),1389396).Add(1389396,0);Session->GetInteractionFacts(TEXT("EntryGate.Rock"),1389410).Add(1389410,0);Session->GetInteractionFacts(TEXT("EntryGate.Gate"),0).Add(1389396,1);
            Phase=1;ChangedAt=World->GetTimeSeconds();return false;
        }
        if(World->GetTimeSeconds()-ChangedAt<.2){return false;}
        int32 Displays=0;for(TActorIterator<ALinkSourceItemDisplay> It(World);It;++It){++Displays;Test->TestEqual(TEXT("Item facts drive display state"),!It->IsHidden(),Phase==1?It->InteractionId==TEXT("EntryGate.Gate"):It->InteractionId!=TEXT("EntryGate.Gate"));}
        Test->TestEqual(TEXT("All nine display parts exercised"),Displays,9);
        if(Phase==1){Session->GetInteractionFacts(TEXT("EntryGate.Kiosk"),1389396).Add(1389396,1);Session->GetInteractionFacts(TEXT("EntryGate.Rock"),1389410).Add(1389410,1);Session->GetInteractionFacts(TEXT("EntryGate.Gate"),0).Add(1389396,0);Phase=2;ChangedAt=World->GetTimeSeconds();return false;}return true;
    }
private:FAutomationTestBase* Test;bool bComposition;double Start=-1,ChangedAt=0;int32 Phase=0;
};
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTask0170World,"Task0172.Headless.Scene.GeneratedWorldPhysicsNavigation",Flags)
bool FTask0170World::RunTest(const FString&)
{
    if(!FPaths::FileExists(FPaths::ProjectContentDir()/TEXT("Task0170/EntryGate_Source.umap"))){AddError(TEXT("Submitted builder did not produce fresh task map"));return false;}
    ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Task0170/EntryGate_Source")));
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));ADD_LATENT_AUTOMATION_COMMAND(FTask0170WorldCheck(this,false));ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTask0170Composition,"Task0172.Headless.Scene.GeneratedComposition",Flags)
bool FTask0170Composition::RunTest(const FString&)
{
    if(!FPaths::FileExists(FPaths::ProjectContentDir()/TEXT("Task0170/EntryGate_Source.umap"))){AddError(TEXT("Submitted builder did not produce fresh task map"));return false;}
    ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Task0170/EntryGate_Source")));
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));ADD_LATENT_AUTOMATION_COMMAND(FTask0170WorldCheck(this,true));ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());return true;
}
#endif
