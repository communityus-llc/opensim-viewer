// Fill out your copyright notice in the Description page of Project Settings.

#include "AvinationViewer.h"
#include "AvinationViewerGameMode.h"

#if PLATFORM_WINDOWS
#include "AllowWindowsPlatformTypes.h"
#include <io.h>
#include "HideWindowsPlatformTypes.h"
#else
#include <unistd.h>
#endif

#include <sys/stat.h>
#include <stdlib.h>
#include <fcntl.h>
#include "MeshActor.h"
#include "AssetCache.h"
#include "AssetDecode.h"
#include "TextureCache.h"
#include "AvnCharacter.h"

class ObjectCreator : public FRunnable
{
public:
    ObjectCreator(AAvinationViewerGameMode *mode);
    ~ObjectCreator();
    
    virtual bool Init() override;
    uint32_t Run();
    virtual void Stop() override;
    
    void TickPool();
    
private:
    FRunnableThread *thread;
    bool runThis = true;
    AAvinationViewerGameMode *mode;
    
    FCriticalSection poolLock;
    TArray<AMeshActor *> pool;
    
    FCriticalSection readyLock;
    TArray<AMeshActor *> ready;
    
    FCriticalSection textureLock;
    TArray<AMeshActor *> textures;
    
    void ObjectReady(AMeshActor *act);
    void GotTexture(FGuid id, UTexture2D *tex, AMeshActor *act);
};

AAvinationViewerGameMode::AAvinationViewerGameMode(const class FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    DefaultPawnClass = AAvnCharacter::StaticClass();
}

void AAvinationViewerGameMode::HandleMatchHasStarted()
{
    Super::HandleMatchHasStarted();
    //delete &TextureCache::Get();
    //TextureCache::outer = this;

    
    if (creator)
        delete creator;
    creator = new ObjectCreator(this);
    
    /*
    FGuid id;
    //FGuid::Parse(TEXT("77540e8e-5064-4cf9-aa77-ad6617d73508"), id);
    FGuid::Parse(TEXT("8e3377b1-ccc7-4f7b-981d-f30ccae8121d"), id);
    
    OnAssetFetched d;
    d.BindUObject(this, &AAvinationViewerGameMode::CreateNewActor);
    AssetCache::Get().Fetch(id, d);
    */
}

AMeshActor *AAvinationViewerGameMode::CreateNewActor(rapidxml::xml_node<> *data)
{
    ObjectReadyDelegate d;
    d.BindUObject(this, &AAvinationViewerGameMode::HandleObjectReady);
    
    return CreateNewActor(data, d);
}

void AAvinationViewerGameMode::HandleObjectReady(AMeshActor *act)
{
    FVector pos(200.0f, 0.0f, 270.0f);
    
    act->SetActorLocationAndRotation(pos /* act->sog->GetRootPart()->groupPosition * 100 */, act->sog->GetRootPart()->rotation);
    
    act->RegisterComponents();
    act->SetActorHiddenInGame(false);
    act->sog->GatherTextures();
    
    UE_LOG(LogTemp, Warning, TEXT("%d textures on object"), act->sog->groupTextures.Num());
}

AMeshActor *AAvinationViewerGameMode::CreateNewActor(rapidxml::xml_node<> *data, ObjectReadyDelegate d, AMeshActor *act)
{
    rapidxml::xml_node<> *sopNode = data->first_node();
    if (FString(sopNode->name()) == TEXT("RootPart"))
        sopNode = sopNode->first_node();
    rapidxml::xml_node<> *uuidNode = sopNode->first_node("UUID");
    rapidxml::xml_node<> *inner = uuidNode->first_node();
    
    FGuid id;
    if (!FGuid::Parse(FString(inner->value()), id))
        return 0;
    
    if (!act)
        act = GetWorld()->SpawnActor<AMeshActor>(AMeshActor::StaticClass());
    
    act->OnObjectReady = d;
    
    actors.Add(id, act);
    
    if (!act->Load(data))
        act->Destroy();
    
    return act;
}

void AAvinationViewerGameMode::CreateNewActor(FGuid id, TArray<uint8_t> data)
{
    AssetDecode adec(data);
    TArray<uint8_t> xml;
    xml = adec.AsBase64DecodeArray();
    xml.Add(0);
    
    rapidxml::xml_document<> doc;
    
    try
    {
        doc.parse<0>((char *)xml.GetData());
    }
//    catch (std::exception& e)
    catch (...)
    {
        return;
    }
    
    rapidxml::xml_node<> *groupNode = doc.first_node();
    if (!groupNode || FString(groupNode->name()) != TEXT("SceneObjectGroup"))
        return;

    AMeshActor *act = CreateNewActor(groupNode);
}

void AAvinationViewerGameMode::Tick(float deltaSeconds)
{
    //static int gap = 2;
    Super::Tick(deltaSeconds);
    
    //TextureCache::Get().DispatchDecodedRequest();
    creator->TickPool();
    
    /*
    if (!(--gap))
    {
        gap = 2;
        if (queue.Num())
        {
            CreateNewActor(queue[0]);
            queue.RemoveAt(0);
        }
    }
    */
}

ObjectCreator::ObjectCreator(AAvinationViewerGameMode *m)
{
    mode = m;
    
    thread = FRunnableThread::Create(this, TEXT("ObjectCreator"), 0, TPri_BelowNormal);
}

ObjectCreator::~ObjectCreator()
{
    runThis = false;
    //thread->Kill();
    thread->WaitForCompletion();
}

/*
void ObjectCreator::Enqueue(rapidxml::xml_node<> *sog)
{
    queueLock.Lock();
    queue.Add(sog);
    queueLock.Unlock();
}
*/

/*
void ObjectCreator::Enqueue(TArray<uint8_t> sog)
{
    queueLock.Lock();
    queue.Add(sog);
    queueLock.Unlock();
}
*/

bool ObjectCreator::Init()
{
    return true;
}

uint32_t ObjectCreator::Run()
{
    struct stat st;
    if (stat("/Users/melanie/UnrealViewerData/primsback.xml", &st))
//    if (stat("/Avination/UnRealViewer/primsback.xml", &st) < 0)
        return 0;
    int fd = open("/Users/melanie/UnrealViewerData/primsback.xml", O_RDONLY);
//    int fd = open("/Avination/UnRealViewer/primsback.xml", O_RDONLY);
    
    if (fd < 0 || st.st_size == 0)
        return 0;
       
    uint8_t *fileData = new uint8_t[st.st_size + 1];
    read(fd, fileData, st.st_size);
    fileData[st.st_size] = 0;
    close(fd);
    
    rapidxml::xml_document<> doc;
    
    doc.parse<0>((char *)fileData);
    
    rapidxml::xml_node<> *rootNode = doc.first_node();
    
    rapidxml::xml_node<> *sog = rootNode->first_node();

    while (runThis)
    {
        usleep(50);
       
        if (sog)
        {
//            AMeshActor *act = mode->GetWorld()->SpawnActor<AMeshActor>(AMeshActor::StaticClass());
        
            ObjectReadyDelegate d;
            d.BindRaw(this,&ObjectCreator::ObjectReady);
            
//            mode->CreateNewActor(sog, d, act);
            mode->CreateNewActor(sog, d);
            sog = sog->next_sibling();
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("All actors created"));
            poolLock.Unlock();
            break;
        }
    }
    
    delete fileData;
    
    return 0;
}

void ObjectCreator::ObjectReady(AMeshActor *act)
{
    act->sog->GatherTextures();
    
    for (auto it = act->sog->groupTextures.CreateConstIterator() ; it ; ++it)
    {
        TextureFetchedDelegate d;
        d.BindRaw(this, &ObjectCreator::GotTexture, act);
        TextureCache::Get().Fetch((*it), d);
    }
    readyLock.Lock();
    ready.Add(act);
    readyLock.Unlock();
}

void ObjectCreator::Stop()
{
    runThis = false;
}

// MUST BE CALLED ON GAME THREAD
void ObjectCreator::TickPool()
{
    readyLock.Lock();
    if (ready.Num())
    {
        AMeshActor *act = ready[0];
        ready.RemoveAt(0);
        readyLock.Unlock();
        act->DoBeginPlay(); // hack needs review
        act->SetActorLocationAndRotation(act->sog->GetRootPart()->groupPosition * 100, act->sog->GetRootPart()->rotation);
        act->RegisterComponents();
        act->SetActorHiddenInGame(false);
    }
    else
    {
        readyLock.Unlock();
    }
}

void ObjectCreator::GotTexture(FGuid id, UTexture2D *tex, AMeshActor *act)
{
    
}