#include <RendererCore/RendererCorePCH.h>

#include <Core/GameApplication/GameApplicationBase.h>
#include <Core/Graphics/Geometry.h>
#include <Core/World/World.h>
#include <Foundation/Configuration/CVar.h>
#include <Foundation/Configuration/Startup.h>
#include <RendererCore/Decals/DecalAtlasResource.h>
#include <RendererCore/Decals/Implementation/DecalManager.h>
#include <RendererCore/Material/MaterialResource.h>
#include <RendererCore/Pipeline/View.h>
#include <RendererCore/RenderContext/RenderContext.h>
#include <RendererCore/RenderWorld/RenderWorld.h>
#include <RendererCore/Textures/DynamicTextureAtlas.h>
#include <RendererCore/Utils/CoreRenderProfile.h>
#include <RendererFoundation/Resources/DynamicBuffer.h>

#include <Shaders/Common/LightData.h>

// clang-format off
EZ_BEGIN_SUBSYSTEM_DECLARATION(RendererCore, DecalManager)
  BEGIN_SUBSYSTEM_DEPENDENCIES
    "Foundation",
    "Core",
    "RenderWorld"
  END_SUBSYSTEM_DEPENDENCIES

  ON_HIGHLEVELSYSTEMS_STARTUP
  {
    ezDecalManager::OnEngineStartup();
  }

  ON_HIGHLEVELSYSTEMS_SHUTDOWN
  {
    ezDecalManager::OnEngineShutdown();
  }
EZ_END_SUBSYSTEM_DECLARATION;
// clang-format on

#if EZ_ENABLED(EZ_COMPILE_FOR_DEVELOPMENT)
ezCVarBool cvar_RenderingDecalsShowAtlasTexture("Rendering.Decals.ShowAtlasTexture", false, ezCVarFlags::Default, "Display the dynamic decal atlas texture");
#endif

/// NOTE: The default values for these are defined in ezCoreRenderProfileConfig
///       but they can also be overwritten in custom game states at startup.
EZ_RENDERERCORE_DLL ezCVarInt cvar_RenderingDecalsDynamicAtlasSize("Rendering.Decals.DynamicAtlasSize", 3072, ezCVarFlags::RequiresDelayedSync, "The size of the dynamic decal atlas texture.");

constexpr ezUInt32 s_uiMinDecalSize = 16;
constexpr ezUInt32 s_uiMaxDecalSize = 1024;

static ezUInt32 s_uiLastConfigModification = 0;

//////////////////////////////////////////////////////////////////////////

ezPerDecalAtlasData MakeAtlasData(const ezRectU16& rect, const ezVec2& vTextureSize)
{
  ezVec2 scale, offset;
  scale.x = (float)rect.width / vTextureSize.x * 0.5f;
  scale.y = (float)rect.height / vTextureSize.y * 0.5f;
  offset.x = (float)rect.x / vTextureSize.x + scale.x;
  offset.y = (float)rect.y / vTextureSize.y + scale.y;

  ezPerDecalAtlasData res;
  res.scale = ezShaderUtils::Float2ToRG16F(scale);
  res.offset = ezShaderUtils::Float2ToRG16F(offset);
  return res;
}

struct DecalInfo
{
  ezUInt8 m_uiGeneration = 1;
  ezUInt32 m_uiAtlasDataOffset = ezInvalidIndex; // Also used as index into s_pData->m_DecalInfos
  ezDynamicTextureAtlas::AllocationId m_atlasAllocationId;

  float m_fMaxScreenSpaceSize = 0.0f;

  ezTexture2DResourceHandle m_hTexture;
  ezMaterialResourceHandle m_hMaterial;
  ezUInt16 m_uiMaxWidth = 0;
  ezUInt16 m_uiMaxHeight = 0;
  float m_fUpdateInterval = ezTime::MakeFromHours(3600).AsFloatInSeconds();

  ezTime m_AutoRemoveTime;
  ezTime m_NextUpdateTime;
  ezTime m_WorldTime;

  float CalculateScore(ezTime now) const
  {
    const float fClampedScreenSpaceSize = ezMath::Clamp(m_fMaxScreenSpaceSize, 0.0f, 10.0f);
    const float fTimeSinceLastUpdate = (now - m_NextUpdateTime).AsFloatInSeconds();

    return fClampedScreenSpaceSize + (fTimeSinceLastUpdate * fTimeSinceLastUpdate);
  }

  ezVec2U32 CalculateScaledSize() const
  {
    const float fScreenSpaceSize = ezMath::Clamp(ezMath::Pow(m_fMaxScreenSpaceSize, 1.5f), 0.01f, 1.0f);
    const ezUInt32 uiWidth = ezMath::Clamp(static_cast<ezUInt32>(m_uiMaxWidth * fScreenSpaceSize), s_uiMinDecalSize, s_uiMaxDecalSize);
    const ezUInt32 uiHeight = ezMath::Clamp(static_cast<ezUInt32>(m_uiMaxHeight * fScreenSpaceSize), s_uiMinDecalSize, s_uiMaxDecalSize);

    const ezUInt32 uiWidthAlign = uiWidth < 32 ? 16 : 32;
    const ezUInt32 uiHeightAlign = uiHeight < 32 ? 16 : 32;
    return ezVec2U32(ezMemoryUtils::AlignSize(uiWidth, uiWidthAlign), ezMemoryUtils::AlignSize(uiHeight, uiHeightAlign));
  }

  ezUInt64 GetKey() const
  {
    if (m_hTexture.IsValid())
      return GetKey(m_hTexture);

    if (m_hMaterial.IsValid())
      return GetKey(m_hMaterial);

    EZ_REPORT_FAILURE("Invalid decal");
    return 0;
  }

  EZ_FORCE_INLINE void Update(float fScreenSpaceSize, const ezView* pReferenceView, ezTime inactiveTimeBeforeAutoRemove)
  {
    m_fMaxScreenSpaceSize = ezMath::Max(m_fMaxScreenSpaceSize, fScreenSpaceSize);
    m_AutoRemoveTime = ezTime::Now() + inactiveTimeBeforeAutoRemove;

    if (pReferenceView->GetCameraUsageHint() == ezCameraUsageHint::MainView || pReferenceView->GetCameraUsageHint() == ezCameraUsageHint::EditorView || m_WorldTime.IsZero())
    {
      if (const ezWorld* pWorld = pReferenceView->GetWorld())
      {
        m_WorldTime = pWorld->GetClock().GetAccumulatedTime();
      }
    }
  }

  EZ_ALWAYS_INLINE static ezUInt64 GetKey(ezTexture2DResourceHandle hTexture)
  {
    return hTexture.GetResourceIDHash();
  }

  EZ_ALWAYS_INLINE static ezUInt64 GetKey(ezMaterialResourceHandle hMaterial)
  {
    return hMaterial.GetResourceIDHash();
  }

  EZ_ALWAYS_INLINE static ezStringView GetNameFromResource(const ezResource& resource)
  {
    if (resource.GetResourceDescription().IsEmpty() == false)
    {
      return resource.GetResourceDescription().GetFileName();
    }
    else
    {
      return resource.GetResourceID();
    }
  }
};

struct SortedDecal
{
  EZ_DECLARE_POD_TYPE();

  ezUInt32 m_uiIndex;
  float m_fScore;

  bool operator<(const SortedDecal& other) const
  {
    if (m_fScore != other.m_fScore)
      return m_fScore > other.m_fScore; // Descending order

    return m_uiIndex < other.m_uiIndex;
  }
};

struct DecalUpdateInfo
{
  ezMaterialResourceHandle m_hMaterial;
  ezRectU16 m_TargetRect;
  ezTime m_WorldTime;
};

struct ezDecalManager::Data
{
  Data()
  {
    ezGALBufferCreationDescription desc;
    desc.m_uiStructSize = sizeof(ezPerDecalAtlasData);
    desc.m_uiTotalSize = desc.m_uiStructSize * 64;
    desc.m_BufferFlags = ezGALBufferUsageFlags::StructuredBuffer | ezGALBufferUsageFlags::ShaderResource;
    desc.m_ResourceAccess.m_bImmutable = false;

    m_hAtlasDataBuffer = ezGALDevice::GetDefaultDevice()->CreateDynamicBuffer(desc, "Decal Atlas Data");
  }

  ~Data()
  {
    ezGALDevice::GetDefaultDevice()->DestroyDynamicBuffer(m_hAtlasDataBuffer);
  }

  void EnsureResourceCreated()
  {
    EZ_LOCK(m_Mutex);

    if (m_RuntimeAtlas.IsInitialized())
      return;

    // use the current CVar values to initialize the values
    ezUInt32 uiAtlasSize = cvar_RenderingDecalsDynamicAtlasSize;

    // if the platform profile has changed, use it to reset the defaults
    const auto& platformProfile = ezGameApplicationBase::GetGameApplicationBaseInstance()->GetPlatformProfile();
    if (s_uiLastConfigModification != platformProfile.GetLastModificationCounter())
    {
      s_uiLastConfigModification = platformProfile.GetLastModificationCounter();

      const auto* pConfig = platformProfile.GetTypeConfig<ezCoreRenderProfileConfig>();
      uiAtlasSize = pConfig->m_uiRuntimeDecalAtlasTextureSize;
    }

    // if the CVars were modified recently (e.g. during game startup), use those values to override the default
    if (cvar_RenderingDecalsDynamicAtlasSize.HasDelayedSyncValueChanged())
      uiAtlasSize = cvar_RenderingDecalsDynamicAtlasSize.GetValue(ezCVarValue::DelayedSync);

    // make sure the values are valid
    uiAtlasSize = ezMath::Clamp(static_cast<ezUInt32>(ezMath::RoundToMultiple(uiAtlasSize, 512.0)), 512u, 8192u);

    // write back the clamped values, so that everyone sees the valid values
    cvar_RenderingDecalsDynamicAtlasSize = uiAtlasSize;
    cvar_RenderingDecalsDynamicAtlasSize.SetToDelayedSyncValue();

    ezGALTextureCreationDescription desc;
    desc.SetAsRenderTarget(uiAtlasSize, uiAtlasSize, ezGALResourceFormat::RGBAUByteNormalized);

    m_RuntimeAtlas.Initialize(desc).AssertSuccess("Failed to initialize runtime decal atlas");

    {
      m_hSimpleCopyMaterial = ezResourceManager::LoadResource<ezMaterialResource>("{ c542b3fa-0c24-4bac-97b7-e481f66f18f1 }"); // DecalCopy.ezMaterialAsset
    }

    const char* szBufferResourceName = "DecalPlaneMeshBuffer";
    m_hPlaneMeshBuffer = ezResourceManager::GetExistingResource<ezMeshBufferResource>(szBufferResourceName);
    if (!m_hPlaneMeshBuffer.IsValid())
    {
      ezGeometry geom;
      geom.AddRect(ezVec2(2.0f));

      ezMeshBufferResourceDescriptor desc;
      desc.AddStream(ezGALVertexAttributeSemantic::Position, ezGALResourceFormat::XYZFloat);
      desc.AllocateStreamsFromGeometry(geom, ezGALPrimitiveTopology::Triangles);

      m_hPlaneMeshBuffer = ezResourceManager::GetOrCreateResource<ezMeshBufferResource>(szBufferResourceName, std::move(desc));
    }
  }

  ezMutex m_Mutex;
  ezDynamicTextureAtlas m_RuntimeAtlas;
  ezGALDynamicBufferHandle m_hAtlasDataBuffer;

  ezMaterialResourceHandle m_hSimpleCopyMaterial;
  ezMeshBufferResourceHandle m_hPlaneMeshBuffer;

  ezDynamicArray<DecalInfo> m_DecalInfos;
  ezHashTable<ezUInt64, ezUInt32> m_DecalKeyToInfoIndex;

  ezDynamicArray<SortedDecal> m_SortedDecals;
  ezDynamicArray<DecalUpdateInfo> m_DecalsToUpdate[2];

  ezDecalAtlasResourceHandle m_hBakedAtlas;
};

//////////////////////////////////////////////////////////////////////////

ezDecalManager::Data* ezDecalManager::s_pData = nullptr;

// static
ezDecalId ezDecalManager::GetOrAddRuntimeDecal(ezTexture2DResourceHandle hTexture, float fScreenSpaceSize, const ezView* pReferenceView, ezTime inactiveTimeBeforeAutoRemove /*= ezTime::MakeFromSeconds(1)*/)
{
  s_pData->EnsureResourceCreated();

  EZ_LOCK(s_pData->m_Mutex);

  ezUInt64 uiKey = DecalInfo::GetKey(hTexture);

  bool bExisted = false;
  ezUInt32& uiIndex = s_pData->m_DecalKeyToInfoIndex.FindOrAdd(uiKey, &bExisted);
  if (!bExisted)
  {
    auto pAtlasDataBuffer = ezGALDevice::GetDefaultDevice()->GetDynamicBuffer(s_pData->m_hAtlasDataBuffer);
    uiIndex = pAtlasDataBuffer->Allocate(0, 1, ezGALDynamicBuffer::AllocateFlags::ZeroFill);

    s_pData->m_DecalInfos.EnsureCount(uiIndex + 1);

    auto& decalInfo = s_pData->m_DecalInfos[uiIndex];
    decalInfo.m_uiAtlasDataOffset = uiIndex;
    decalInfo.m_hTexture = hTexture;

    ezStringBuilder decalMaterialName;
    decalMaterialName.AppendFormat("DecalMaterial_{0}", hTexture.GetResourceID());

    decalInfo.m_hMaterial = ezResourceManager::GetExistingResource<ezMaterialResource>(decalMaterialName);
    if (!decalInfo.m_hMaterial.IsValid())
    {
      ezMaterialResourceDescriptor desc;
      desc.m_hBaseMaterial = s_pData->m_hSimpleCopyMaterial;
      desc.m_Texture2DBindings.PushBack({ezMakeHashedString("BaseTexture"), hTexture});

      decalInfo.m_hMaterial = ezResourceManager::CreateResource<ezMaterialResource>(decalMaterialName, std::move(desc));
    }
  }

  auto& decalInfo = s_pData->m_DecalInfos[uiIndex];
  EZ_ASSERT_DEBUG(decalInfo.m_uiAtlasDataOffset == uiIndex, "Implementation error");
  decalInfo.Update(fScreenSpaceSize, pReferenceView, inactiveTimeBeforeAutoRemove);

  return ezDecalId(uiIndex, decalInfo.m_uiGeneration);
}

ezDecalId ezDecalManager::GetOrAddRuntimeDecal(ezMaterialResourceHandle hMaterial, ezUInt32 uiResolution, ezTime updateInterval, float fScreenSpaceSize, const ezView* pReferenceView, ezTime inactiveTimeBeforeAutoRemove)
{
  s_pData->EnsureResourceCreated();

  EZ_LOCK(s_pData->m_Mutex);

  ezUInt64 uiKey = DecalInfo::GetKey(hMaterial);

  bool bExisted = false;
  ezUInt32& uiIndex = s_pData->m_DecalKeyToInfoIndex.FindOrAdd(uiKey, &bExisted);
  if (!bExisted)
  {
    auto pAtlasDataBuffer = ezGALDevice::GetDefaultDevice()->GetDynamicBuffer(s_pData->m_hAtlasDataBuffer);
    uiIndex = pAtlasDataBuffer->Allocate(0, 1, ezGALDynamicBuffer::AllocateFlags::ZeroFill);

    s_pData->m_DecalInfos.EnsureCount(uiIndex + 1);

    auto& decalInfo = s_pData->m_DecalInfos[uiIndex];
    decalInfo.m_uiAtlasDataOffset = uiIndex;
    decalInfo.m_hMaterial = hMaterial;
  }

  auto& decalInfo = s_pData->m_DecalInfos[uiIndex];
  EZ_ASSERT_DEBUG(decalInfo.m_uiAtlasDataOffset == uiIndex, "Implementation error");
  decalInfo.Update(fScreenSpaceSize, pReferenceView, inactiveTimeBeforeAutoRemove);

  decalInfo.m_fUpdateInterval = ezMath::Min(decalInfo.m_fUpdateInterval, updateInterval.AsFloatInSeconds());
  decalInfo.m_NextUpdateTime = ezMath::Min(decalInfo.m_NextUpdateTime, ezTime::Now() + ezTime::MakeFromSeconds(decalInfo.m_fUpdateInterval));

  decalInfo.m_uiMaxWidth = ezMath::Clamp<ezUInt16>(decalInfo.m_uiMaxWidth, uiResolution, s_uiMaxDecalSize);
  decalInfo.m_uiMaxHeight = decalInfo.m_uiMaxWidth;

  return ezDecalId(uiIndex, decalInfo.m_uiGeneration);
}

// static
void ezDecalManager::RemoveRuntimeDecal(ezDecalId decalId)
{
  EZ_LOCK(s_pData->m_Mutex);

  if (decalId.m_InstanceIndex >= s_pData->m_DecalInfos.GetCount())
    return;

  auto& decalInfo = s_pData->m_DecalInfos[decalId.m_InstanceIndex];
  if (decalInfo.m_uiGeneration != decalId.m_Generation)
    return;

  if (decalInfo.m_atlasAllocationId.IsInvalidated() == false)
  {
    s_pData->m_RuntimeAtlas.Deallocate(decalInfo.m_atlasAllocationId);
  }

  auto pAtlasDataBuffer = ezGALDevice::GetDefaultDevice()->GetDynamicBuffer(s_pData->m_hAtlasDataBuffer);
  pAtlasDataBuffer->Deallocate(decalInfo.m_uiAtlasDataOffset);

  EZ_VERIFY(s_pData->m_DecalKeyToInfoIndex.Remove(decalInfo.GetKey()), "Implemenation error");

  const ezUInt8 generation = decalInfo.m_uiGeneration;
  decalInfo = {}; // Reset the decal info

  decalInfo.m_uiGeneration = generation + 1;
  if (decalInfo.m_uiGeneration == 0)
    decalInfo.m_uiGeneration = 1;
}

ezDecalAtlasResourceHandle ezDecalManager::GetBakedDecalAtlas()
{
  if (s_pData->m_hBakedAtlas.IsValid() == false)
  {
    s_pData->m_hBakedAtlas = ezResourceManager::LoadResource<ezDecalAtlasResource>("{ ProjectDecalAtlas }");
  }

  return s_pData->m_hBakedAtlas;
}

ezGALTextureHandle ezDecalManager::GetRuntimeDecalAtlasTexture()
{
  if (s_pData->m_RuntimeAtlas.IsInitialized())
  {
    return s_pData->m_RuntimeAtlas.GetTexture();
  }

  return ezGALTextureHandle();
}

ezGALBufferHandle ezDecalManager::GetDecalAtlasDataBufferForRendering()
{
  auto pAtlasDataBuffer = ezGALDevice::GetDefaultDevice()->GetDynamicBuffer(s_pData->m_hAtlasDataBuffer);

  return pAtlasDataBuffer->GetBufferForRendering();
}

// static
void ezDecalManager::OnEngineStartup()
{
  s_pData = EZ_DEFAULT_NEW(ezDecalManager::Data);

  ezRenderWorld::GetExtractionEvent().AddEventHandler(OnExtractionEvent);
  ezRenderWorld::GetRenderEvent().AddEventHandler(OnRenderEvent);
}

// static
void ezDecalManager::OnEngineShutdown()
{
  ezRenderWorld::GetExtractionEvent().RemoveEventHandler(OnExtractionEvent);
  ezRenderWorld::GetRenderEvent().RemoveEventHandler(OnRenderEvent);

  EZ_DEFAULT_DELETE(s_pData);
}

// static
void ezDecalManager::OnExtractionEvent(const ezRenderWorldExtractionEvent& e)
{
  if (e.m_Type != ezRenderWorldExtractionEvent::Type::EndExtraction)
    return;

#if EZ_ENABLED(EZ_COMPILE_FOR_DEVELOPMENT)
  if (cvar_RenderingDecalsShowAtlasTexture)
  {
    ezDebugRendererContext debugContext(ezWorld::GetWorld(0));
    float viewWidth = 1920;
    float viewHeight = 1080;

    if (const ezView* pView = ezRenderWorld::GetViewByUsageHint(ezCameraUsageHint::MainView, ezCameraUsageHint::EditorView))
    {
      debugContext = ezDebugRendererContext(pView->GetHandle());
      viewWidth = pView->GetViewport().width;
      viewHeight = pView->GetViewport().height;
    }

    s_pData->m_RuntimeAtlas.DebugDraw(debugContext, viewWidth, viewHeight);
  }
#endif

  auto pAtlasDataBuffer = ezGALDevice::GetDefaultDevice()->GetDynamicBuffer(s_pData->m_hAtlasDataBuffer);

  EZ_LOCK(s_pData->m_Mutex);

  {
    const ezTime now = ezTime::Now();

    for (auto& decalInfo : s_pData->m_DecalInfos)
    {
      if (decalInfo.m_uiAtlasDataOffset == ezInvalidIndex)
        continue;

      if (now > decalInfo.m_AutoRemoveTime)
      {
        RemoveRuntimeDecal(ezDecalId(decalInfo.m_uiAtlasDataOffset, decalInfo.m_uiGeneration));
      }
      else
      {
        bool bShouldUpdate = now >= decalInfo.m_NextUpdateTime;
        if (decalInfo.m_atlasAllocationId.IsInvalidated())
        {
          bShouldUpdate = true;
        }
        else
        {
          const ezRectU16 currentRect = s_pData->m_RuntimeAtlas.GetAllocationRect(decalInfo.m_atlasAllocationId);
          const ezVec2U32 newSize = decalInfo.CalculateScaledSize();

          if (currentRect.width != newSize.x || currentRect.height != newSize.y)
          {
            s_pData->m_RuntimeAtlas.Deallocate(decalInfo.m_atlasAllocationId);
            bShouldUpdate = true;
          }
        }

        if (bShouldUpdate)
        {
          s_pData->m_SortedDecals.PushBack({decalInfo.m_uiAtlasDataOffset, decalInfo.CalculateScore(now)});
        }
      }
    }
  }


  s_pData->m_SortedDecals.Sort();

  const ezVec2 vAtlasSize = ezVec2(float(cvar_RenderingDecalsDynamicAtlasSize));
  auto& decalsToUpdate = s_pData->m_DecalsToUpdate[ezRenderWorld::GetDataIndexForExtraction()];

  for (auto& decalToUpdate : s_pData->m_SortedDecals)
  {
    auto& decalInfo = s_pData->m_DecalInfos[decalToUpdate.m_uiIndex];

    if (decalInfo.m_atlasAllocationId.IsInvalidated())
    {
      ezStringView sName;

      if (decalInfo.m_hTexture.IsValid())
      {
        ezResourceLock<ezTexture2DResource> pTexture(decalInfo.m_hTexture, ezResourceAcquireMode::AllowLoadingFallback);
        if (pTexture.GetAcquireResult() != ezResourceAcquireResult::Final || pTexture->GetNumQualityLevelsLoadable() > 0)
          continue;

        decalInfo.m_uiMaxWidth = ezMath::Clamp(pTexture->GetWidth(), s_uiMinDecalSize, s_uiMaxDecalSize);
        decalInfo.m_uiMaxHeight = ezMath::Clamp(pTexture->GetHeight(), s_uiMinDecalSize, s_uiMaxDecalSize);
        sName = DecalInfo::GetNameFromResource(*pTexture.GetPointer());
      }
      else
      {
        EZ_ASSERT_DEV(decalInfo.m_hMaterial.IsValid(), "DecalInfo must have either a texture or a material assigned.");

        ezResourceLock<ezMaterialResource> pMaterial(decalInfo.m_hMaterial, ezResourceAcquireMode::AllowLoadingFallback);
        if (pMaterial.GetAcquireResult() != ezResourceAcquireResult::Final)
          continue;

        sName = DecalInfo::GetNameFromResource(*pMaterial.GetPointer());
      }

      ezVec2U32 uiSize = decalInfo.CalculateScaledSize();

      ezRectU16 rect;
      decalInfo.m_atlasAllocationId = s_pData->m_RuntimeAtlas.Allocate(uiSize.x, uiSize.y, sName, &rect);

      auto data = pAtlasDataBuffer->MapForWriting<ezPerDecalAtlasData>(decalInfo.m_uiAtlasDataOffset);
      data[0] = MakeAtlasData(rect, vAtlasSize);
    }

    auto& updateInfo = decalsToUpdate.ExpandAndGetRef();
    updateInfo.m_hMaterial = decalInfo.m_hMaterial;
    updateInfo.m_TargetRect = s_pData->m_RuntimeAtlas.GetAllocationRect(decalInfo.m_atlasAllocationId);
    updateInfo.m_WorldTime = decalInfo.m_WorldTime;

    // Reset data
    decalInfo.m_NextUpdateTime = ezTime::Now() + ezTime::MakeFromSeconds(decalInfo.m_fUpdateInterval);
    decalInfo.m_fUpdateInterval = ezTime::MakeFromHours(3600).AsFloatInSeconds();
    decalInfo.m_fMaxScreenSpaceSize = 0.0f;

    if (decalInfo.m_hTexture.IsValid() == false)
    {
      decalInfo.m_uiMaxWidth = 0;
      decalInfo.m_uiMaxHeight = 0;
      decalInfo.m_WorldTime = ezTime::MakeZero();
    }
  }

  s_pData->m_SortedDecals.Clear();

  pAtlasDataBuffer->UploadChangesForNextFrame();
}

// static
void ezDecalManager::OnRenderEvent(const ezRenderWorldRenderEvent& e)
{
  if (e.m_Type != ezRenderWorldRenderEvent::Type::BeginRender)
    return;

  if (s_pData->m_RuntimeAtlas.IsInitialized() &&
      (cvar_RenderingDecalsDynamicAtlasSize.HasDelayedSyncValueChanged() ||
        s_uiLastConfigModification != ezGameApplicationBase::GetGameApplicationBaseInstance()->GetPlatformProfile().GetLastModificationCounter()))
  {
    OnEngineShutdown();
    OnEngineStartup();
  }

  if (s_pData->m_RuntimeAtlas.IsInitialized() == false || s_pData->m_hPlaneMeshBuffer.IsValid() == false)
    return;

  auto& decalsToUpdate = s_pData->m_DecalsToUpdate[ezRenderWorld::GetDataIndexForRendering()];
  if (decalsToUpdate.IsEmpty())
    return;

  ezGALDevice* pDevice = ezGALDevice::GetDefaultDevice();
  auto rtv = pDevice->GetDefaultRenderTargetView(s_pData->m_RuntimeAtlas.GetTexture());
  if (rtv.IsInvalidated())
    return;

  {
    auto pRenderContext = ezRenderContext::GetDefaultInstance();

    // Don't allow async shader loading since we don't want to miss any updates
    const bool bAllowAsyncShaderLoading = pRenderContext->GetAllowAsyncShaderLoading();
    pRenderContext->SetAllowAsyncShaderLoading(false);
    EZ_SCOPE_EXIT(pRenderContext->SetAllowAsyncShaderLoading(bAllowAsyncShaderLoading));

    pRenderContext->BindMeshBuffer(s_pData->m_hPlaneMeshBuffer);

    ezGALCommandEncoder* pCommandEncoder = pDevice->BeginCommands("Decal Atlas");

    ezGALRenderingSetup renderingSetup;
    renderingSetup.SetColorTarget(0, rtv, ezGALRenderTargetLoadOp::Load, ezGALRenderTargetStoreOp::Store);

    for (ezUInt32 i = 0; i < decalsToUpdate.GetCount(); ++i)
    {
      auto& updateInfo = decalsToUpdate[i];
      ezRectFloat viewport = ezRectFloat(updateInfo.m_TargetRect.x, updateInfo.m_TargetRect.y, updateInfo.m_TargetRect.width, updateInfo.m_TargetRect.height);

      if (i == 0)
      {
        pRenderContext->BeginRendering(renderingSetup, viewport, "Decal Atlas");
      }
      else
      {
        pCommandEncoder->SetViewport(viewport);
      }

      pRenderContext->SetGlobalAndWorldTimeConstants(updateInfo.m_WorldTime);
      pRenderContext->BindMaterial(updateInfo.m_hMaterial);

      pRenderContext->DrawMeshBuffer().IgnoreResult();
    }

    pRenderContext->EndRendering();
    pDevice->EndCommands(pCommandEncoder);

    decalsToUpdate.Clear();
  }
}

EZ_STATICLINK_FILE(RendererCore, RendererCore_Decals_Implementation_DecalManager);
