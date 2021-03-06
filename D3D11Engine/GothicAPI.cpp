#include "pch.h"
#include "GothicAPI.h"
#include "Engine.h"
#include "BaseGraphicsEngine.h"
#include "zCPolygon.h"
#include "WorldConverter.h"
#include "HookedFunctions.h"
#include "zCMaterial.h"
#include "zCTexture.h"
#include "zCVisual.h"
#include "zCVob.h"
#include "zCProgMeshProto.h"
#include "zCCamera.h"
#include "oCGame.h"
#include "zCModel.h"
//#include "ReferenceD3D11GraphicsEngine.h"
#include "zCMorphMesh.h"
#include "zCParticleFX.h"
#include "GSky.h"
#include "GInventory.h"

#define DIRECTINPUT_VERSION 0x0700
#include <dinput.h>
#include "BaseAntTweakBar.h"
#include "zCInput.h"
#include "zCBspTree.h"
#include "BaseLineRenderer.h"
#include "D3D7\MyDirect3DDevice7.h"
#include "GVegetationBox.h"
#include "oCNPC.h"
#include "zCMeshSoftSkin.h"
#include "GRenderThread.h"
#include "GOcean.h"
#include "zCVobLight.h"
#include "UpdateCheck.h"
#include "zCQuadMark.h"
#include "zCOption.h"
#include "zCRndD3D.h"
#include "win32ClipboardWrapper.h"
#include "zCSoundSystem.h"

/** Writes this info to a file */
void MaterialInfo::WriteToFile(const std::string& name)
{
	FILE* f = fopen(("system\\GD3D11\\textures\\infos\\" + name + ".mi").c_str(), "wb");

	if(!f)
	{
		LogError() << "Failed to open file '" << ("system\\GD3D11\\textures\\infos\\" + name + ".mi") << "' for writing! Make sure the game runs in Admin mode "
					  " to get the rights to write to that directory!";

		return;
	}

	// Write the version first
	fwrite(&MATERIALINFO_VERSION, sizeof(MATERIALINFO_VERSION), 1, f);

	// Then the data
	fwrite(&buffer, sizeof(MaterialInfo::Buffer), 1, f);

	fclose(f);
}

/** Loads this info from a file */
void MaterialInfo::LoadFromFile(const std::string& name)
{
	FILE* f = fopen(("system\\GD3D11\\textures\\infos\\" + name + ".mi").c_str(), "rb");

	if(!f)
		return;

	// Write the version first
	int version;
	fread(&version, sizeof(int), 1, f);

	// Then the data
	ZeroMemory(&buffer, sizeof(MaterialInfo::Buffer));
	fread(&buffer, sizeof(MaterialInfo::Buffer), 1, f);

	if(version < 2)
	{
		if(buffer.DisplacementFactor == 0.0f)
		{
			buffer.DisplacementFactor = 0.7f;
		}

		if(buffer.TextureScale == 0)
		{
			buffer.TextureScale = 3.3f;
		}
	}

	if(version < 3)
	{
		buffer.FresnelFactor = 0.5f;
	}

	fclose(f);

	UpdateConstantbuffer();
}

/** creates/updates the constantbuffer */
void MaterialInfo::UpdateConstantbuffer()
{
	if(Constantbuffer)
	{
		Constantbuffer->UpdateBuffer(&buffer);
	}else
	{
		Engine::GraphicsEngine->CreateConstantBuffer(&Constantbuffer, &buffer, sizeof(buffer));
	}
}


GothicAPI::GothicAPI(void)
{
	//UpdateCheck::CheckForUpdate();

	OriginalGothicWndProc = 0;

	TextureTestBindMode = false;

	ZeroMemory(BoundTextures, sizeof(BoundTextures));

	CameraReplacementPtr = NULL;
	WrappedWorldMesh = NULL;

	RenderThread = NULL;
	Ocean = NULL;
	CurrentCamera = NULL;

	MainThreadID = GetCurrentThreadId();

	PendingMovieFrame = NULL;

	//RenderThread = new GRenderThread;
	//XLE(RenderThread->InitThreads());
}


GothicAPI::~GothicAPI(void)
{
	//ResetWorld(); // Just let it leak for now. // FIXME: Do this properly

	delete Ocean;
	delete SkyRenderer;
	delete Inventory;
	delete LoadedWorldInfo;
	delete WrappedWorldMesh;
	delete RenderThread;
}

/** Called when the game starts */
void GothicAPI::OnGameStart()
{
	LoadMenuSettings(MENU_SETTINGS_FILE);

	if(RendererState.RendererSettings.EnableAutoupdates && !zCOption::GetOptions()->IsParameter("XNOUPDATE"))
		UpdateCheck::RunUpdater();

#ifdef PUBLIC_RELEASE

#ifndef BUILD_GOTHIC_1_08k
	// See if the user correctly onstalled the normalmaps
	CheckNormalmapFiles();
#endif
#endif

	LoadedWorldInfo = new WorldInfo;
	LoadedWorldInfo->HighestVertex = 2;
	LoadedWorldInfo->LowestVertex = 3;
	LoadedWorldInfo->MidPoint = D3DXVECTOR2(4,5);

	// Get start directory
	char dir[MAX_PATH];
	GetCurrentDirectoryA(MAX_PATH, dir);
	StartDirectory = dir;

	InitializeCriticalSection(&ResourceCriticalSection);

	SkyRenderer = new GSky;
	SkyRenderer->InitSky();

	Inventory = new GInventory; 
}

/** Caches the current world mesh */
/*void GothicAPI::CacheWorldMesh(const std::string& file)
{
	FILE* f;
	fopen_s(&f, file.c_str(), "wb");

	if(!f)
	{
		LogError() << "Failed to write world-cache file!";
		return;
	}

	// Cache file format:
	//
	//- int: FileVersion
	//	- int: NumVertices
	//	- List of ExVertexStruct
	//	- int: NumIndices
	//	- List of unsigned ints
	//

	int version = 1;
	fwrite(&version, sizeof(version), 1, f);

	std::vector<ExVertexStruct> vx;
	for(std::map<int, std::map<int, WorldMeshSectionInfo>>::iterator itx = Engine::GAPI->GetWorldSections().begin(); itx != Engine::GAPI->GetWorldSections().end(); itx++)
	{
		for(std::map<int, WorldMeshSectionInfo>::iterator ity = (*itx).second.begin(); ity != (*itx).second.end(); ity++)
		{
			WorldMeshSectionInfo& section = (*ity).second;

			
			for(std::map<MeshKey, MeshInfo*>::iterator it = section.WorldMeshes.begin(); it != section.WorldMeshes.end();it++)
			{
				if(!(*it).first.Material ||
					(*it).first.Material->HasAlphaTest())
					continue;

				for(int i=0;i<(*it).second->Indices.size(); i+=3)
				{
					// Push all triangles
					vx.push_back((*it).second->Vertices[(*it).second->Indices[i]]);
					vx.push_back((*it).second->Vertices[(*it).second->Indices[i+1]]);
					vx.push_back((*it).second->Vertices[(*it).second->Indices[i+2]]);
				}
			}
		}
	}

	std::vector<ExVertexStruct> indexedVertices;
	std::vector<unsigned int> indices;
	WorldConverter::IndexVertices(&vx[0], vx.size(), indexedVertices, indices);

	int numVertices = indexedVertices.size();
	fwrite(&numVertices, sizeof(numVertices), 1, f);

	struct AOVertex
	{
		float3 Position;
		float3 Normal;
		float3 Color;
	};

	for(int i=0;i<indexedVertices.size();i++)
	{
		AOVertex v;
		v.Position = indexedVertices[i].Position;
		v.Normal = indexedVertices[i].Normal;
		v.Color = float3(1.0f, 1.0f, 1.0f);

		fwrite(&v, sizeof(v), 1, f);
	}

	int numIndices = indices.size();
	fwrite(&numIndices, sizeof(numIndices), 1, f);
	fwrite(&indices[0], sizeof(unsigned int) * numIndices, 1, f);

	fclose(f);
}*/

/** Called to update the world, before rendering */
void GothicAPI::OnWorldUpdate()
{
	RendererState.RendererInfo.Reset();
	RendererState.RendererInfo.FPS = GetFramesPerSecond();
	RendererState.GraphicsState.FF_Time = GetTimeSeconds();

	if(zCCamera::GetCamera())
	{
		RendererState.RendererInfo.FarPlane = zCCamera::GetCamera()->GetFarPlane();
		RendererState.RendererInfo.NearPlane = zCCamera::GetCamera()->GetNearPlane();

		//zCCamera::GetCamera()->Activate();
		SetViewTransform(zCCamera::GetCamera()->GetTransform(zCCamera::ETransformType::TT_VIEW));
	}

	// Update renderthread with information
	if(RenderThread)
	{
		FrameInfo* pendingInfo = new FrameInfo;
		RenderThread->SetPendingFrameInfo(pendingInfo);
	}

	// Apply the hints for the sound system to fix voices in indoor locations being quiet
	// This was originally done in zCBspTree::Render 
	if(IsCameraIndoor())
	{ 
		// Set mode to 2, which means we are indoors, but can see the outside
		if(zCSoundSystem::GetSoundSystem())
			zCSoundSystem::GetSoundSystem()->SetGlobalReverbPreset(2, 0.6f);

		if(oCGame::GetGame()->_zCSession_world && oCGame::GetGame()->_zCSession_world->GetSkyControllerOutdoor())
			oCGame::GetGame()->_zCSession_world->GetSkyControllerOutdoor()->SetCameraLocationHint(1);
	}else
	{
		// Set mode to 0, which is the default
		if(zCSoundSystem::GetSoundSystem())
			zCSoundSystem::GetSoundSystem()->SetGlobalReverbPreset(0, 0.0f); 

		if(oCGame::GetGame()->_zCSession_world && oCGame::GetGame()->_zCSession_world->GetSkyControllerOutdoor())
			oCGame::GetGame()->_zCSession_world->GetSkyControllerOutdoor()->SetCameraLocationHint(0);
	}

	// Fix the ice-texture if it is loaded, this is for the original game and only a quick fix
	// TODO: Fix properly!
	if(LoadedWorldInfo->WorldName == "OLDWORLD")
	{
		static bool s_fixed = false;
		if(!s_fixed)
		{
			// Get material, this is NULL when the texture isn't loaded which is why we need to check for it every frame until we got it
			for(std::set<zCMaterial *>::iterator it = LoadedMaterials.begin(); it != LoadedMaterials.end(); it++)
			{
				if((*it)->GetTexture())
				{
					std::string tn = (*it)->GetTexture()->GetNameWithoutExt();
					if(_strnicmp("OW_ICEDRAGON_LAKE_01", tn.c_str(), 255)==0)
					{
						// Fix broken ice-lakes in old world
						(*it)->SetAlphaFunc(zMAT_ALPHA_FUNC_FUNC_NONE);

						MaterialInfo* info = GetMaterialInfoFrom((*it)->GetTexture());
						info->MaterialType = MaterialInfo::MT_None;

						s_fixed = true;
					}
				}
			}
		}
	}
}

/** Returns gothics fps-counter */
int GothicAPI::GetFramesPerSecond()
{
	return ((vidGetFPSRate)GothicMemoryLocations::Functions::vidGetFPSRate)();
}

/** Returns wether the camera is indoor or not */
bool GothicAPI::IsCameraIndoor()
{
	if(!oCGame::GetGame() || !oCGame::GetGame()->_zCSession_camVob || !oCGame::GetGame()->_zCSession_camVob->GetGroundPoly())
		return false;

	return oCGame::GetGame()->_zCSession_camVob->GetGroundPoly()->GetLightmap() != NULL;
}

/** Returns global time */
float GothicAPI::GetTimeSeconds()
{
#ifdef BUILD_GOTHIC_1_08k
	if(zCTimer::GetTimer())
		return zCTimer::GetTimer()->totalTimeFloat / 1000.0f; // Gothic 1 has this in seconds
#else
	if(zCTimer::GetTimer())
		return zCTimer::GetTimer()->totalTimeFloatSecs;
#endif

	return 0.0f;
}

/** Returns the current frame time */
float GothicAPI::GetFrameTimeSec()
{
#ifdef BUILD_GOTHIC_1_08k
	if(zCTimer::GetTimer())
		return zCTimer::GetTimer()->frameTimeFloat / 1000.0f;
#else
	if(zCTimer::GetTimer())
		return zCTimer::GetTimer()->frameTimeFloatSecs;
#endif
	return -1.0f;
}

/** Disables the input from gothic */
void GothicAPI::SetEnableGothicInput(bool value)
{
	zCInput* input = zCInput::GetInput();

	if(!input)
		return;

	static int disableCounter = 0;

	// Check if everything has disabled input
	if(disableCounter > 0 && value)
	{
		disableCounter--;

		if(disableCounter < 0)
			disableCounter = 0;

		if(disableCounter > 0)
			return; // Do nothing, we decremented the counter and it's still not 0
	}

	if(!value)
		disableCounter++;

	// zMouse, false
	input->SetDeviceEnabled(2, value ? 1 : 0);
	input->SetDeviceEnabled(1, value ? 1 : 0);

/*#ifdef BUILD_GOTHIC_1_08k
	return;
#endif*/

	IDirectInputDevice7A* dInputMouse = *(IDirectInputDevice7A **)GothicMemoryLocations::GlobalObjects::DInput7DeviceMouse;
	IDirectInputDevice7A* dInputKeyboard = *(IDirectInputDevice7A **)GothicMemoryLocations::GlobalObjects::DInput7DeviceKeyboard;
	if(dInputMouse)
	{
		//LogInfo() << "Got DInput (Mouse)!";
		if(!value)
			dInputMouse->Unacquire();
		else
			dInputMouse->Acquire();
	}

	if(dInputKeyboard)
	{
		//LogInfo() << "Got DInput (Keyboard)!";
		if(!value)
			dInputKeyboard->Unacquire();
		else
			dInputKeyboard->Acquire();
	}

}



/** Called when the window got set */
void GothicAPI::OnSetWindow(HWND hWnd)
{
	if(OriginalGothicWndProc || !hWnd)
		return; // Dont do that twice

	OutputWindow = hWnd;

	// Start here, create our engine
	Engine::GraphicsEngine->SetWindow(hWnd);

	OriginalGothicWndProc = GetWindowLongPtrA(hWnd, GWL_WNDPROC);
	SetWindowLongPtrA(hWnd, GWL_WNDPROC, (LONG)GothicWndProc);
}

/** Returns the GraphicsState */
GothicRendererState* GothicAPI::GetRendererState()
{
	return &RendererState;
}


/** Spawns a vegetationbox at the camera */
GVegetationBox* GothicAPI::SpawnVegetationBoxAt(const D3DXVECTOR3& position, 
												const D3DXVECTOR3& min, 
												const D3DXVECTOR3& max, 
												float density,
												const std::string& restrictByTexture)
{
	GVegetationBox* v = new GVegetationBox;
	v->InitVegetationBox(min + position, max + position, "", density, 1.0f, restrictByTexture);

	VegetationBoxes.push_back(v);

	return v;
}


/** Adds a vegetationbox to the world */
void GothicAPI::AddVegetationBox(GVegetationBox* box)
{
	VegetationBoxes.push_back(box);
}

/** Removes a vegetationbox from the world */
void GothicAPI::RemoveVegetationBox(GVegetationBox* box)
{
	VegetationBoxes.remove(box);
	delete box;
}

/** Resets the object, like at level load */
void GothicAPI::ResetWorld()
{
	WorldSections.clear();
	
	ResetVobs();

	delete WrappedWorldMesh;
	WrappedWorldMesh = NULL;

	// Clear inventory too?
}

/** Resets only the vobs */
void GothicAPI::ResetVobs()
{
	// Clear sections
	for(std::map<int, std::map<int, WorldMeshSectionInfo>>::iterator itx = Engine::GAPI->GetWorldSections().begin(); itx != Engine::GAPI->GetWorldSections().end(); itx++)
	{
		for(std::map<int, WorldMeshSectionInfo>::iterator ity = (*itx).second.begin(); ity != (*itx).second.end(); ity++)
		{
			WorldMeshSectionInfo& section = (*ity).second;

			section.Vobs.clear();
		}
	}

	// Remove vegetation
	ResetVegetation();

	// Clear helper-lists
	ParticleEffectVobs.clear();
	RegisteredVobs.clear();
	BspLeafVobLists.clear();
	DynamicallyAddedVobs.clear();
	DecalVobs.clear();
	VobsByVisual.clear();
	SkeletalVobMap.clear();

	// Delete static mesh visuals
	for(auto it = StaticMeshVisuals.begin(); it != StaticMeshVisuals.end();it++)
	{
		delete (*it).second;
	}
	StaticMeshVisuals.clear();

	// Delete skeletal mesh visuals
	for(auto it = SkeletalMeshVisuals.begin(); it != SkeletalMeshVisuals.end();it++)
	{
		delete (*it).second;
	}
	SkeletalMeshVisuals.clear();

	// Delete static mesh vobs
	for(auto it = VobMap.begin(); it != VobMap.end(); it++)
	{
		delete (*it).second;
	}
	VobMap.clear();

	// Delete skeletal mesh vobs
	for(std::list<SkeletalVobInfo *>::iterator it = SkeletalMeshVobs.begin(); it != SkeletalMeshVobs.end(); it++)
	{
		delete (*it);
	}
	SkeletalMeshVobs.clear();

	// Delete light vobs
	for(std::hash_map<zCVobLight*, VobLightInfo*>::iterator it = VobLightMap.begin(); it != VobLightMap.end(); it++)
	{
		delete (*it).second;
	}
	VobLightMap.clear();
}

/** Called when the game loaded a new level */
void GothicAPI::OnGeometryLoaded(zCPolygon** polys, unsigned int numPolygons)
{
	LogInfo() << "Extracting world";
	 
	ResetWorld();


	//WorldConverter::ConvertWorldMesh(polys, numPolygons, &WorldSections, LoadedWorldInfo, &WrappedWorldMesh);
	//WorldConverter::ConvertWorldMeshPNAEN(polys, numPolygons, &WorldSections, LoadedWorldInfo, &WrappedWorldMesh);

	
	std::string worldStr = "system\\GD3D11\\meshes\\WLD_" + LoadedWorldInfo->WorldName + ".obj";
	// Convert world to our own format
#ifdef BUILD_GOTHIC_2_6_fix
	WorldConverter::ConvertWorldMesh(polys, numPolygons, &WorldSections, LoadedWorldInfo, &WrappedWorldMesh);
#else
	if(Toolbox::FileExists(worldStr))
	{
		WorldConverter::LoadWorldMeshFromFile(worldStr, &WorldSections, LoadedWorldInfo, &WrappedWorldMesh);
	}else
	{
		WorldConverter::ConvertWorldMesh(polys, numPolygons, &WorldSections, LoadedWorldInfo, &WrappedWorldMesh);
	}
#endif
	LogInfo() << "Done extracting world!";
}

/** Called when the game is about to load a new level */
void GothicAPI::OnLoadWorld(const std::string& levelName, int loadMode)
{
	if((loadMode == 0 || loadMode == 2) && !levelName.empty())
	{
		std::string name = levelName;
		const size_t last_slash_idx = name.find_last_of("\\/");
		if (std::string::npos != last_slash_idx)
		{
			name.erase(0, last_slash_idx + 1);
		}

		// Remove extension if present.
		const size_t period_idx = name.rfind('.');
		if (std::string::npos != period_idx)
		{
			name.erase(period_idx);
		}

		// Initial load
		LoadedWorldInfo->WorldName = name;
	}

	// Reset caches
	BspLeafVobLists.clear();
	ResetVobs();

#ifndef PUBLIC_RELEASE
	// Disable input here, so you can tab out
	SetEnableGothicInput(false);
#endif
}



/** Called when the game is done loading the world */
void GothicAPI::OnWorldLoaded()
{
	LogInfo() << "Collecting vobs...";
	



	// Get all VOBs
	zCTree<zCVob>* vobTree = oCGame::GetGame()->_zCSession_world->GetGlobalVobTree();
	TraverseVobTree(vobTree);
	 
	// Build instancing cache for the static vobs for each section
	BuildStaticMeshInstancingCache();

	// Build vob info cache for the bsp-leafs
	BuildBspVobMapCache();

#ifdef BUILD_GOTHIC_1_08k
	CreatezCPolygonsForSections();
	PutCustomPolygonsIntoBspTree();
#endif

	// Cache world
	//CacheWorldMesh("WorldMesh.aom");


	if(!Ocean)
	{
		Ocean = new GOcean;
		Ocean->InitOcean();
	}

	// Load our stuff
	//LoadCustomZENResources();

	LogInfo() << "Done!";

	LogInfo() << "Settings sky texture for " << LoadedWorldInfo->WorldName;

	// Hard code the original games sky textures here, since we can't modify the scripts to use the ikarus bindings without
	// installing more content like a .mod file
	if(LoadedWorldInfo->WorldName == "OLDWORLD" || LoadedWorldInfo->WorldName == "WORLD")
	{
		GetSky()->SetSkyTexture(ESkyTexture::ST_OldWorld); // Sky for gothic 2s oldworld
		RendererState.RendererSettings.SetupOldWorldSpecificValues();
	}else if(LoadedWorldInfo->WorldName == "ADDONWORLD")
	{
		GetSky()->SetSkyTexture(ESkyTexture::ST_NewWorld); // Sky for gothic 2s addonworld
		RendererState.RendererSettings.SetupAddonWorldSpecificValues();
	}else 
	{
		GetSky()->SetSkyTexture(ESkyTexture::ST_NewWorld); // Make newworld default
		RendererState.RendererSettings.SetupNewWorldSpecificValues();
	}

#ifndef PUBLIC_RELEASE
	// Enable input again, disabled it when loading started
	SetEnableGothicInput(true);
#endif
}



/** Goes through the given zCTree and registeres all found vobs */
void GothicAPI::TraverseVobTree(zCTree<zCVob>* tree)
{
	// Iterate through the nodes
	if(tree->FirstChild != NULL)
	{
		TraverseVobTree(tree->FirstChild);
	}

	if(tree->Next != NULL)
	{
		TraverseVobTree(tree->Next);
	}

	// Add the vob if it exists and has a visual
	if(tree->Data)
	{
		if(tree->Data->GetVisual())
			OnAddVob(tree->Data, (zCWorld *)oCGame::GetGame()->_zCSession_world);
	}
}

/** Returns in which directory we started in */
const std::string& GothicAPI::GetStartDirectory()
{
	return StartDirectory;
}

/** Builds the static mesh instancing cache */
void GothicAPI::BuildStaticMeshInstancingCache()
{
	for(std::hash_map<zCProgMeshProto*, MeshVisualInfo*>::iterator it = StaticMeshVisuals.begin(); it != StaticMeshVisuals.end(); it++)
	{
		(*it).second->StartNewFrame();
	}

	// Cache everything
	/*for(std::map<int, std::map<int, WorldMeshSectionInfo>>::iterator itx = Engine::GAPI->GetWorldSections().begin(); itx != Engine::GAPI->GetWorldSections().end(); itx++)
	{
		for(std::map<int, WorldMeshSectionInfo>::iterator ity = (*itx).second.begin(); ity != (*itx).second.end(); ity++)
		{
			WorldMeshSectionInfo& section = (*ity).second;

			// Cache positions for the vobs
			for(std::list<VobInfo*>::iterator itv = section.Vobs.begin(); itv != section.Vobs.end();itv++)
			{
				section.InstanceCache.ClearCacheForStatic((MeshVisualInfo *)(*itv)->VisualInfo);
			}

			// Cache positions for the vobs
			for(std::list<VobInfo*>::iterator itv = section.Vobs.begin(); itv != section.Vobs.end();itv++)
			{
				VS_ExConstantBuffer_PerInstance cb;
				cb.World = *(*itv)->Vob->GetWorldMatrixPtr();

				section.InstanceCache.InstanceCacheData[(MeshVisualInfo *)(*itv)->VisualInfo].push_back(cb);
			}

			// Generate constantbuffers
			for(std::map<MeshVisualInfo *, std::vector<VS_ExConstantBuffer_PerInstance>>::iterator it = section.InstanceCache.InstanceCacheData.begin(); it != section.InstanceCache.InstanceCacheData.end(); it++)
			{				

				Engine::GraphicsEngine->CreateVertexBuffer(&section.InstanceCache.InstanceCache[(*it).first]);
				
				section.InstanceCache.InstanceCache[(*it).first]->Init(&(*it).second[0], (*it).second.size() * sizeof(VS_ExConstantBuffer_PerInstance));
			}

			// Generate full section mesh for the current section
			//WorldConverter::GenerateFullSectionMesh(section);
		}
	}*/
}

/** Draws the world-mesh */
void GothicAPI::DrawWorldMeshNaive()
{
	if(!zCCamera::GetCamera() || !oCGame::GetGame())
		return;

	static float setfovH = RendererState.RendererSettings.FOVHoriz;
	static float setfovV = RendererState.RendererSettings.FOVVert;
	if(zCCamera::GetCamera() && (zCCamera::GetCamera() != CurrentCamera || setfovH != RendererState.RendererSettings.FOVHoriz || setfovV != RendererState.RendererSettings.FOVVert))
	{
		setfovH = RendererState.RendererSettings.FOVHoriz;
		setfovV = RendererState.RendererSettings.FOVVert;

		// Fix camera FOV-Bug
		zCCamera::GetCamera()->SetFOV(RendererState.RendererSettings.FOVHoriz, (Engine::GraphicsEngine->GetResolution().y / (float)Engine::GraphicsEngine->GetResolution().x) * RendererState.RendererSettings.FOVVert);

		CurrentCamera = zCCamera::GetCamera();
	}

	FrameParticleInfo.clear();
	FrameParticles.clear();
	FrameMeshInstances.clear();

	//for(int i=0;i<100;i++)
	
	START_TIMING();
	Engine::GraphicsEngine->DrawWorldMesh();
	STOP_TIMING(GothicRendererTiming::TT_WorldMesh);

	for(std::list<GVegetationBox *>::iterator it = VegetationBoxes.begin(); it != VegetationBoxes.end();it++)
	{
		(*it)->RenderVegetation(GetCameraPosition());
	}


	//Engine::GraphicsEngine->SetActivePixelShader("PS_Simple");

	// Get camera position (TODO: Make a function for this)
	
	D3DXVECTOR3 camPos = GetCameraPosition();

	INT2 camSection = WorldConverter::GetSectionOfPos(camPos);
	//camSection.x += 1;
	//camSection.y += 1;

	D3DXMATRIX view;
	GetViewMatrix(&view);

	// Clear instances
	int sectionViewDist = Engine::GAPI->GetRendererState()->RendererSettings.SectionDrawRadius;

	START_TIMING();
	if(RendererState.RendererSettings.DrawSkeletalMeshes)
	{
		Engine::GraphicsEngine->SetActivePixelShader("PS_World");
		// Set up frustum for the camera
		zCCamera::GetCamera()->Activate();

		for(std::list<SkeletalVobInfo *>::iterator it = SkeletalMeshVobs.begin(); it != SkeletalMeshVobs.end(); it++)
		{
			// Don't render if sleeping and has skeletal meshes available
			if(!(*it)->VisualInfo)// || ((*it)->Vob->GetSleepingMode() == 0 && !((SkeletalMeshVisualInfo *)(*it)->VisualInfo)->SkeletalMeshes.empty()))
				continue;

			INT2 s = WorldConverter::GetSectionOfPos((*it)->Vob->GetPositionWorld());

			//if(abs(s.x - camSection.x) > sectionViewDist || abs(s.y - camSection.y) > sectionViewDist)
			//	continue;

			float dist = D3DXVec3Length(&((*it)->Vob->GetPositionWorld() - camPos));
			if(dist > RendererState.RendererSettings.SkeletalMeshDrawRadius)
				continue; // Skip out of range

			zTBBox3D bb = (*it)->Vob->GetBBoxLocal();
			zCCamera::GetCamera()->SetTransform(zCCamera::ETransformType::TT_WORLD, *(*it)->Vob->GetWorldMatrixPtr());

			//Engine::GraphicsEngine->GetLineRenderer()->AddAABBMinMax(bb.Min, bb.Max, D3DXVECTOR4(1,1,1,1));

			int clipFlags = 15; // No far clip
			if(zCCamera::GetCamera()->BBox3DInFrustum(bb, clipFlags) == ZTCAM_CLIPTYPE_OUT)
				continue;

			// Indoor? // TODO: Double-check if this is really a simple getter. Also, is this even valid for NPCs?
			(*it)->IndoorVob = (*it)->Vob->GetGroundPoly() && ((*it)->Vob->GetGroundPoly()->GetLightmap() != 0);

			zCModel* model = (zCModel *)(*it)->Vob->GetVisual();

			if(!model)// || vi->VisualInfo)
				continue; // Gothic fortunately sets this to 0 when it throws the model out of the cache

			// This is important, because gothic only lerps between animation when this distance is set and below ~2000
			model->SetDistanceToCamera(dist);

			DrawSkeletalMeshVob((*it), dist);
		}
	}
	STOP_TIMING(GothicRendererTiming::TT_SkeletalMeshes);

	RendererState.RasterizerState.CullMode = GothicRasterizerStateInfo::CM_CULL_FRONT;
	RendererState.RasterizerState.SetDirty();

	// Reset vob-state
	/*for(std::hash_map<zCProgMeshProto*, MeshVisualInfo*>::iterator it = StaticMeshVisuals.begin(); it != StaticMeshVisuals.end(); it++)
	{
		(*it).second->StartNewFrame();
	}*/

	// Draw vobs in view
	Engine::GraphicsEngine->DrawVOBs();


	// Draw ocean
	//if(Ocean)
	//	Ocean->Draw();

	//DebugDrawBSPTree();

	ResetWorldTransform();
	ResetViewTransform();

	/*for(int i=0;i<SkeletalMeshVobs.size();i++)
	{
		DrawSkeletalMeshVob(SkeletalMeshVobs[i]);
	}*/
}

/** Draws particles, in a simple way */
void GothicAPI::DrawParticlesSimple()
{
	ParticleFrameData data;

	if(RendererState.RendererSettings.DrawParticleEffects)
	{
		D3DXVECTOR3 camPos = GetCameraPosition();

		std::vector<zCVob *> renderedParticleFXs;
		for(std::list<zCVob *>::iterator it = ParticleEffectVobs.begin(); it != ParticleEffectVobs.end(); it++)
		{
			INT2 s = WorldConverter::GetSectionOfPos((*it)->GetPositionWorld());

			//if(abs(s.x - camSection.x) > 2  || abs(s.y - camSection.y) > 2)
			//	continue;

			float dist = D3DXVec3Length(&((*it)->GetPositionWorld() - camPos));
			if(dist > RendererState.RendererSettings.OutdoorSmallVobDrawRadius)
				continue;

			if((*it)->GetVisual())
			{
				renderedParticleFXs.push_back((*it));
			}
		}

		// Update particles
		/*for(unsigned int i=0;i<renderedParticleFXs.size();i++)
		{
			// Update here, since UpdateParticleFX can actually delete the particle FX and all its vobs
			// ((zCParticleFX *)renderedParticleFXs[i]->GetVisual())->UpdateParticleFX();
		}*/

		// now it is save to render
		for(std::list<zCVob *>::iterator it = ParticleEffectVobs.begin(); it != ParticleEffectVobs.end(); it++)
		{
			INT2 s = WorldConverter::GetSectionOfPos((*it)->GetPositionWorld());

			//if(abs(s.x - camSection.x) > 2  || abs(s.y - camSection.y) > 2)
			//	continue;

			float dist = D3DXVec3Length(&((*it)->GetPositionWorld() - camPos));
			if(dist > RendererState.RendererSettings.VisualFXDrawRadius)
				continue;

			if((*it)->GetVisual())
			{
				int mod = dist > RendererState.RendererSettings.IndoorVobDrawRadius ? 2 : 1;

				
				DrawParticleFX((*it), (zCParticleFX *)(*it)->GetVisual(), data);
			}
		}		
		
		DrawTestInstances();
	}
}

/** Returns a list of visible particle-effects */
void GothicAPI::GetVisibleParticleEffectsList(std::vector<zCVob*>& pfxList)
{
	if(RendererState.RendererSettings.DrawParticleEffects)
	{
		D3DXVECTOR3 camPos = GetCameraPosition();

		// now it is save to render
		for(std::list<zCVob *>::iterator it = ParticleEffectVobs.begin(); it != ParticleEffectVobs.end(); it++)
		{
			INT2 s = WorldConverter::GetSectionOfPos((*it)->GetPositionWorld());

			//if(abs(s.x - camSection.x) > 2  || abs(s.y - camSection.y) > 2)
			//	continue;

			float dist = D3DXVec3Length(&((*it)->GetPositionWorld() - camPos));
			if(dist > RendererState.RendererSettings.VisualFXDrawRadius)
				continue;

			if((*it)->GetVisual())
			{
				pfxList.push_back((*it));
			}
		}	
	}
}

static bool DecalSortcmpFunc(const std::pair<zCVob *, float>& a, const std::pair<zCVob *, float>& b)
{
	return a.second > b.second; // Back to front
}

/** Gets a list of visible decals */
void GothicAPI::GetVisibleDecalList(std::vector<zCVob *>& decals)
{
	D3DXVECTOR3 camPos = GetCameraPosition();
	static std::vector<std::pair<zCVob *, float>> decalDistances; // Static to get around reallocations

	for(std::list<zCVob *>::iterator it = DecalVobs.begin(); it != DecalVobs.end(); it++)
	{
		float dist = D3DXVec3Length(&((*it)->GetPositionWorld() - camPos));
		if(dist > RendererState.RendererSettings.VisualFXDrawRadius)
			continue;

		if((*it)->GetVisual())
		{
			decalDistances.push_back(std::make_pair((*it), dist));
		}
	}

	// Sort back to front
	std::sort(decalDistances.begin(), decalDistances.end(), DecalSortcmpFunc);

	// Put into output list
	for(auto it = decalDistances.begin(); it != decalDistances.end(); it++)
	{
		decals.push_back((*it).first);
	}

	decalDistances.clear();

}

/** Called when a material got removed */
void GothicAPI::OnMaterialDeleted(zCMaterial* mat)
{
	LoadedMaterials.erase(mat);
	if(!mat)
		return;

	/** Map for static mesh visuals */
	/*for(std::hash_map<zCProgMeshProto*, MeshVisualInfo*>::iterator it = StaticMeshVisuals.begin(); it != StaticMeshVisuals.end(); it++)
	{
		for(std::map<zCMaterial *, std::vector<MeshInfo*>>::iterator itm = (*it).second->Meshes.begin(); itm != (*it).second->Meshes.end(); itm++)
		{
			if((*itm).first == mat)
			{
				(*it).second->UnloadedSomething = true;
			}
		}
	}*/
	
	
	for(std::hash_map<std::string, SkeletalMeshVisualInfo*>::iterator it = SkeletalMeshVisuals.begin(); it != SkeletalMeshVisuals.end(); it++)
	{
		(*it).second->Meshes.erase(mat);
		(*it).second->SkeletalMeshes.erase(mat);

		/*for(std::map<int, std::vector<MeshVisualInfo *>>::iterator ait = (*it).second->NodeAttachments.begin(); ait != (*it).second->NodeAttachments.end(); ait++)
		{
			if(!(*ait).second.empty())
			{
				(*ait).second[0]->Meshes.erase(mat); // Make sure no skeletal mesh can use an invalid material
			}
		}*/
	}

	
}

/** Called when a material got removed */
void GothicAPI::OnMaterialCreated(zCMaterial* mat)
{
	LoadedMaterials.insert(mat);
}

/** Returns if the material is currently active */
bool GothicAPI::IsMaterialActive(zCMaterial* mat)
{
	std::set<zCMaterial *>::iterator it = LoadedMaterials.find(mat);
	if(it != LoadedMaterials.end())
	{
		return true;
	}

	return false;
}

/** Called when a vob moved */
void GothicAPI::OnVobMoved(zCVob* vob)
{
	auto it = VobMap.find(vob);

	if(it != VobMap.end() && !(*it).second->ParentBSPNodes.empty())
	{
		// Move vob into the dynamic list, if not already done
		MoveVobFromBspToDynamic((*it).second);
	}
}

/** Called when a visual got removed */
void GothicAPI::OnVisualDeleted(zCVisual* visual)
{
	std::vector<std::string> extv;
	
	// Add to map
	std::list<BaseVobInfo *> list = VobsByVisual[visual];
	for(auto it = list.begin(); it != list.end(); it++)
	{
		OnRemovedVob((*it)->Vob, LoadedWorldInfo->MainWorld);
	}
	VobsByVisual[visual].clear();

	/** OLD CODE, MAYBE USEFUL */

	// Get the visuals possible file extensions
	int e=0;
	while(strlen(visual->GetFileExtension(e)) > 0)
	{
		extv.push_back(visual->GetFileExtension(e));
		e++;
	}

	// Check every extension
	for(unsigned int i=0; i<extv.size();i++)
	{
		std::string ext = extv[i];

		// Delete according to the type
		if(ext == ".3DS")
		{
			// Clear the visual from all vobs (FIXME: This may be slow!)
			for(std::hash_map<zCVob*, VobInfo*>::iterator it = VobMap.begin();it != VobMap.end();it++)
			{
				if(!(*it).second->VisualInfo) // This happens sometimes, so get rid of it
				{
					it = VobMap.erase(it);
					continue;
				}

				if((*it).second->VisualInfo->Visual == (zCProgMeshProto *)visual)
				{
					(*it).second->VisualInfo = NULL;
				}
			}

			delete StaticMeshVisuals[(zCProgMeshProto *)visual];
			StaticMeshVisuals.erase((zCProgMeshProto *)visual);

			
			
			break;
		}else if(ext == ".MDS" || ext == ".ASC")
		{
			if(((zCModel *)visual)->GetMeshSoftSkinList()->NumInArray)
			{
				// Find vobs using this visual
				for(std::list<SkeletalVobInfo *>::iterator it = SkeletalMeshVobs.begin(); it != SkeletalMeshVobs.end(); it++)
				{
					if((*it)->VisualInfo && ((zCModel *)(*it)->VisualInfo->Visual)->GetMeshSoftSkinList()->Array[0] == ((zCModel *)visual)->GetMeshSoftSkinList()->Array[0])
					{			
						(*it)->VisualInfo = NULL;
					}
				}
			}

			std::string mds = ((zCModel *)visual)->GetModelName().ToChar();
			
			//if(mds == "SWARM.MDS")
			//	LogInfo() << "Swarm!";

			std::string str = ((zCModel *)visual)->GetVisualName();

			if(str.empty()) // Happens when the model has no skeletal-mesh
				str = mds;

			delete SkeletalMeshVisuals[str];
			SkeletalMeshVisuals.erase(str);
			break;
		}
	}
}
/** Draws a MeshInfo */
void GothicAPI::DrawMeshInfo(zCMaterial* mat, MeshInfo* msh)
{
	// Check for material and bind the texture if it exists
	if(mat)
	{
		if(mat->GetTexture())
		{
			/*if((*it).first->GetTexture()->GetCacheState() != zRES_CACHED_IN)
			{
			(*it).first->GetTexture()->CacheIn(0.6f);
			}else
			{
			(*it).first->GetTexture()->Bind(0);
			}*/

			// Setup alphatest //FIXME: This has to be done earlier!
			if(mat->GetAlphaFunc() == zRND_ALPHA_FUNC_TEST)
				RendererState.GraphicsState.FF_GSwitches |= GSWITCH_ALPHAREF;
			else
				RendererState.GraphicsState.FF_GSwitches &= ~GSWITCH_ALPHAREF;
		}
	}

	if(!msh->MeshIndexBuffer)
	{
		Engine::GraphicsEngine->DrawVertexBuffer(msh->MeshVertexBuffer, msh->Vertices.size());
	}else
	{
		Engine::GraphicsEngine->DrawVertexBufferIndexed(msh->MeshVertexBuffer, msh->MeshIndexBuffer, msh->Indices.size());
	}
}

/** Draws a SkeletalMeshInfo */
void GothicAPI::DrawSkeletalMeshInfo(zCMaterial* mat, SkeletalMeshInfo* msh, SkeletalMeshVisualInfo* vis, std::vector<D3DXMATRIX>& transforms, float fatness)
{
	// Check for material and bind the texture if it exists
	if(mat)
	{
		if(mat->GetTexture())
		{
			if(mat->GetAniTexture()->CacheIn(0.6f) == zRES_CACHED_IN)
				mat->GetAniTexture()->Bind(0);
			else
				return;
		}else
		{
			Engine::GraphicsEngine->UnbindTexture(0);
		}
	}

	if(RendererState.RendererSettings.EnableTesselation && !msh->IndicesPNAEN.empty())
	{
		Engine::GraphicsEngine->DrawSkeletalMesh(msh->MeshVertexBuffer, msh->MeshIndexBufferPNAEN, msh->IndicesPNAEN.size(), transforms, fatness, vis);
	}else
	{
		Engine::GraphicsEngine->DrawSkeletalMesh(msh->MeshVertexBuffer, msh->MeshIndexBuffer, msh->Indices.size(), transforms, fatness, vis);
	}
}

/** Locks the resource CriticalSection */
void GothicAPI::EnterResourceCriticalSection()
{
	//ResourceMutex.lock();
	EnterCriticalSection(&ResourceCriticalSection);
}

/** Unlocks the resource CriticalSection */
void GothicAPI::LeaveResourceCriticalSection()
{
	//ResourceMutex.unlock();
	LeaveCriticalSection(&ResourceCriticalSection);
}

/** Called when a VOB got removed from the world */
void GothicAPI::OnRemovedVob(zCVob* vob, zCWorld* world)
{
	//LogInfo() << "Removing vob: " << vob;

	std::set<zCVob *>::iterator it = RegisteredVobs.find(vob);
	if(it == RegisteredVobs.end())
	{
		// Not registered
		return;
	}

	RegisteredVobs.erase(vob);

	// Erase the vob from visual-vob map
	std::list<BaseVobInfo *>& list = VobsByVisual[vob->GetVisual()];
	for(std::list<BaseVobInfo *>::iterator it = list.begin(); it != list.end(); it++)
	{
		if((*it)->Vob == vob)
		{
			it = list.erase(it);
			break; // Can (should!) only be in here once
		}
	}
	
	// Check if this was in some inventory
	if(Inventory->OnRemovedVob(vob, world))
		return; // Don't search in the other lists since it wont be in it anyways

	if(world != LoadedWorldInfo->MainWorld)
		return; // *should* be already deleted from the inventory here. But watch out for dem leaks, dragons be here!

	VobInfo* vi = VobMap[vob];
	SkeletalVobInfo* svi = SkeletalVobMap[vob];
	VobLightInfo* li = VobLightMap[(zCVobLight *)vob];

	// Erase it from the particle-effect list
	ParticleEffectVobs.remove(vob);
	DecalVobs.remove(vob);

	//if(!tempParticleNames[vob].empty())
	//	LogInfo() << "Removing Particle-Effect with: " << tempParticleNames[vob];



	// Erase it from the list of lights
	VobLightMap.erase((zCVobLight*)vob);

	// Remove from BSP-Cache
	std::vector<BspVobInfo*>* nodes = NULL;
	if(vi)
		nodes = &vi->ParentBSPNodes;
	else if(li)
		nodes = &li->ParentBSPNodes;

	if(nodes)
	{
		for(unsigned int i=0;i<nodes->size();i++)
		{
			BspVobInfo* node = (*nodes)[i];
			if(vi)
			{
				for(std::vector<VobInfo *>::iterator bit = node->IndoorVobs.begin(); bit != node->IndoorVobs.end(); bit++)
				{
					if((*bit) == vi)
					{
						node->IndoorVobs.erase(bit);
						break;
					}
				}

				for(std::vector<VobInfo *>::iterator bit = node->Vobs.begin(); bit != node->Vobs.end(); bit++)
				{
					if((*bit) == vi)
					{
						node->Vobs.erase(bit);
						break;
					}
				}

				for(std::vector<VobInfo *>::iterator bit = node->SmallVobs.begin(); bit != node->SmallVobs.end(); bit++)
				{
					if((*bit) == vi)
					{
						node->SmallVobs.erase(bit);
						break;
					}
				}
			}

			if(li && nodes)
			{
				for(std::vector<VobLightInfo *>::iterator bit = node->Lights.begin(); bit != node->Lights.end(); bit++)
				{
					if((*bit)->Vob == (zCVobLight *)vob)
					{
						node->Lights.erase(bit);
						break;
					}
				}

				for(std::vector<VobLightInfo *>::iterator bit = node->IndoorLights.begin(); bit != node->IndoorLights.end(); bit++)
				{
					if((*bit)->Vob == (zCVobLight *)vob)
					{
						node->IndoorLights.erase(bit);
						break;
					}
				}

				//(*it).second.IndoorVobs.remove(vi);
				//(*it).second.SmallVobs.remove(vi);
				//(*it).second.Vobs.remove(vi);
			}
		}
	}
	SkeletalVobMap.erase(vob);

	// Erase the vob from the section

	if(vi && vi->VobSection)
	{
		vi->VobSection->Vobs.remove(vi);
	}

	// Erase it from the skeletal vob-list
	for(std::list<SkeletalVobInfo *>::iterator it = SkeletalMeshVobs.begin(); it != SkeletalMeshVobs.end(); it++)
	{
		if((*it)->Vob == vob)
		{
			SkeletalVobInfo* vi = (*it);
			it = SkeletalMeshVobs.erase(it);

			delete vi;

			//LogInfo() << "Removed vob (SKELETAL): " << vob;
			break;
		}
	}

	// Erase it from dynamically loaded vobs
	for(std::list<VobInfo *>::iterator it = DynamicallyAddedVobs.begin(); it != DynamicallyAddedVobs.end(); it++)
	{
		if((*it)->Vob == vob)
		{
			DynamicallyAddedVobs.erase(it);
			break;
		}
	}
	
	// Erase it from vob-map
	std::hash_map<zCVob*, VobInfo*>::iterator vit = VobMap.find(vob);
	if(vit != VobMap.end())
	{
		delete (*vit).second;
		VobMap.erase(vob);
	}

	// delete light info, if valid
	delete li;

	/*for(std::list<VobInfo *>::iterator it = WorldSections[s.x][s.y].Vobs.begin(); it != WorldSections[s.x][s.y].Vobs.end(); it++)
	{
		if((*it)->Vob == vob)
		{
			VobInfo* vi = (*it);
			WorldSections[s.x][s.y].Vobs.remove((*it));

			delete vi;
			break;
		}
	}

	for(std::list<VobInfo *>::iterator it = WorldSections[s.x][s.y].Vobs.begin(); it != WorldSections[s.x][s.y].Vobs.end(); it++)
	{
		if((*it)->Vob == vob)
		{
			VobInfo* vi = (*it);
			WorldSections[s.x][s.y].Vobs.remove((*it));

			delete vi;
			break;
		}
	}*/

	//if(!tempParticleNames[vob].empty())
	//	LogInfo() << "Done!";
}

/** Called on a SetVisual-Call of a vob */
void GothicAPI::OnSetVisual(zCVob* vob)
{
	if(!oCGame::GetGame() || !oCGame::GetGame()->_zCSession_world || !vob->GetHomeWorld())
		return;

	// Check for skeletalmesh
	/*if((zCModel*)vob->GetVisual() == NULL || std::string(vob->GetVisual()->GetFileExtension(0)) != ".PFX")
	{
		return; // Only take PFX
	}*/

	// Add the vob to the set
	std::set<zCVob *>::iterator it = RegisteredVobs.find(vob);
	if(it != RegisteredVobs.end())
	{
		for(std::list<SkeletalVobInfo *>::iterator it = SkeletalMeshVobs.begin(); it != SkeletalMeshVobs.end(); it++)
		{
			if((*it)->VisualInfo && (*it)->Vob == vob && (*it)->VisualInfo->Visual == (zCModel *)vob->GetVisual())
			{
				return; // No change, skip this.
			}
		}

		//LogInfo() << "Set visual called on registered vob, updating";
		// This one is already there. Re-Add it!
		OnRemovedVob(vob, vob->GetHomeWorld());	
	}

	OnAddVob(vob, vob->GetHomeWorld());
}

/** Called when a VOB got added to the BSP-Tree */
void GothicAPI::OnAddVob(zCVob* vob, zCWorld* world)
{
	//LogInfo() << "Adding vob: " << vob;


	if(!vob->GetVisual())
		return; // Don't need it if we can't render it

	// Add the vob to the set
	std::set<zCVob *>::iterator it = RegisteredVobs.find(vob);
	if(it != RegisteredVobs.end())
	{
		// Already got that
		return;
	}

	RegisteredVobs.insert(vob);

	if(!world)
		world = oCGame::GetGame()->_zCSession_world;

	std::vector<std::string> extv;
	
	int e=0;
	while(strlen(vob->GetVisual()->GetFileExtension(e)) > 0)
	{
		extv.push_back(vob->GetVisual()->GetFileExtension(e));
		e++;
	}

	for(unsigned int i=0; i<extv.size();i++)
	{
		std::string ext = extv[i];

		if(ext == ".3DS" || ext == ".MMS")
		{
			zCProgMeshProto* pm;
			if(ext == ".3DS")
				pm = (zCProgMeshProto *)vob->GetVisual();
			else
				pm = ((zCMorphMesh *)vob->GetVisual())->GetMorphMesh();

			if(StaticMeshVisuals.count(pm) == 0)
			{
				if(pm->GetNumSubmeshes() == 0)
					return; // Empty mesh?

				// Load the new visual
				MeshVisualInfo* mi = new MeshVisualInfo;

				//LogInfo() << "Loading: " << vob->GetVisual()->GetObjectName();
				/*if (strncmp(vob->GetVisual()->GetObjectName(), "ITAR_RANGER_ADDON.3DS", strlen("ITAR_RANGER_ADDON.3DS")) == 0)
				{
					LogInfo() << "OMG!";
					//return;
				}*/

				WorldConverter::Extract3DSMeshFromVisual2(pm, mi);

				StaticMeshVisuals[pm] = mi;
			}

			INT2 section = WorldConverter::GetSectionOfPos(vob->GetPositionWorld());
			/*ID3D11Device* device = ((D3D11GraphicsEngine *)Engine::GraphicsEngine)->GetDevice();

			for(int i=0;i<INT_MAX;i++)
			{
				VS_ExConstantBuffer_PerInstance dt; // Just some 64-byte sized struct

				// Init cb
				D3D11_SUBRESOURCE_DATA d;
				d.pSysMem = &dt;
				d.SysMemPitch = 0;
				d.SysMemSlicePitch = 0;
	
				// Create constantbuffer
				ID3D11Buffer* buffer;
				device->CreateBuffer(&CD3D11_BUFFER_DESC(sizeof(VS_ExConstantBuffer_PerInstance), D3D11_BIND_CONSTANT_BUFFER), &d, &buffer);
				
				// Release it again
				int r = buffer->Release();
			}*/

			VobInfo* vi = new VobInfo;
			vi->Vob = vob;
			vi->VisualInfo = StaticMeshVisuals[pm];

			// Add to map
			VobsByVisual[vob->GetVisual()].push_back(vi);

			// Check for mainworld
			if(world == oCGame::GetGame()->_zCSession_world)
			{
				VobMap[vob] = vi;
				WorldSections[section.x][section.y].Vobs.push_back(vi);

				vi->VobSection = &WorldSections[section.x][section.y];
	
				// Create this constantbuffer only for non-inventory vobs because it would be recreated for each vob every frame
				Engine::GraphicsEngine->CreateConstantBuffer(&vi->VobConstantBuffer, NULL, sizeof(VS_ExConstantBuffer_PerInstance));
				vi->UpdateVobConstantBuffer();

				if(!BspLeafVobLists.empty()) // Check if this is the initial loading
				{
					// It's not, chose this as a dynamically added vob
					DynamicallyAddedVobs.push_back(vi);
				}

			}else
			{
				// Must be inventory
				Inventory->OnAddVob(vi, world);
			}
			break;
		}else if(ext == ".MDS" || ext == ".ASC")
		{
			std::string mds = ((zCModel *)vob->GetVisual())->GetModelName().ToChar();
			
			//if(mds == "SWARM.MDS")
			//	LogInfo() << "Swarm!";

			std::string str = ((zCModel *)vob->GetVisual())->GetVisualName();

			if(str.empty()) // Happens when the model has no skeletal-mesh
				str = mds;

			if(str == "HAMMEL_BODY")
			{
				str == "SHEEP_BODY"; // FIXME: HAMMEL_BODY seems to have fucked up bones, but only this model! Replace with usual sheep before I fix this
				if(!SkeletalMeshVisuals[str])
				{
					RegisteredVobs.erase(vob);
					SkeletalMeshVisuals.erase(str);
					return; // Just don't load it here!
				}
			}

			// Load the model or get it from cache if already done
			SkeletalMeshVisualInfo* mi = LoadzCModelData(((zCModel *)vob->GetVisual()));
		
			// Add vob to the skeletal list
			SkeletalVobInfo* vi = new SkeletalVobInfo;
			vi->Vob = vob;
			vi->VisualInfo = SkeletalMeshVisuals[str];

			// Add to map
			VobsByVisual[vob->GetVisual()].push_back(vi);

			/*for(std::list<SkeletalVobInfo *>::iterator it = SkeletalMeshVobs.begin(); it != SkeletalMeshVobs.end(); it++)
			{
				if((*it)->Vob == vob)
				{
					LogError() << "SKELETAL ALREADY IN LIST!";
				}
			}*/

			// Check for mainworld
			if(world == oCGame::GetGame()->_zCSession_world)
			{
				SkeletalMeshVobs.push_back(vi);
				SkeletalVobMap[vob] = vi;
			}else
			{
				// Must be inventory
				//Inventory->OnAddVob(vi, world);
			}
			break;
		}else if(ext == ".PFX")
		{
			//if(!BspLeafVobLists.empty())
			//	return; // FIXME: Don't add particle effects after the loading time, since some effects (like landing from a jump?) will crash the game!

			ParticleEffectVobs.push_back(vob);

			//tempParticleNames[vob] = ((zCParticleFX *)vob->GetVisual())->GetEmitter()->particleFXName.ToChar() + std::string(" | ") + ((zCParticleFX *)vob->GetVisual())->GetEmitter()->visName_S.ToChar();
			break;
		}else if(ext == ".TGA")
		{
			DecalVobs.push_back(vob);
			break;
		}
	}
}

/** Loads the data out of a zCModel */
SkeletalMeshVisualInfo* GothicAPI::LoadzCModelData(zCModel* model)
{
	std::string mds = model->GetModelName().ToChar();
	std::string str = model->GetVisualName();

	if(str.empty()) // Happens when the model has no skeletal-mesh
		str = mds;

	SkeletalMeshVisualInfo* mi = SkeletalMeshVisuals[str];

	if(!mi || mi->Meshes.empty())
	{
		// Load the new visual
		if(!mi)
			mi = new SkeletalMeshVisualInfo;

		WorldConverter::ExtractSkeletalMeshFromVob(model, mi);

		mi->Visual = model;

		SkeletalMeshVisuals[str] = mi;
	}

	return mi;
}

SkeletalMeshVisualInfo* GothicAPI::LoadzCModelPrototypeData(zCModelPrototype* model)
{
	/*std::string str = model->GetVisualName();

	if(str.empty()) // Happens when the model has no skeletal-mesh
		return NULL; // Deal with that at a later time

	SkeletalMeshVisualInfo* mi = SkeletalMeshVisuals[str];

	if(!mi || mi->Meshes.empty())
	{
		// Load the new visual
		if(!mi)
			mi = new SkeletalMeshVisualInfo;

		WorldConverter::ExtractSkeletalMeshFromProto(model, mi);

		mi->Visual = model;

		SkeletalMeshVisuals[str] = mi;
	}

	return mi;*/

	return NULL;
}

// FIXME: REMOVE!!
#include "D3D11GraphicsEngine.h"

/** Draws a skeletal mesh-vob */
void GothicAPI::DrawSkeletalMeshVob(SkeletalVobInfo* vi, float distance)
{
	// FIXME: Put this into the renderer!!
	D3D11GraphicsEngine* g = (D3D11GraphicsEngine *)Engine::GraphicsEngine;
	g->SetActiveVertexShader("VS_Ex");

	std::vector<D3DXMATRIX> transforms;
	zCModel* model = (zCModel *)vi->Vob->GetVisual();

	

	if(!model)// || vi->VisualInfo)
		return; // Gothic fortunately sets this to 0 when it throws the model out of the cache

	//std::string name = model->GetModelName().ToChar();
	//if(name.empty())
	//	return; // This happens for wild bees in Odyssee for example. Not sure what that is, but it crashes horrible.


	model->SetIsVisible(true);

#ifndef BUILD_GOTHIC_1_08k
	if(!vi->Vob->GetShowVisual())
		return;
#endif

	if(model->GetDrawHandVisualsOnly())
		return; // Not supported yet

	g->SetDefaultStates();

	D3DXMATRIX world;
	vi->Vob->GetWorldMatrix(&world);

	D3DXMATRIX scale;
	D3DXVECTOR3 modelScale = model->GetModelScale();
	D3DXMatrixScaling(&scale, modelScale.x, modelScale.y, modelScale.z);
	world = world * scale;

	zCCamera::GetCamera()->SetTransform(zCCamera::TT_WORLD, world);

	// Get the bone transforms
	model->GetBoneTransforms(&transforms, vi->Vob);

	// Update textures
	/*for(int i=0;i<model->GetMeshLibList()->NumInArray;i++)
	{
		model->GetMeshLibList()->Array[i]->TexAniState.UpdateTexList();
	}*/

	// Update attachments
	model->UpdateAttachedVobs();
	model->UpdateMeshLibTexAniState();

	D3DXMATRIX view;
	GetViewMatrix(&view);
	SetWorldViewTransform(world, view);

	for(unsigned int i=0;i<transforms.size();i++)
	{
		//D3DXMatrixTranspose(&transforms[i], &transforms[i]);
	}

	float fatness = model->GetModelFatness();
	 
	// Draw submeshes
	for(std::map<zCMaterial *, std::vector<SkeletalMeshInfo*>>::iterator itm = ((SkeletalMeshVisualInfo *)vi->VisualInfo)->SkeletalMeshes.begin(); itm != ((SkeletalMeshVisualInfo *)vi->VisualInfo)->SkeletalMeshes.end();itm++)
	{
		for(unsigned int i=0;i<(*itm).second.size();i++)
		{
			DrawSkeletalMeshInfo((*itm).first, (*itm).second[i], ((SkeletalMeshVisualInfo *)vi->VisualInfo), transforms, fatness);
		}
	}

	g->SetActiveVertexShader("VS_Ex");

	g->SetDefaultStates();
	RendererState.RasterizerState.CullMode = GothicRasterizerStateInfo::CM_CULL_FRONT;
	RendererState.RasterizerState.SetDirty();
	g->UpdateRenderStates();

	/*D3DXMATRIX identity;
	D3DXMatrixIdentity(&identity);
	zCCamera::GetCamera()->SetTransform(zCCamera::TT_WORLD, identity);*/

	
	std::map<int, std::vector<MeshVisualInfo *>>& nodeAttachments = vi->NodeAttachments;
	for(unsigned int i=0;i<transforms.size();i++)
	{
		// Check for new visual
		zCModel* visual = (zCModel *)vi->Vob->GetVisual();
		zCModelNodeInst* node = visual->GetNodeList()->Array[i];

		if(!node->NodeVisual)
			continue; // Happens when you pull your sword for example

		// Check if this is loaded
		if(node->NodeVisual && nodeAttachments.find(i) == nodeAttachments.end())
		{
			// It's not, extract it
			WorldConverter::ExtractNodeVisual(i, node, nodeAttachments);
		}

		// Check for changed visual
		if(nodeAttachments[i].size() && node->NodeVisual != (zCVisual *)nodeAttachments[i][0]->Visual)
		{
			// Visual changed
			//delete vi->VisualInfo->NodeAttachments[i][0];

			// Check for deleted attachment
			if(!node->NodeVisual)
			{
				// Remove attachment
				delete nodeAttachments[i][0];
				nodeAttachments[i].clear();

				LogInfo() << "Removed attachment from model " << vi->VisualInfo->VisualName;

				continue; // Go to next attachment
			}

			// check if we already loaded this
			//nodeAttachments[i][0] = StaticMeshVisuals[(zCProgMeshProto *)node->NodeVisual];

			//if(!nodeAttachments[i][0])
			{
				/*if(nodeAttachments[i].size() > 1)
					LogWarn() << "MEMLEAK: Node has more than one attachment!";*/

				//nodeAttachments[i].clear();

				// Load the new one
				WorldConverter::ExtractNodeVisual(i, node, nodeAttachments);



				// Store
				//StaticMeshVisuals[(zCProgMeshProto *)node->NodeVisual] = nodeAttachments[i][0];
			}
		}

		if(nodeAttachments.find(i) != nodeAttachments.end())
		{
			//RendererState.TransformState.TransformWorld = transforms[i] * RendererState.TransformState.TransformWorld;

			// Go through all attachments this node has
			for(unsigned int n=0;n<nodeAttachments[i].size();n++)
			{
				SetWorldViewTransform(world * transforms[i], view);

				if(!nodeAttachments[i][n]->Visual)
				{
					LogWarn() << "Attachment without visual on model: " << model->GetVisualName();
					continue;
				}

				// Update animated textures
				node->TexAniState.UpdateTexList();

				g->SetupVS_ExMeshDrawCall();
				g->SetupVS_ExConstantBuffer();
				

				bool isMMS = std::string(node->NodeVisual->GetFileExtension(0)) == ".MMS";
				if(distance < 2000 && isMMS)
				{
					zCMorphMesh* mm = (zCMorphMesh *)node->NodeVisual;
					mm->GetTexAniState()->UpdateTexList();

					g->SetActivePixelShader("PS_DiffuseAlphaTest");

					if(g->GetRenderingStage() == DES_MAIN) // Only draw this as a morphmesh when rendering the main scene
					{					
						D3DXMATRIX fatnessScale;
						float fs = (fatness + 1.0f) * 0.02f; // This is what gothic seems to be doing for heads, and it even looks right...

						// Calculate "patched" scale according to fatness
						D3DXMatrixScaling(&fatnessScale, fs, fs, fs);
						D3DXMATRIX w = world * scale;

						// Set new "fat" worldmatrix
						SetWorldViewTransform(w * transforms[i], view);

						// Update constantbuffer
						g->SetupVS_ExPerInstanceConstantBuffer();

						// Only 0.35f of the fatness wanted by gothic. 
						// They seem to compensate for that with the scaling.
						DrawMorphMesh(mm, fatness * 0.35f);
						continue;
					}
				}

				// Update the head textures in case this is a morph-mesh
				if(isMMS)
				{
					zCMorphMesh* mm = (zCMorphMesh *)node->NodeVisual;
					mm->GetTexAniState()->UpdateTexList();
				}

				g->SetupVS_ExPerInstanceConstantBuffer();

				// Go through all materials registered here
				for(std::map<zCMaterial *, std::vector<MeshInfo*>>::iterator itm = nodeAttachments[i][n]->Meshes.begin(); 
					itm != nodeAttachments[i][n]->Meshes.end();
					itm++)
				{
					if((*itm).first && (*itm).first->GetAniTexture()) // TODO: Crash here!
					{
						if((*itm).first->GetAniTexture()->CacheIn(0.6f) == zRES_CACHED_IN)
						{
							(*itm).first->GetAniTexture()->Bind(0);
							g->BindShaderForTexture((*itm).first->GetAniTexture());
							MaterialInfo* info = GetMaterialInfoFrom((*itm).first->GetAniTexture());
							if(!info->Constantbuffer)
							info->UpdateConstantbuffer();

							info->Constantbuffer->BindToPixelShader(2);
						}
						else
							continue;
					}

					// Go through all meshes using that material
					for(unsigned int m=0;m<(*itm).second.size();m++)
					{
						DrawMeshInfo((*itm).first, (*itm).second[m]);
					}
				}
			}
			//((ReferenceD3D11GraphicsEngine *)Engine::GraphicsEngine)->DrawTriangle();
		}
	}

	//model->SetIsVisible(vi->VisualInfo->Visual->GetMeshSoftSkinList()->NumInArray > 0);



	RendererState.RendererInfo.FrameDrawnVobs++;
}

/** Called when a particle system got removed */
void GothicAPI::OnParticleFXDeleted(zCParticleFX* pfx)
{
	// Remove this from our list
	for(std::list<zCVob *>::iterator it = ParticleEffectVobs.begin();it != ParticleEffectVobs.end(); it++)
	{
		if((*it)->GetVisual() == (zCVisual *)pfx)
			it = ParticleEffectVobs.erase(it);
	}
}

/** Draws a zCParticleFX */
void GothicAPI::DrawParticleFX(zCVob* source, zCParticleFX* fx, ParticleFrameData& data)
{
	// Set the current vob
	//fx->SetVisualUsedBy(source);


	/*if(tempParticleNames.find(source) == tempParticleNames.end() || tempParticleNames[source].size() == 0)
	{
		LogInfo() << "Failed to find ParticleEffect at position " << float3(source->GetPositionWorld()).toString();

		return;
	}*/

	//Engine::GraphicsEngine->GetLineRenderer()->AddPointLocator(source->GetPositionWorld());
	

	// Get our view-matrix
	D3DXMATRIX view;
	GetViewMatrix(&view);	
	
	D3DXMATRIX world;
	source->GetWorldMatrix(&world);
	SetWorldViewTransform(world, view);

	D3DXMATRIX scale;
	float effectBrightness = 1.0f;

	//DrawTriangle();

	// Update effects time
	fx->UpdateTime();

	// Maybe create more emitters?
	fx->CheckDependentEmitter();

	//fx->UpdateParticleFX();

	zTParticle* pfx = fx->GetFirstParticle();
	if(pfx)
	{
		//zCMaterial* mat = fx->GetPartMeshQuad()->GetPolygons()[0]->GetMaterial();

		// Get texture
		zCTexture* texture = NULL;
		if(fx->GetEmitter())
		{
			texture = fx->GetEmitter()->GetVisTexture();
			if(texture)
			{
				// Check if it's loaded
				if(texture->CacheIn(0.6f) != zRES_CACHED_IN)
					return;
			}else
			{
				return;
			}
		}

		// Set render states for this type
		ParticleRenderInfo inf;

		switch(fx->GetEmitter()->GetVisAlphaFunc())
		{
		case zRND_ALPHA_FUNC_ADD:
			inf.BlendState.SetAdditiveBlending();
			effectBrightness = 5.0f;
			inf.BlendMode = zRND_ALPHA_FUNC_ADD;
			break;

		case zRND_ALPHA_FUNC_BLEND:
		default:
			inf.BlendState.SetAlphaBlending();
			inf.BlendMode = zRND_ALPHA_FUNC_BLEND;
			break;
		}

		
		std::vector<ParticleInstanceInfo>& part = FrameParticles[texture];

		// Check for kill
		zTParticle*	kill = NULL;
		zTParticle*	p = NULL;

		for (;;) 
		{
			kill = pfx;
			if (kill && (kill->LifeSpan < *fx->GetPrivateTotalTime())) 
			{			
				//if (kill->PolyStrip)	
				//	kill->polyStrip->Release(); // TODO: MEMLEAK RIGHT HERE!

				pfx = kill->Next;
				fx->SetFirstParticle(pfx);

				kill->Next = *(zTParticle **)GothicMemoryLocations::GlobalObjects::s_globFreePart;
				*(zTParticle **)GothicMemoryLocations::GlobalObjects::s_globFreePart = kill;
				continue;
			}
			break;
		}

		int i=0;
		for (p = pfx ; p ; p=p->Next) 
		{
			//if(i == fx->GetNumParticlesThisFrame() || i == 2048)
			//	break;


			//Engine::GraphicsEngine->GetLineRenderer()->AddPointLocator(p->PositionWS);

			for ( ;; ) 
			{
				kill = p->Next;
				if (kill && (kill->LifeSpan < *fx->GetPrivateTotalTime() )) 
				{
					//if (kill->PolyStrip)	
					//	kill->polyStrip->Release(); // TODO: MEMLEAK RIGHT HERE!

					p->Next			= kill->Next;
					kill->Next = *(zTParticle **)GothicMemoryLocations::GlobalObjects::s_globFreePart;
					*(zTParticle **)GothicMemoryLocations::GlobalObjects::s_globFreePart = kill;
					continue;
				}
				break;
			}

			// Only render every renderNumMods particle
			/*if((i % renderNumMod) != 0)
			{
				i++;
				continue;
			}*/

			// Generate instance info
			ParticleInstanceInfo ii;
			ii.scale = D3DXVECTOR2(p->Size.x, p->Size.y);

			// Construct world matrix
			int alignment = fx->GetEmitter()->GetVisAlignment();
			if(alignment == zPARTICLE_ALIGNMENT_XY)
			{
				D3DXMATRIX rot;
				D3DXMatrixRotationX(&rot, (float)(D3DX_PI / 2.0f));
				D3DXMatrixTranslation(&world, p->PositionWS.x, p->PositionWS.y, p->PositionWS.z);
				world = rot * world;
			}else
			{
				if(alignment == zPARTICLE_ALIGNMENT_VELOCITY)
				{
					D3DXMATRIX sw; source->GetWorldMatrix(&sw);
					D3DXMatrixTranspose(&sw, &sw);

					D3DXVECTOR3 velNrm; D3DXVec3Normalize(&velNrm, &p->Vel);
					D3DXVec3Normalize(&velNrm, &velNrm);
					D3DXVECTOR3 velPosY; D3DXVec3TransformNormal(&velPosY, &velNrm, &sw);
					
					D3DXVECTOR3 velPosX = D3DXVECTOR3(-velPosY.y, velPosY.x, velPosY.z);
					D3DXVECTOR3 xdim = velPosX * ii.scale.x;
					D3DXVECTOR3 ydim = velPosY * ii.scale.y;

					ii.scale.x = -xdim.x + ydim.x;
					ii.scale.y = -xdim.y + ydim.y;

					if(abs(ii.scale.x) < abs(ii.scale.y))
						ii.scale.x = abs(ii.scale.y / 1.5f) * (ii.scale.x < 0 ? -1 : 1);
					else
						ii.scale.y = abs(ii.scale.x / 1.5f) * (ii.scale.y < 0 ? -1 : 1);
				}
				
				//D3DXMatrixIdentity(&world);
				D3DXMatrixTranslation(&world, p->PositionWS.x, p->PositionWS.y, p->PositionWS.z);			
			}

			
			D3DXMatrixTranspose(&world, &world);

#ifndef PUBLIC_RELEASE
			/*if(p->PolyStrip)
			{
				// Collect vertices from strip
				std::vector<ExVertexStruct> stripVx;
				p->PolyStrip->GenerateVertexBuffer(stripVx);

				// bind blend-state and texture
				RendererState.BlendState = inf.BlendState;
				RendererState.BlendState.SetDirty();
				texture->Bind(0);
				
				Engine::GraphicsEngine->SetupPerInstanceConstantBuffer();

				// Draw
				Engine::GraphicsEngine->SetActiveVertexShader("VS_Ex");
				Engine::GraphicsEngine->SetActivePixelShader("PS_Simple");
				Engine::GraphicsEngine->DrawVertexArray(&stripVx[0], stripVx.size());
				continue;
			}*/
#endif

			//Engine::GraphicsEngine->GetLineRenderer()->AddPointLocator(p->PositionWS, 20.0f, D3DXVECTOR4(1,0,0,1));

			//RendererState.TransformState.TransformWorld = world * view;
			//DrawTriangle();

			D3DXMATRIX mat = view * world;
			
			// Undo all rotations from view
			if(alignment != zPARTICLE_ALIGNMENT_XY) 
			{
				for(int x=0;x<3;x++)
				{
					for(int y=0;y<3;y++) 
					{
						if ( x==y )
							mat(x,y) = 1.0;
						else
							mat(x,y) = 0.0;
					}
				}
			}

			//mat *= scale;

			//D3DXMatrixTranspose(&mat, &mat);	
			
			// Set computed matrix to instance info
			ii.worldview = mat;
			

			if(!fx->GetEmitter()->GetVisIsQuadPoly())
			{
				ii.scale.x *= 0.5f;
				ii.scale.y *= 0.5f;
			}

			float4 color;
			color.x = p->Color.x / 255.0f;
			color.y = p->Color.y / 255.0f;
			color.z = p->Color.z / 255.0f;

			if(fx->GetEmitter()->GetVisTexAniIsLooping() != 2) // 2 seems to be some magic case with sinus smoothing
			{
				color.w = std::min(p->Alpha, 255.0f) / 255.0f;
			}else
			{
				color.w = std::min((zCParticleFX::SinSmooth(fabs((p->Alpha - fx->GetEmitter()->GetVisAlphaStart()) * fx->GetEmitter()->GetAlphaDist())) * p->Alpha) / 255.0f, 255.0f);
			}

			color.w = std::max(color.w, 0.0f);

			ii.color = color.ToDWORD();
			part.push_back(ii);

			/*if(data.BufferPosition + sizeof(ii) <= data.BufferSize)
			{
				memcpy(data.Buffer + data.BufferPosition, &ii, sizeof(ii)); // Copy instance info into buffer
			}

			data.BufferPosition += sizeof(ii); // Increase buffer position to next element
			data.NeededSize += sizeof(ii); // Update the buffersize next frame, skip the particle effect for now
			*/
			//if(FrameParticles[texture].size() > 1024)
			//	break;

			fx->UpdateParticle(p);

			i++;
		}

		FrameParticleInfo[texture] = inf;
	}



	// Create new particles?
	fx->CreateParticlesUpdateDependencies();

	// This causes an infinite loop on G1. TODO: Investigate!
#ifndef BUILD_GOTHIC_1_08k
	// Do something I dont exactly know what it does :)
	fx->GetStaticPFXList()->TouchPfx(fx);
#endif
}

/** Debugging */
void GothicAPI::DrawTriangle()
{
	BaseVertexBuffer* vxb;
	Engine::GraphicsEngine->CreateVertexBuffer(&vxb);
	vxb->Init(NULL, 6 * sizeof(ExVertexStruct), BaseVertexBuffer::EBindFlags::B_VERTEXBUFFER, BaseVertexBuffer::EUsageFlags::U_DYNAMIC, BaseVertexBuffer::CA_WRITE);
	
	ExVertexStruct vx[6];
	ZeroMemory(vx, sizeof(vx));

	float scale = 50.0f;
	vx[0].Position = float3(0.0f, 0.5f * scale, 0.0f);
	vx[1].Position = float3(0.45f * scale, -0.5f * scale, 0.0f);
	vx[2].Position = float3(-0.45f * scale, -0.5f * scale, 0.0f);

	vx[0].Color = float4(1,0,0,1).ToDWORD();
	vx[1].Color = float4(0,1,0,1).ToDWORD();
	vx[2].Color = float4(0,0,1,1).ToDWORD();

	vx[3].Position = float3(0.0f, 0.5f * scale, 0.0f);
	vx[5].Position = float3(0.45f * scale, -0.5f * scale, 0.0f);
	vx[4].Position = float3(-0.45f * scale, -0.5f * scale, 0.0f);

	vx[3].Color = float4(1,0,0,1).ToDWORD();
	vx[5].Color = float4(0,1,0,1).ToDWORD();
	vx[4].Color = float4(0,0,1,1).ToDWORD();

	vxb->UpdateBuffer(vx);

	Engine::GraphicsEngine->DrawVertexBuffer(vxb, 6);

	delete vxb;
}

/** Debugging */
void GothicAPI::DrawTestInstances()
{
	//((D3D11GraphicsEngine*)Engine::GraphicsEngine)->SetDefaultStates(); // TODO: This whole function belongs into the graphics engine!

	Engine::GraphicsEngine->DrawFrameParticles(FrameParticles, FrameParticleInfo);
}

/** Sets the world matrix */
void GothicAPI::SetWorldTransform(const D3DXMATRIX& world)
{
	RendererState.TransformState.TransformWorld = world;
}
/** Sets the world matrix */
void GothicAPI::SetViewTransform(const D3DXMATRIX& view)
{
	RendererState.TransformState.TransformView = view;
}

/** Sets the Projection matrix */
void GothicAPI::SetProjTransform(const D3DXMATRIX& proj)
{
	RendererState.TransformState.TransformProj = proj;
}

/** Sets the Projection matrix */
D3DXMATRIX GothicAPI::GetProjTransform()
{
	return RendererState.TransformState.TransformProj;
}

/** Sets the world matrix */
void GothicAPI::SetWorldTransform(const D3DXMATRIX& world, bool transpose)
{
	if(transpose)
		D3DXMatrixTranspose(&RendererState.TransformState.TransformWorld, &world);
	else
		RendererState.TransformState.TransformWorld = world;
}

/** Sets the world matrix */
void GothicAPI::SetViewTransform(const D3DXMATRIX& view, bool transpose)
{
	if(transpose)
		D3DXMatrixTranspose(&RendererState.TransformState.TransformView, &view);
	else
		RendererState.TransformState.TransformView = view;
}

/** Sets the world matrix */
void GothicAPI::SetWorldViewTransform(const D3DXMATRIX& world, const D3DXMATRIX& view)
{
	RendererState.TransformState.TransformWorld = world;
	RendererState.TransformState.TransformView = view;
}

/** Sets the world matrix */
void GothicAPI::ResetWorldTransform()
{
	D3DXMatrixIdentity(&RendererState.TransformState.TransformWorld);
	D3DXMatrixTranspose(&RendererState.TransformState.TransformWorld,&RendererState.TransformState.TransformWorld);
}

/** Sets the world matrix */
void GothicAPI::ResetViewTransform()
{
	D3DXMatrixIdentity(&RendererState.TransformState.TransformView);
	D3DXMatrixTranspose(&RendererState.TransformState.TransformView,&RendererState.TransformState.TransformView);
}

/** Returns the wrapped world mesh */
MeshInfo* GothicAPI::GetWrappedWorldMesh()
{
	return WrappedWorldMesh;
}

/** Returns the loaded sections */
std::map<int, std::map<int, WorldMeshSectionInfo>>& GothicAPI::GetWorldSections()
{
	return WorldSections;
}

static bool TraceWorldMeshBoxCmp(const std::pair<WorldMeshSectionInfo*, float>& a, const std::pair<WorldMeshSectionInfo*, float>& b)
{
	return a.second > b.second;
}

/** Traces vobs with static mesh visual */
VobInfo* GothicAPI::TraceStaticMeshVobsBB(const D3DXVECTOR3& origin, const D3DXVECTOR3& dir, D3DXVECTOR3& hit, zCMaterial** hitMaterial)
{
	float closest = FLT_MAX;

	std::list<VobInfo*> hitBBs;

	for(auto it = VobMap.begin(); it != VobMap.end(); it++)
	{
		D3DXMATRIX world; D3DXMatrixTranspose(&world, (*it).first->GetWorldMatrixPtr());

		D3DXVECTOR3 min; D3DXVec3TransformCoord(&min, &(*it).second->VisualInfo->BBox.Min, &world);
		D3DXVECTOR3 max; D3DXVec3TransformCoord(&max, &(*it).second->VisualInfo->BBox.Max, &world);

		float t = 0;
		if(Toolbox::IntersectBox(min, max, origin, dir, t))
		{
			if(t < closest)
			{
				closest = t;
				hitBBs.push_back((*it).second);
			}
		}
	}

	// Now trace the actual triangles to find the real hit

	closest = FLT_MAX;
	zCMaterial* closestMaterial = NULL;
	VobInfo* closestVob = NULL;
	for(auto it = hitBBs.begin(); it != hitBBs.end(); it++)
	{
		D3DXMATRIX invWorld; D3DXMatrixTranspose(&invWorld, (*it)->Vob->GetWorldMatrixPtr());
		D3DXMatrixInverse(&invWorld, NULL, &invWorld);
		D3DXVECTOR3 localOrigin; D3DXVec3TransformCoord(&localOrigin, &origin, &invWorld);
		D3DXVECTOR3 localDir; D3DXVec3TransformNormal(&localDir, &dir, &invWorld);

		zCMaterial* hitMat = NULL;
		float t = TraceVisualInfo(localOrigin, localDir, (*it)->VisualInfo, &hitMat);
		if(t > 0.0f && t < closest)
		{
			closest = t;
			closestVob = (*it);
			closestMaterial = hitMat;
		}
	}

	if(closest == FLT_MAX)
		return NULL;

	if(hitMaterial)
		*hitMaterial = closestMaterial;

	hit = origin + dir * closest;

	return closestVob;
}

SkeletalVobInfo* GothicAPI::TraceSkeletalMeshVobsBB(const D3DXVECTOR3& origin, const D3DXVECTOR3& dir, D3DXVECTOR3& hit)
{
	float closest = FLT_MAX;
	SkeletalVobInfo* vob = NULL;

	for(auto it = SkeletalMeshVobs.begin(); it != SkeletalMeshVobs.end(); it++)
	{
		float t = 0;
		if(Toolbox::IntersectBox(	(*it)->Vob->GetBBoxLocal().Min + (*it)->Vob->GetPositionWorld(), 
									(*it)->Vob->GetBBoxLocal().Max + (*it)->Vob->GetPositionWorld(), origin, dir, t))
		{
			if(t < closest)
			{
				closest = t;
				vob = (*it);
			}
		}
	}

	if(closest == FLT_MAX)
		return NULL;

	hit = origin + dir * closest;

	return vob;
}

float GothicAPI::TraceVisualInfo(const D3DXVECTOR3& origin, const D3DXVECTOR3& dir, BaseVisualInfo* visual, zCMaterial** hitMaterial)
{
	float u,v,t;
	float closest = FLT_MAX;

	for(auto it = visual->Meshes.begin(); it != visual->Meshes.end(); it++)
	{
		for(unsigned int m = 0; m < (*it).second.size(); m++)
		{
			MeshInfo* mesh = (*it).second[m];

			for(unsigned int i=0;i<mesh->Indices.size();i+=3)
			{
				if(Toolbox::IntersectTri(*mesh->Vertices[mesh->Indices[i]].Position.toD3DXVECTOR3(), 
					*mesh->Vertices[mesh->Indices[i+1]].Position.toD3DXVECTOR3(), 
					*mesh->Vertices[mesh->Indices[i+2]].Position.toD3DXVECTOR3(),
					origin, dir, u,v,t))
				{
					if(t > 0 && t < closest)
					{
						closest = t;

						if(hitMaterial)
							*hitMaterial = (*it).first;
					}
				}
			}
		}
	}

	return closest == FLT_MAX ? -1.0f : closest;
}


/** Traces the worldmesh and returns the hit-location */
bool GothicAPI::TraceWorldMesh(const D3DXVECTOR3& origin, const D3DXVECTOR3& dir, D3DXVECTOR3& hit, std::string* hitTextureName, D3DXVECTOR3* hitTriangle, MeshInfo** hitMesh, zCMaterial** hitMaterial)
{
	const int maxSections = 2;
	float closest = FLT_MAX;
	std::list<std::pair<WorldMeshSectionInfo*, float>> hitSections;

	// Trace bounding-boxes first
	for(std::map<int, std::map<int, WorldMeshSectionInfo>>::iterator itx = WorldSections.begin(); itx != WorldSections.end(); itx++)
	{
		for(std::map<int, WorldMeshSectionInfo>::iterator ity = (*itx).second.begin(); ity != (*itx).second.end(); ity++)
		{
			WorldMeshSectionInfo& section = (*ity).second;

			if(section.WorldMeshes.empty())
				continue;

			float t = 0;
			if(Toolbox::PositionInsideBox(origin, section.BoundingBox.Min, section.BoundingBox.Max) || Toolbox::IntersectBox(section.BoundingBox.Min, section.BoundingBox.Max, origin, dir, t))
			{
				if(t < maxSections * WORLD_SECTION_SIZE)
					hitSections.push_back(std::make_pair(&section, t));		
			}
		}
	}
	// Distance-sort
	hitSections.sort(TraceWorldMeshBoxCmp);

	int numProcessed = 0;
	for(std::list<std::pair<WorldMeshSectionInfo*, float>>::iterator bit = hitSections.begin(); bit != hitSections.end(); bit++)
	{
		for(std::map<MeshKey, WorldMeshInfo*>::iterator it = (*bit).first->WorldMeshes.begin(); it != (*bit).first->WorldMeshes.end();it++)
		{
			float u,v,t;

			for(unsigned int i=0;i<(*it).second->Indices.size();i+=3)
			{
				if(Toolbox::IntersectTri(*(*it).second->Vertices[(*it).second->Indices[i]].Position.toD3DXVECTOR3(), 
					*(*it).second->Vertices[(*it).second->Indices[i+1]].Position.toD3DXVECTOR3(), 
					*(*it).second->Vertices[(*it).second->Indices[i+2]].Position.toD3DXVECTOR3(),
					origin, dir, u,v,t))
				{
					if(t > 0 && t < closest)
					{
						closest = t;

						if(hitTriangle)
						{
							hitTriangle[0] = *(*it).second->Vertices[(*it).second->Indices[i]].Position.toD3DXVECTOR3();
							hitTriangle[1] = *(*it).second->Vertices[(*it).second->Indices[i+1]].Position.toD3DXVECTOR3();
							hitTriangle[2] = *(*it).second->Vertices[(*it).second->Indices[i+2]].Position.toD3DXVECTOR3();	 
						}

						if(hitMesh)
						{
							*hitMesh = (*it).second;
						}

						if(hitMaterial)
						{
							*hitMaterial = (*it).first.Material;
						}

						if(hitTextureName && (*it).first.Material && (*it).first.Material->GetTexture())
							*hitTextureName = (*it).first.Material->GetTexture()->GetNameWithoutExt();
					}
				}
			}

			numProcessed++;

			if(numProcessed == maxSections && closest != FLT_MAX)
				break;
		}
	}
	if(closest == FLT_MAX)
		return false;

	hit = origin + dir * closest;

	return true;
}

/** Unprojects a pixel-position on the screen */
void GothicAPI::Unproject(const D3DXVECTOR3& p, D3DXVECTOR3* worldPos, D3DXVECTOR3* worldDir)
{
	D3DXVECTOR3 u;
	D3DXMATRIX proj = GetProjectionMatrix();
	D3DXMATRIX invView; GetInverseViewMatrix(&invView);
	D3DXMatrixTranspose(&invView, &invView);
	D3DXMatrixTranspose(&proj, &proj);

	// Convert to screenspace
	u.x = (((2 * p.x) / Engine::GraphicsEngine->GetResolution().x) - 1) / proj(0,0);
	u.y = -(((2 * p.y) / Engine::GraphicsEngine->GetResolution().y) - 1) / proj(1,1);
	u.z = 1;

	// Transform and output
	D3DXVec3TransformCoord(worldPos, &u, &invView);
	D3DXVec3Normalize(&u, &u);
	D3DXVec3TransformNormal(worldDir, &u, &invView);
}

/** Unprojects the current cursor */
D3DXVECTOR3 GothicAPI::UnprojectCursor()
{
	D3DXVECTOR3 mPos, mDir;
	POINT p = GetCursorPosition();

	Engine::GAPI->Unproject(D3DXVECTOR3((float)p.x, (float)p.y, 0), &mPos, &mDir);

	return mDir;
}

/** Returns the current cameraposition */
D3DXVECTOR3 GothicAPI::GetCameraPosition()
{
	if(!oCGame::GetGame()->_zCSession_camVob)
		return D3DXVECTOR3(0,0,0);

	if(CameraReplacementPtr)
		return CameraReplacementPtr->PositionReplacement;

	return oCGame::GetGame()->_zCSession_camVob->GetPositionWorld();
}

/** Returns the current forward vector of the camera */
D3DXVECTOR3 GothicAPI::GetCameraForward()
{
	D3DXVECTOR3 fwd = D3DXVECTOR3(1,0,0);
	D3DXVec3TransformNormal(&fwd, &fwd, oCGame::GetGame()->_zCSession_camVob->GetWorldMatrixPtr());
	return fwd;
}

/** Returns the view matrix */
void GothicAPI::GetViewMatrix(D3DXMATRIX* view)
{
	if(CameraReplacementPtr)
	{
		*view = CameraReplacementPtr->ViewReplacement;
		return;
	}

	*view = zCCamera::GetCamera()->GetTransform(zCCamera::ETransformType::TT_VIEW);
	//D3DXMatrixTranspose(view, oCGame::GetGame()->_zCSession_camVob->GetWorldMatrixPtr());
	//D3DXMatrixInverse(view, NULL, oCGame::GetGame()->_zCSession_camVob->GetWorldMatrixPtr());	
}


/** Returns the view matrix */
void GothicAPI::GetInverseViewMatrix(D3DXMATRIX* invView)
{
	if(CameraReplacementPtr)
	{
		D3DXMatrixInverse(invView, NULL, &CameraReplacementPtr->ViewReplacement);	
		return;
	}

	*invView = zCCamera::GetCamera()->GetTransform(zCCamera::ETransformType::TT_VIEW_INV);
	//*invView = *(oCGame::GetGame()->_zCSession_camVob->GetWorldMatrixPtr());
	//D3DXMatrixTranspose(invView, invView);
}

/** Returns the projection-matrix */
D3DXMATRIX& GothicAPI::GetProjectionMatrix()
{
	if(CameraReplacementPtr)
	{
		return CameraReplacementPtr->ProjectionReplacement;
	}

	return RendererState.TransformState.TransformProj;
}

/** Returns the GSky-Object */
GSky* GothicAPI::GetSky()
{
	return SkyRenderer;
}

/** Returns the inventory */
GInventory* GothicAPI::GetInventory()
{
	return Inventory;
}

/** Returns the fog-color */
D3DXVECTOR3 GothicAPI::GetFogColor()
{
	zCSkyController_Outdoor* sc = oCGame::GetGame()->_zCSession_world->GetSkyControllerOutdoor();

	// Only give the overridden color out if the flag is set
	if(!sc || !sc->GetOverrideFlag())
		return *RendererState.RendererSettings.FogColorMod.toD3DXVECTOR3();

	D3DXVECTOR3 color = sc->GetOverrideColor();

	// Clamp to length of 0.5f. Gothic does it at an intensity of 120 / 255.
	float len = D3DXVec3Length(&color);
	if(len > 0.5f)
	{
		D3DXVec3Normalize(&color, &color);

		color *= 0.5f;
		len = 0.5f;
	}

	// Mix these, so the fog won't get black at transitions
	D3DXVec3Lerp(&color, RendererState.RendererSettings.FogColorMod.toD3DXVECTOR3(), &color, len * 2.0f);

	return color;
}

/** Returns true if the game is overwriting the fog color with a fog-zone */
float GothicAPI::GetFogOverride()
{
	zCSkyController_Outdoor* sc = oCGame::GetGame()->_zCSession_world->GetSkyControllerOutdoor();

	// Catch invalid controller
	if(!sc)
		return 0.0f;

	return sc->GetOverrideFlag() ? std::min(D3DXVec3Length(&sc->GetOverrideColor()), 0.5f) * 2.0f : 0.0f;
}

/** Draws the inventory */
void GothicAPI::DrawInventory(zCWorld* world, zCCamera& camera)
{
	Inventory->DrawInventory(world, camera);
}

LRESULT CALLBACK GothicAPI::GothicWndProc(
	HWND hWnd,
	UINT msg,
		WPARAM wParam,
		LPARAM lParam
		)
{
	return Engine::GAPI->OnWindowMessage(hWnd, msg, wParam, lParam);
}

/** Message-Callback for the main window */
LRESULT GothicAPI::OnWindowMessage(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch(msg)
	{
	case WM_KEYDOWN:
		Engine::GraphicsEngine->OnKeyDown(wParam);
		switch(wParam)
		{
		case VK_F11:
#ifdef PUBLIC_RELEASE
			if (GetAsyncKeyState(VK_CONTROL))
			{
				Engine::AntTweakBar->SetActive(!Engine::AntTweakBar->GetActive());
				SetEnableGothicInput(!Engine::AntTweakBar->GetActive());
			}else
			{
				Engine::AntTweakBar->SetActive(false);
				SetEnableGothicInput(true);
				Engine::GraphicsEngine->OnUIEvent(BaseGraphicsEngine::EUIEvent::UI_OpenSettings);
			}
#else
			Engine::AntTweakBar->SetActive(!Engine::AntTweakBar->GetActive());
			SetEnableGothicInput(!Engine::AntTweakBar->GetActive());
#endif
			break;

		case VK_NUMPAD1:
			if(!Engine::AntTweakBar->GetActive())
				SpawnVegetationBoxAt(GetCameraPosition());
			break;

		case VK_NUMPAD2:
#ifdef PUBLIC_RELEASE
			if(!Engine::AntTweakBar->GetActive())
#endif
				Ocean->AddWaterPatchAt((unsigned int)(GetCameraPosition().x / OCEAN_PATCH_SIZE), (unsigned int)(GetCameraPosition().z / OCEAN_PATCH_SIZE));
			break;

		case VK_NUMPAD3:
#ifdef PUBLIC_RELEASE
			if(!Engine::AntTweakBar->GetActive())
#endif
			{
				for(int x=-1;x<=1;x++)
					for(int y=-1;y<=1;y++)
						Ocean->AddWaterPatchAt((unsigned int)((GetCameraPosition().x / OCEAN_PATCH_SIZE) + x), (unsigned int)((GetCameraPosition().z / OCEAN_PATCH_SIZE) + y));	
			}
			break;
		}
		break;

	}

	// This is only processed when the bar is activated, so just call this here
	Engine::AntTweakBar->OnWindowMessage(hWnd, msg, wParam, lParam);
	Engine::GraphicsEngine->OnWindowMessage(hWnd, msg, wParam, lParam);

	if(OriginalGothicWndProc)
	{
		return CallWindowProc((WNDPROC)OriginalGothicWndProc, hWnd, msg, wParam, lParam);
	}
	else
		return 0;
}

/** Recursive helper function to draw the BSP-Tree */
void GothicAPI::DebugDrawTreeNode(zCBspBase* base, zTBBox3D boxCell, int clipFlags)
{
	while(base)
	{
		if (clipFlags>0) 
		{
			float yMaxWorld = Engine::GAPI->GetLoadedWorldInfo()->BspTree->GetRootNode()->BBox3D.Max.y;

			zTBBox3D nodeBox = base->BBox3D;
			float nodeYMax = std::min(yMaxWorld, Engine::GAPI->GetCameraPosition().y);
			nodeYMax = std::max(nodeYMax, base->BBox3D.Max.y);
			nodeBox.Max.y = nodeYMax;

			zTCam_ClipType nodeClip = zCCamera::GetCamera()->BBox3DInFrustum(nodeBox, clipFlags); 

			if (nodeClip==ZTCAM_CLIPTYPE_OUT) 
				return; // Nothig to see here. Discard this node and the subtree
		}



		if(base->IsLeaf())
		{
			// Check if this leaf is inside the frustum
			if(clipFlags > 0) 
			{
				if(zCCamera::GetCamera()->BBox3DInFrustum(base->BBox3D, clipFlags) == ZTCAM_CLIPTYPE_OUT) 
					return;
			}

			//Engine::GraphicsEngine->GetLineRenderer()->AddAABBMinMax(base->BBox3D.Min, base->BBox3D.Max);
			return;
		}else
		{
			zCBspNode* node = (zCBspNode *)base;

			int	planeAxis = node->PlaneSignbits;

			boxCell.Min.y	= node->BBox3D.Min.y;
			boxCell.Max.y	= node->BBox3D.Min.y;

			zTBBox3D tmpbox = boxCell;
			if (D3DXVec3Dot(&node->Plane.Normal, &GetCameraPosition()) > node->Plane.Distance)
			{ 
				if(node->Front) 
				{
					((float *)&tmpbox.Min)[planeAxis] = node->Plane.Distance;
					DebugDrawTreeNode(node->Front, tmpbox, clipFlags);
				}

				((float *)&boxCell.Max)[planeAxis] = node->Plane.Distance;
				base = node->Back;

			} else 
			{
				if (node->Back ) 
				{
					((float *)&tmpbox.Max)[planeAxis] = node->Plane.Distance;
					DebugDrawTreeNode(node->Back, tmpbox, clipFlags);
				}

				((float *)&boxCell.Min)[planeAxis] = node->Plane.Distance;
				base = node->Front;
			}
		}
	}
}

/** Draws the AABB for the BSP-Tree using the line renderer*/
void GothicAPI::DebugDrawBSPTree()
{
	zCBspTree* tree = LoadedWorldInfo->BspTree;

	zCBspBase* root = tree->GetRootNode();

	// Recursively go through the tree and draw all nodes
	DebugDrawTreeNode(root, root->BBox3D);
}

/** Collects vobs using gothics BSP-Tree */
void GothicAPI::CollectVisibleVobs(std::vector<VobInfo *>& vobs, std::vector<VobLightInfo *>& lights)
{
	zCBspTree* tree = LoadedWorldInfo->BspTree;

	zCBspBase* rootBsp = tree->GetRootNode();
	BspVobInfo* root = &BspLeafVobLists[rootBsp];
	if(zCCamera::GetCamera())
		zCCamera::GetCamera()->Activate();

	/*for(auto it = VobMap.begin(); it != VobMap.end(); it++)
	{
		vobs.push_back((*it).second);

		VobInstanceInfo vii;
		vii.world = vobs.back()->WorldMatrix;
		((MeshVisualInfo *)(*it).second->VisualInfo)->Instances.push_back(vii);
	}

	return;*/

	// Recursively go through the tree and draw all nodes
	CollectVisibleVobsHelper(root, root->OriginalNode->BBox3D, 63, vobs, lights);

	D3DXVECTOR3 camPos = GetCameraPosition();
	float vobIndoorDist = Engine::GAPI->GetRendererState()->RendererSettings.IndoorVobDrawRadius;
	float vobOutdoorDist = Engine::GAPI->GetRendererState()->RendererSettings.OutdoorVobDrawRadius;
	float vobOutdoorSmallDist = Engine::GAPI->GetRendererState()->RendererSettings.OutdoorSmallVobDrawRadius;
	float vobSmallSize = Engine::GAPI->GetRendererState()->RendererSettings.SmallVobSize;

	// Add visible dynamically added vobs
	for(std::list<VobInfo*>::iterator it = DynamicallyAddedVobs.begin(); it != DynamicallyAddedVobs.end(); it++)
	{
		// Get distance to this vob
		float dist = D3DXVec3Length(&(camPos - (*it)->Vob->GetPositionWorld()));

		if(Engine::GAPI->GetRendererState()->RendererSettings.DrawVOBs)
		{
			// Draw, if in range
			if((*it)->VisualInfo && ((dist < vobIndoorDist && (*it)->IsIndoorVob) || 
				(dist < vobOutdoorSmallDist && (*it)->VisualInfo->MeshSize < vobSmallSize) || 
				(dist < vobOutdoorDist)))
			{
				//if(memcmp(&(*it)->LastRenderPosition, (*it)->Vob->GetPositionWorld(), sizeof(D3DXVECTOR3)) != 0)
				{
					(*it)->LastRenderPosition = (*it)->Vob->GetPositionWorld();
					(*it)->UpdateVobConstantBuffer();

					Engine::GAPI->GetRendererState()->RendererInfo.FrameVobUpdates++;
				}

				VobInstanceInfo vii;
				vii.world = (*it)->WorldMatrix;
				vii.color = (*it)->GroundColor;

				/*if((*it)->IsIndoorVob)
					vii.color = float4(DEFAULT_INDOOR_VOB_AMBIENT).ToDWORD();
				else
					vii.color = 0xFFFFFFFF;*/

				((MeshVisualInfo *)(*it)->VisualInfo)->Instances.push_back(vii);

				vobs.push_back(*it);
				(*it)->VisibleInRenderPass = true;
			}
		}
	}
}

/** Collects visible sections from the current camera perspective */
void GothicAPI::CollectVisibleSections(std::list<WorldMeshSectionInfo*>& sections)
{
	D3DXVECTOR3 camPos = Engine::GAPI->GetCameraPosition();
	INT2 camSection = WorldConverter::GetSectionOfPos(camPos);

	// run through every section and check for range and frustum
	int sectionViewDist = Engine::GAPI->GetRendererState()->RendererSettings.SectionDrawRadius;
	for(std::map<int, std::map<int, WorldMeshSectionInfo>>::iterator itx = WorldSections.begin(); itx != WorldSections.end(); itx++)
	{
		for(std::map<int, WorldMeshSectionInfo>::iterator ity = (*itx).second.begin(); ity != (*itx).second.end(); ity++)
		{
			WorldMeshSectionInfo& section = (*ity).second;

			// Simple range-check
			if(abs((*itx).first - camSection.x) < sectionViewDist && 
				abs((*ity).first - camSection.y) < sectionViewDist)
			{
				int flags = 15; // Frustum check, no farplane
				if(zCCamera::GetCamera()->BBox3DInFrustum(section.BoundingBox, flags) == ZTCAM_CLIPTYPE_OUT)
					continue;

				sections.push_back(&section);

				//Engine::GraphicsEngine->GetLineRenderer()->AddAABBMinMax((*ity).second.BoundingBox.Min, (*ity).second.BoundingBox.Max, D3DXVECTOR4(0,0,1,0.5f));
			}			
		}
	}
}

/** Moves the given vob from a BSP-Node to the dynamic vob list */
void GothicAPI::MoveVobFromBspToDynamic(VobInfo* vob)
{
	// Remove from all nodes
	for(int i=0;i<vob->ParentBSPNodes.size();i++)
	{
		BspVobInfo* node = vob->ParentBSPNodes[i];

		// Remove from possible lists
		for(std::vector<VobInfo *>::iterator it = node->IndoorVobs.begin(); it != node->IndoorVobs.end(); it++)
		{
			if((*it) == vob)
			{
				node->IndoorVobs.erase(it);
				break;
			}
		}

		for(std::vector<VobInfo *>::iterator it = node->SmallVobs.begin(); it != node->SmallVobs.end(); it++)
		{
			if((*it) == vob)
			{
				node->SmallVobs.erase(it);
				break;
			}
		}

		for(std::vector<VobInfo *>::iterator it = node->Vobs.begin(); it != node->Vobs.end(); it++)
		{
			if((*it) == vob)
			{
				node->Vobs.erase(it);
				break;
			}
		}
	}

	vob->ParentBSPNodes.clear();

	// Add to dynamic vob list
	DynamicallyAddedVobs.push_back(vob);
}

std::vector<VobInfo *>::iterator GothicAPI::MoveVobFromBspToDynamic(VobInfo* vob, std::vector<VobInfo *>* source)
{
	std::vector<VobInfo *>::iterator itn = source->end();
	std::vector<VobInfo *>::iterator itc;

	// Remove from all nodes
	for(int i=0;i<vob->ParentBSPNodes.size();i++)
	{
		BspVobInfo* node = vob->ParentBSPNodes[i];

		// Remove from possible lists
		for(std::vector<VobInfo *>::iterator it = node->IndoorVobs.begin(); it != node->IndoorVobs.end(); it++)
		{
			if((*it) == vob)
			{
				itc = node->IndoorVobs.erase(it);
				break;
			}
		}

		if(&node->IndoorVobs == source)
			itn = itc;

		for(std::vector<VobInfo *>::iterator it = node->SmallVobs.begin(); it != node->SmallVobs.end(); it++)
		{
			if((*it) == vob)
			{
				itc = node->SmallVobs.erase(it);
				break;
			}
		}

		if(&node->SmallVobs == source)
			itn = itc;

		for(std::vector<VobInfo *>::iterator it = node->Vobs.begin(); it != node->Vobs.end(); it++)
		{
			if((*it) == vob)
			{
				itc = node->Vobs.erase(it);
				break;
			}
		}

		if(&node->Vobs == source)
			itn = itc;
	}

	// Add to dynamic vob list
	DynamicallyAddedVobs.push_back(vob);

	return itn;
}

static void CVVH_AddNotDrawnVobToList(std::vector<VobInfo *>& target, std::vector<VobInfo *>& source, float dist)
{
	std::vector<VobInfo *> remVobs;

	for(std::vector<VobInfo *>::iterator it = source.begin(); it != source.end(); it++)
	{
		if(!(*it)->VisibleInRenderPass)
		{	
			VobInstanceInfo vii;

			float vd = D3DXVec3Length(&(Engine::GAPI->GetCameraPosition() - (*it)->LastRenderPosition));
			if(vd < dist && (*it)->Vob->GetShowVisual())
			{
				// Update if near and moved
				/*if(vd < dist / 4 && memcmp(&(*it)->LastRenderPosition, (*it)->Vob->GetPositionWorld(), sizeof(D3DXVECTOR3)) != 0)
				{
					(*it)->LastRenderPosition = (*it)->Vob->GetPositionWorld();
					(*it)->UpdateVobConstantBuffer();				

					Engine::GAPI->GetRendererState()->RendererInfo.FrameVobUpdates++;

					remVobs.push_back((*it));
			
					continue; // Render with the other dynamic vobs
				}*/

				VobInstanceInfo vii;
				vii.world = (*it)->WorldMatrix;
				vii.color = (*it)->GroundColor;

				//if((*it)->IsIndoorVob)
				//	vii.color = float4(DEFAULT_INDOOR_VOB_AMBIENT).ToDWORD();
				//else
				//	vii.color = 0xFFFFFFFF;

				((MeshVisualInfo *)(*it)->VisualInfo)->Instances.push_back(vii);
				target.push_back((*it));
				(*it)->VisibleInRenderPass = true;
			}
		}
	}

	/*for(std::vector<VobInfo *>::iterator it = remVobs.begin(); it != remVobs.end(); it++)
	{
		// Move to dynamic vob-list since these vobs have moved this frame
		Engine::GAPI->MoveVobFromBspToDynamic((*it));
	}*/
}

static void CVVH_AddNotDrawnVobToList(std::vector<VobLightInfo *>& target, std::vector<VobLightInfo *>& source, float dist)
{
	for(std::vector<VobLightInfo *>::iterator it = source.begin(); it != source.end(); it++)
	{
		if(!(*it)->VisibleInRenderPass)
		{
			if(D3DXVec3Length(&(Engine::GAPI->GetCameraPosition() - (*it)->Vob->GetPositionWorld())) - (*it)->Vob->GetLightRange() < dist)
			{
				//(*it)->VisualInfo->Instances.push_back((*it)->WorldMatrix);
				target.push_back((*it));
				(*it)->VisibleInRenderPass = true;
			}
		}
	}
}

/** Recursive helper function to draw collect the vobs */
void GothicAPI::CollectVisibleVobsHelper(BspVobInfo* base, zTBBox3D boxCell, int clipFlags, std::vector<VobInfo *>& vobs, std::vector<VobLightInfo *>& lights)
{
	float vobIndoorDist = Engine::GAPI->GetRendererState()->RendererSettings.IndoorVobDrawRadius;
	float vobOutdoorDist = Engine::GAPI->GetRendererState()->RendererSettings.OutdoorVobDrawRadius;
	float vobOutdoorSmallDist = Engine::GAPI->GetRendererState()->RendererSettings.OutdoorSmallVobDrawRadius;
	float vobSmallSize = Engine::GAPI->GetRendererState()->RendererSettings.SmallVobSize;
	float visualFXDrawRadius = Engine::GAPI->GetRendererState()->RendererSettings.VisualFXDrawRadius;
	D3DXVECTOR3 camPos = Engine::GAPI->GetCameraPosition();

	while(base->OriginalNode)
	{
		if (clipFlags>0) 
		{
			float yMaxWorld = Engine::GAPI->GetLoadedWorldInfo()->BspTree->GetRootNode()->BBox3D.Max.y;

			zTBBox3D nodeBox = base->OriginalNode->BBox3D;
			float nodeYMax = std::min(yMaxWorld, Engine::GAPI->GetCameraPosition().y);
			nodeYMax = std::max(nodeYMax, base->OriginalNode->BBox3D.Max.y);
			nodeBox.Max.y = nodeYMax;

			float dist = Toolbox::ComputePointAABBDistance(camPos, base->OriginalNode->BBox3D.Min, base->OriginalNode->BBox3D.Max);
			if(dist < vobOutdoorDist)
			{
				zTCam_ClipType nodeClip = zCCamera::GetCamera()->BBox3DInFrustum(nodeBox, clipFlags); 

				//float dist = D3DXVec3Length(&(base->BBox3D.Min - camPos));

				if (nodeClip==ZTCAM_CLIPTYPE_OUT) 
					return; // Nothig to see here. Discard this node and the subtree}
			}else
			{
				// Too far
				return;
			}
		}



		if(base->OriginalNode->IsLeaf())
		{
			// Check if this leaf is inside the frustum
			bool insideFrustum = true;
			if(clipFlags > 0) 
			{
				if(zCCamera::GetCamera()->BBox3DInFrustum(base->OriginalNode->BBox3D, clipFlags) == ZTCAM_CLIPTYPE_OUT) 
					insideFrustum = false;
			}
			zCBspLeaf* leaf = (zCBspLeaf *)base->OriginalNode;
			//BspVobInfo& bvi = BspLeafVobLists[leaf];
			std::vector<VobInfo *>& listA = base->IndoorVobs;
			std::vector<VobInfo *>& listB = base->SmallVobs;
			std::vector<VobInfo *>& listC = base->Vobs;
			std::vector<VobLightInfo *>& listL = base->Lights;
			std::vector<VobLightInfo *>& listL_Indoor = base->IndoorLights;

			// Concat the lists
			float dist = Toolbox::ComputePointAABBDistance(camPos, base->OriginalNode->BBox3D.Min, base->OriginalNode->BBox3D.Max);
			// float dist = D3DXVec3Length(&(base->BBox3D.Min - camPos));

			if(insideFrustum)
			{
				if(Engine::GAPI->GetRendererState()->RendererSettings.DrawVOBs)
				{
					if(dist < vobIndoorDist)
						CVVH_AddNotDrawnVobToList(vobs, listA, vobIndoorDist);

					if(dist < vobOutdoorSmallDist)
						CVVH_AddNotDrawnVobToList(vobs, listB, vobOutdoorSmallDist);
				}

				if(dist < vobOutdoorDist)
				{
					if(Engine::GAPI->GetRendererState()->RendererSettings.DrawVOBs)
					{
						CVVH_AddNotDrawnVobToList(vobs, listC, vobOutdoorDist);
					}

				}

				if(dist < vobOutdoorSmallDist)
				{
					/*if(RendererState.RendererSettings.EnableDynamicLighting)
					{
						if(!listL.empty() && Engine::GAPI->GetRendererState()->RendererSettings.EnableDynamicLighting)
							CVVH_AddNotDrawnVobToList(lights, listL, vobOutdoorSmallDist);

						if(dist < vobIndoorDist)
							CVVH_AddNotDrawnVobToList(lights, listL_Indoor, vobIndoorDist);
					}*/
				}
			}

			if(RendererState.RendererSettings.EnableDynamicLighting && insideFrustum)
			{
				// Add dynamic lights
				for(int i=0;i<leaf->LightVobList.NumInArray;i++)
				{
					if(D3DXVec3Length(&(Engine::GAPI->GetCameraPosition() - leaf->LightVobList.Array[i]->GetPositionWorld())) + leaf->LightVobList.Array[i]->GetLightRange() < visualFXDrawRadius)
					{
						zCVobLight* v = leaf->LightVobList.Array[i];
						VobLightInfo** vi = &VobLightMap[leaf->LightVobList.Array[i]];

						// Check if we already have this light
						if(!*vi)
						{
							// Add if not
							*vi = new VobLightInfo;
							(*vi)->Vob = leaf->LightVobList.Array[i];
						}

						if(!(*vi)->VisibleInRenderPass && (*vi)->Vob->IsEnabled())
						{
							(*vi)->VisibleInRenderPass = true;

							//D3DXVECTOR3 mid = base->BBox3D.Min * 0.5f + base->BBox3D.Max * 0.5f;
							//Engine::GraphicsEngine->GetLineRenderer()->AddLine(LineVertex((*vi)->Vob->GetPositionWorld()), LineVertex(mid));

							// Render it
							lights.push_back(*vi);
						}
					}else
					{
						//Engine::GraphicsEngine->GetLineRenderer()->AddPointLocator(leaf->LightVobList.Array[i]->GetPositionWorld(), 10.0f, D3DXVECTOR4(0,1,0,1));
					}
				}
			}
			//Engine::GraphicsEngine->GetLineRenderer()->AddAABBMinMax(base->BBox3D.Min, base->BBox3D.Max);
			return;
		}else
		{
			zCBspNode* node = (zCBspNode *)base->OriginalNode;

			int	planeAxis = node->PlaneSignbits;

			boxCell.Min.y	= node->BBox3D.Min.y;
			boxCell.Max.y	= node->BBox3D.Min.y;

			zTBBox3D tmpbox = boxCell;
			if (D3DXVec3Dot(&node->Plane.Normal, &GetCameraPosition()) > node->Plane.Distance)
			{ 
				if(node->Front) 
				{
					((float *)&tmpbox.Min)[planeAxis] = node->Plane.Distance;
					CollectVisibleVobsHelper(base->Front, tmpbox, clipFlags, vobs, lights);
				}

				((float *)&boxCell.Max)[planeAxis] = node->Plane.Distance;
				base = base->Back;

			} else 
			{
				if (node->Back ) 
				{
					((float *)&tmpbox.Max)[planeAxis] = node->Plane.Distance;
					CollectVisibleVobsHelper(base->Back, tmpbox, clipFlags, vobs, lights);
				}

				((float *)&boxCell.Min)[planeAxis] = node->Plane.Distance;
				base = base->Front;
			}
		}
	}
}

/** Helper function for going through the bsp-tree */
void GothicAPI::BuildBspVobMapCacheHelper(zCBspBase* base)
{
	if(!base)
		return;

	// Put it into the cache
	BspVobInfo& bvi = BspLeafVobLists[base];
	bvi.OriginalNode = base;

	if(base->IsLeaf())
	{
		zCBspLeaf* leaf = (zCBspLeaf *)base;

		
		bvi.Front = NULL;
		bvi.Back = NULL;
		

		for(int i=0;i<leaf->LeafVobList.NumInArray;i++)
		{
			// Get the vob info for this one
			if(VobMap.find(leaf->LeafVobList.Array[i]) != VobMap.end())
			{
				VobInfo* v = VobMap[leaf->LeafVobList.Array[i]];

				if(v)
				{				
					float vobSmallSize = Engine::GAPI->GetRendererState()->RendererSettings.SmallVobSize;

					if(v->Vob->GetGroundPoly() && v->Vob->GetGroundPoly()->GetLightmap())
					{
						// Only add once
						if(std::find(bvi.IndoorVobs.begin(), bvi.IndoorVobs.end(), v) == bvi.IndoorVobs.end())
						{
							v->ParentBSPNodes.push_back(&bvi);

							bvi.IndoorVobs.push_back(v);	
							v->IsIndoorVob = true;
						}
					}
					else if(v->VisualInfo->MeshSize < vobSmallSize)
					{
						// Only add once
						if(std::find(bvi.SmallVobs.begin(), bvi.SmallVobs.end(), v) == bvi.SmallVobs.end())
						{
							v->ParentBSPNodes.push_back(&bvi);

							bvi.SmallVobs.push_back(v);
						}
					}
					else
					{
						// Only add once
						if(std::find(bvi.Vobs.begin(), bvi.Vobs.end(), v) == bvi.Vobs.end())
						{
							v->ParentBSPNodes.push_back(&bvi);



							bvi.Vobs.push_back(v);
						}
					}
				}
			}

			for(int i=0;i<leaf->LightVobList.NumInArray;i++)
			{
				// Add the light to the map if not already done
				std::hash_map<zCVobLight*, VobLightInfo*>::iterator vit = VobLightMap.find(leaf->LightVobList.Array[i]);

				if(vit == VobLightMap.end())
				{
					VobLightInfo* vi = new VobLightInfo;
					vi->Vob = leaf->LightVobList.Array[i];
					VobLightMap[leaf->LightVobList.Array[i]] = vi;

					if(((zCVob *)vi->Vob)->GetGroundPoly() && ((zCVob *)vi->Vob)->GetGroundPoly()->GetLightmap())
					{
						vi->IsIndoorVob = true;
					}
				}


				VobLightInfo* vi = VobLightMap[leaf->LightVobList.Array[i]];
				if(!vi->IsIndoorVob)
				{
					// Only add once
					if(std::find(bvi.Lights.begin(), bvi.Lights.end(), vi) == bvi.Lights.end())
					{
						vi->ParentBSPNodes.push_back(&bvi);

						bvi.Lights.push_back(vi);
					}
				}
				else
				{
					// Only add once
					if(std::find(bvi.IndoorLights.begin(), bvi.IndoorLights.end(), vi) == bvi.IndoorLights.end())
					{
						vi->ParentBSPNodes.push_back(&bvi);

						bvi.IndoorLights.push_back(vi);
					}
				}

				//zCVob* vob = (zCVob *) leaf->LightVobList.Array[i];
				//Engine::GraphicsEngine->GetLineRenderer()->AddAABB(vob->GetPositionWorld(), D3DXVECTOR3(50,50,50));
			}

			bvi.NumStaticLights = leaf->LightVobList.NumInArray;
		}
	}else
	{
		zCBspNode* node = (zCBspNode *)base;

		bvi.OriginalNode = base;

		BuildBspVobMapCacheHelper(node->Front);
		BuildBspVobMapCacheHelper(node->Back);

		// Save front and back to this
		bvi.Front = &BspLeafVobLists[node->Front];
		bvi.Back = &BspLeafVobLists[node->Back];
	}
}

/** Builds our BspTreeVobMap */
void GothicAPI::BuildBspVobMapCache()
{
	BuildBspVobMapCacheHelper(LoadedWorldInfo->BspTree->GetRootNode());
}

/** Cleans empty BSPNodes */
void GothicAPI::CleanBSPNodes()
{
	for(std::hash_map<zCBspBase *, BspVobInfo>::iterator it = BspLeafVobLists.begin(); it != BspLeafVobLists.end(); it++)
	{
		if((*it).second.IsEmpty())
			it = BspLeafVobLists.erase(it);
	}
}

/** Returns the new node from tha base node */
BspVobInfo* GothicAPI::GetNewBspNode(zCBspBase* base)
{
	return &BspLeafVobLists[base];
}

/** Disables a problematic method which causes the game to conflict with other applications on startup */
void GothicAPI::DisableErrorMessageBroadcast()
{
#ifndef BUILD_GOTHIC_1_08k
	LogInfo() << "Disabling global message broadcast";

	// Unlock the memory region
	DWORD dwProtect;
	unsigned int size = GothicMemoryLocations::zERROR::BroadcastEnd - GothicMemoryLocations::zERROR::BroadcastStart;
	VirtualProtect((void *)GothicMemoryLocations::zERROR::BroadcastStart, size, PAGE_EXECUTE_READWRITE, &dwProtect);

	// NOP it
	REPLACE_RANGE(GothicMemoryLocations::zERROR::BroadcastStart, GothicMemoryLocations::zERROR::BroadcastEnd-1, INST_NOP);
#endif

}

/** Sets/Gets the far-plane */
void GothicAPI::SetFarPlane(float value)
{
	zCCamera::GetCamera()->SetFarPlane(value);
	zCCamera::GetCamera()->Activate();
}

float GothicAPI::GetFarPlane()
{
	return zCCamera::GetCamera()->GetFarPlane();
}

/** Sets/Gets the far-plane */
void GothicAPI::SetNearPlane(float value)
{
	LogWarn() << "SetNearPlane not implemented yet!";
	
}

float GothicAPI::GetNearPlane()
{
	return zCCamera::GetCamera()->GetNearPlane();
}

/** Get material by texture name */
zCMaterial* GothicAPI::GetMaterialByTextureName(const std::string& name)
{
	for(std::set<zCMaterial *>::iterator it = LoadedMaterials.begin(); it != LoadedMaterials.end(); it++)
	{
		if((*it)->GetTexture())
		{
			std::string tn = (*it)->GetTexture()->GetNameWithoutExt();
			if(_strnicmp(name.c_str(), tn.c_str(), 255)==0)
				return (*it);
		}
	}

	return NULL;
}

void GothicAPI::GetMaterialListByTextureName(const std::string& name, std::list<zCMaterial*>& list)
{
	for(std::set<zCMaterial *>::iterator it = LoadedMaterials.begin(); it != LoadedMaterials.end(); it++)
	{
		if((*it)->GetTexture())
		{
			std::string tn = (*it)->GetTexture()->GetNameWithoutExt();
			if(_strnicmp(name.c_str(), tn.c_str(), 255)==0)
				list.push_back((*it));
		}
	}
}

/** Returns the time since the last frame */
float GothicAPI::GetDeltaTime()
{
	zCTimer* timer = zCTimer::GetTimer();

	return timer->frameTimeFloat / 1000.0f;
}

/** Sets the current texture test bind mode status */
void GothicAPI::SetTextureTestBindMode(bool enable, const std::string& currentTexture)
{
	TextureTestBindMode = enable;

	if(enable)
		BoundTestTexture = currentTexture;
}

/** If this returns true, the property holds the name of the currently bound texture. If that is the case, any MyDirectDrawSurfaces should not bind themselfes
to the pipeline, but rather check if there are additional textures to load */
bool GothicAPI::IsInTextureTestBindMode(std::string& currentBoundTexture)
{
	if(TextureTestBindMode)
		currentBoundTexture = BoundTestTexture;

	return TextureTestBindMode;
}

/** Lets Gothic draw its sky */
void GothicAPI::DrawSkyGothicOriginal()
{
	HookedFunctions::OriginalFunctions.original_zCWorldRender(oCGame::GetGame()->_zCSession_world, *zCCamera::GetCamera());
}

/** Returns the material info associated with the given material */
MaterialInfo* GothicAPI::GetMaterialInfoFrom(zCTexture* tex)
{
	std::hash_map<zCTexture*, MaterialInfo>::iterator f = MaterialInfos.find(tex);
	if(f == MaterialInfos.end() && tex)
	{
		// Make a new one and try to load it
		MaterialInfos[tex].LoadFromFile(tex->GetNameWithoutExt());
	}

	return &MaterialInfos[tex];
}

/** Adds a surface */
void GothicAPI::AddSurface(const std::string& name, MyDirectDrawSurface7* surface)
{
	SurfacesByName[name] = surface;
}

/** Gets a surface by texturename */
MyDirectDrawSurface7* GothicAPI::GetSurface(const std::string& name)
{
	return SurfacesByName[name];
}

/** Removes a surface */
void GothicAPI::RemoveSurface(MyDirectDrawSurface7* surface)
{
	SurfacesByName.erase(surface->GetTextureName());
}

/** Returns the loaded skeletal mesh vobs */
std::list<SkeletalVobInfo *>& GothicAPI::GetSkeletalMeshVobs()
{
	return SkeletalMeshVobs;
}

/** Resets all vob-stats drawn this frame */
void GothicAPI::ResetVobFrameStats(std::list<VobInfo *>& vobs)
{
	for(std::list<VobInfo *>::iterator it = vobs.begin(); it != vobs.end();it++)
	{
		(*it)->VisibleInRenderPass = false;
	}
}

/** Sets the currently bound texture */
void GothicAPI::SetBoundTexture(int idx, zCTexture* tex)
{
	BoundTextures[idx] = tex;
}

zCTexture* GothicAPI::GetBoundTexture(int idx)
{
	return BoundTextures[idx];
}

/** Teleports the player to the given location */
void GothicAPI::SetPlayerPosition(const D3DXVECTOR3& pos)
{
	if(oCGame::GetPlayer())
		oCGame::GetPlayer()->ResetPos(pos);
}

/** Loads resources created for this .ZEN */
void GothicAPI::LoadCustomZENResources()
{
	std::string zen = "system\\GD3D11\\ZENResources\\" + LoadedWorldInfo->WorldName;

	LogInfo() << "Loading custom ZEN-Resources from: " << zen;

	// Suppressed Textures
	LoadSuppressedTextures(zen + ".spt");

	// Load vegetation
	LoadVegetation(zen + ".veg");

	// Load world mesh information
	LoadSectionInfos();
}

/** Saves resources created for this .ZEN */
void GothicAPI::SaveCustomZENResources()
{
	std::string zen = "system\\GD3D11\\ZENResources\\" + LoadedWorldInfo->WorldName;

	LogInfo() << "Saving custom ZEN-Resources to: " << zen;

	// Suppressed Textures
	SaveSuppressedTextures(zen + ".spt");

	// Save vegetation
	SaveVegetation(zen + ".veg");

	// Save world mesh information
	SaveSectionInfos();
}

/** Applys the suppressed textures */
void GothicAPI::ApplySuppressedSectionTextures()
{
	for(std::map<WorldMeshSectionInfo*, std::vector<std::string>>::iterator it = SuppressedTexturesBySection.begin(); it != SuppressedTexturesBySection.end(); it++)
	{
		WorldMeshSectionInfo* section = (*it).first;

		// Look into each mesh of this section and find the texture
		for(std::map<MeshKey, WorldMeshInfo*>::iterator mit = section->WorldMeshes.begin(); mit != section->WorldMeshes.end();mit++)
		{
			for(unsigned int i=0;i<(*it).second.size();i++)
			{
				// Is this the texture we are looking for?
				if((*mit).first.Material && (*mit).first.Material->GetTexture() && (*mit).first.Material->GetTexture()->GetNameWithoutExt() == (*it).second[i])
				{
					// Yes, move it to the suppressed map
					section->SuppressedMeshes[(*mit).first] = (*mit).second;
					section->WorldMeshes.erase(mit);
					break;
				}
			}
		}
	}
}

/** Resets the suppressed textures */
void GothicAPI::ResetSupressedTextures()
{
	for(std::map<WorldMeshSectionInfo*, std::vector<std::string>>::iterator it = SuppressedTexturesBySection.begin(); it != SuppressedTexturesBySection.end(); it++)
	{
		WorldMeshSectionInfo* section = (*it).first;

		// Look into each mesh of this section and find the texture
		for(std::map<MeshKey, WorldMeshInfo*>::iterator mit = section->WorldMeshes.begin(); mit != section->WorldMeshes.end();mit++)
		{
			section->WorldMeshes[(*mit).first] = (*mit).second;
		}
	}

	SuppressedTexturesBySection.clear();
}

/** Resets the vegetation */
void GothicAPI::ResetVegetation()
{
	for(std::list<GVegetationBox *>::iterator it = VegetationBoxes.begin(); it != VegetationBoxes.end();it++)
	{
		delete (*it);
	}
	VegetationBoxes.clear();
}


/** Removes the given texture from the given section and stores the supression, so we can load it next time */
void GothicAPI::SupressTexture(WorldMeshSectionInfo* section, const std::string& texture)
{
	SuppressedTexturesBySection[section].push_back(texture);

	ApplySuppressedSectionTextures(); // This is an editor only feature, so it's okay to "not be blazing fast"
}

/** Saves Suppressed textures to a file */
XRESULT GothicAPI::SaveSuppressedTextures(const std::string& file)
{
	FILE* f = fopen(file.c_str(), "wb");

	LogInfo() << "Saving suppressed textures";

	if(!f)
		return XR_FAILED;

	int version = 1;
	fwrite(&version, sizeof(version), 1, f);

	int count = SuppressedTexturesBySection.size();
	fwrite(&count, sizeof(count), 1, f);

	for(std::map<WorldMeshSectionInfo*, std::vector<std::string>>::iterator it = SuppressedTexturesBySection.begin(); it != SuppressedTexturesBySection.end(); it++)
	{
		// Write section xy-coords
		fwrite(&(*it).first->WorldCoordinates, sizeof(INT2), 1, f);

		int countTX = (*it).second.size();
		fwrite(&countTX, sizeof(countTX), 1, f);

		for(int i=0;i<countTX;i++)
		{
			unsigned int numChars = (*it).second[i].size();
			numChars = std::min(255, (int)numChars);

			// Write num of chars
			fwrite(&numChars, sizeof(numChars), 1, f);

			// Write chars
			fwrite(&(*it).second[0], numChars, 1, f);
		}
	}

	fclose(f);

	return XR_SUCCESS;
}

/** Saves Suppressed textures to a file */
XRESULT GothicAPI::LoadSuppressedTextures(const std::string& file)
{
	FILE* f = fopen(file.c_str(), "rb");

	LogInfo() << "Loading Suppressed textures";

	// Clean first
	ResetSupressedTextures();

	if(!f)
		return XR_FAILED;

	int version;
	fread(&version, sizeof(version), 1, f);

	int count;
	fread(&count, sizeof(count), 1, f);


	for(int c=0;c<count;c++)
	{
		int countTX;
		fread(&countTX, sizeof(countTX), 1, f);

		for(int i=0;i<countTX;i++)
		{
			// Read section xy-coords
			INT2 coords;
			fread(&coords, sizeof(INT2), 1, f);

			// Read num of chars
			unsigned int numChars;
			fread(&numChars, sizeof(numChars), 1, f);

			// Read chars
			char name[256];
			memset(name, 0, 256);
			fread(name, numChars, 1, f);

			// Add to map
			SuppressedTexturesBySection[&WorldSections[coords.x][coords.y]].push_back(std::string(name));
		}
	}

	fclose(f);

	// Apply the whole thing
	ApplySuppressedSectionTextures();

	return XR_SUCCESS;
}

/** Saves vegetation to a file */
XRESULT GothicAPI::SaveVegetation(const std::string& file)
{
	FILE* f = fopen(file.c_str(), "wb");

	LogInfo() << "Saving vegetation";

	if(!f)
		return XR_FAILED;

	int version = 1;
	fwrite(&version, sizeof(version), 1, f);

	int num = VegetationBoxes.size();
	fwrite(&num, sizeof(num), 1, f);

	for(std::list<GVegetationBox *>::iterator it = VegetationBoxes.begin(); it != VegetationBoxes.end();it++)
	{
		(*it)->SaveToFILE(f, version);
	}

	fclose(f);

	return XR_SUCCESS;
}

/** Saves vegetation to a file */
XRESULT GothicAPI::LoadVegetation(const std::string& file)
{
	FILE* f = fopen(file.c_str(), "rb");

	LogInfo() << "Loading vegetation";

	// Reset first
	ResetVegetation();

	if(!f)
		return XR_FAILED;

	int version;
	fread(&version, sizeof(version), 1, f);

	int num = VegetationBoxes.size();
	fread(&num, sizeof(num), 1, f);

	for(int i=0;i<num;i++)
	{
		GVegetationBox* b = new GVegetationBox;
		b->LoadFromFILE(f, version);

		AddVegetationBox(b);
	}

	fclose(f);

	return XR_SUCCESS;
}

/** Saves the users settings from the menu */
XRESULT GothicAPI::SaveMenuSettings(const std::string& file)
{
	FILE* f = fopen(file.c_str(), "wb");

	LogInfo() << "Saving menu settings";

	if(!f)
		return XR_FAILED;

	GothicRendererSettings& s = RendererState.RendererSettings;

	int version = 1;
	fwrite(&version, sizeof(version), 1, f);

	fwrite(&s.EnableShadows, sizeof(s.EnableShadows), 1, f);
	fwrite(&s.EnableSoftShadows, sizeof(s.EnableSoftShadows), 1, f);
	fwrite(&s.ShadowMapSize, sizeof(s.ShadowMapSize), 1, f);
	fwrite(&s.WorldShadowRangeScale, sizeof(s.WorldShadowRangeScale), 1, f);

	INT2 res = Engine::GraphicsEngine->GetResolution();
	fwrite(&res, sizeof(res), 1, f);

	fwrite(&s.EnableHDR, sizeof(s.EnableHDR), 1, f);
	fwrite(&s.EnableVSync, sizeof(s.EnableVSync), 1, f);

	fwrite(&s.OutdoorVobDrawRadius, sizeof(s.OutdoorVobDrawRadius), 1, f);
	fwrite(&s.OutdoorSmallVobDrawRadius, sizeof(s.OutdoorSmallVobDrawRadius), 1, f);

	fwrite(&s.FOVHoriz, sizeof(s.FOVHoriz), 1, f);
	fwrite(&s.FOVVert, sizeof(s.FOVVert), 1, f);

	fwrite(&s.SectionDrawRadius, sizeof(s.SectionDrawRadius), 1, f);

	fwrite(&s.HbaoSettings.Enabled, sizeof(s.HbaoSettings.Enabled), 1, f);

	fwrite(&s.EnableAutoupdates, sizeof(s.EnableAutoupdates), 1, f);

	fwrite(&s.GammaValue, sizeof(s.GammaValue), 1, f);
	fwrite(&s.BrightnessValue, sizeof(s.BrightnessValue), 1, f);

	fwrite(&s.EnableGodRays, sizeof(s.EnableGodRays), 1, f);
	fwrite(&s.EnableTesselation, sizeof(s.EnableTesselation), 1, f);
	fwrite(&s.EnableSMAA, sizeof(s.EnableSMAA), 1, f);

	fclose(f);

	return XR_SUCCESS;
}

/** Loads the users settings from the menu */
XRESULT GothicAPI::LoadMenuSettings(const std::string& file)
{
	FILE* f = fopen(file.c_str(), "rb");

	LogInfo() << "Saving menu settings";

	if(!f)
		return XR_FAILED;

	
	GothicRendererSettings& s = RendererState.RendererSettings;

	int version;
	fread(&version, sizeof(version), 1, f);

	fread(&s.EnableShadows, sizeof(s.EnableShadows), 1, f);
	fread(&s.EnableSoftShadows, sizeof(s.EnableSoftShadows), 1, f);
	fread(&s.ShadowMapSize, sizeof(s.ShadowMapSize), 1, f);
	fread(&s.WorldShadowRangeScale, sizeof(s.WorldShadowRangeScale), 1, f);

	// Fix the shadow range
	switch(s.ShadowMapSize)
	{
	case 512:
		s.WorldShadowRangeScale = 16.0f;
		break;

	case 1024:
		s.WorldShadowRangeScale = 16.0f;
		break;

	case 2048:
		s.WorldShadowRangeScale = 8.0f;
		break;

	case 4096:
		s.WorldShadowRangeScale = 4.0f;
		break;
	}


	INT2 res;
	fread(&res, sizeof(res), 1, f);
	s.LoadedResolution = res;

	fread(&s.EnableHDR, sizeof(s.EnableHDR), 1, f);
	s.EnableHDR = false; // FIXME: Force HDR to off for now

	fread(&s.EnableVSync, sizeof(s.EnableVSync), 1, f);

	fread(&s.OutdoorVobDrawRadius, sizeof(s.OutdoorVobDrawRadius), 1, f);
	fread(&s.OutdoorSmallVobDrawRadius, sizeof(s.OutdoorSmallVobDrawRadius), 1, f);

	fread(&s.FOVHoriz, sizeof(s.FOVHoriz), 1, f);
	fread(&s.FOVVert, sizeof(s.FOVVert), 1, f);

	fread(&s.SectionDrawRadius, sizeof(s.SectionDrawRadius), 1, f);

	fread(&s.HbaoSettings.Enabled, sizeof(s.HbaoSettings.Enabled), 1, f);

	fread(&s.EnableAutoupdates, sizeof(s.EnableAutoupdates), 1, f);

	fread(&s.GammaValue, sizeof(s.GammaValue), 1, f);
	fread(&s.BrightnessValue, sizeof(s.BrightnessValue), 1, f);

	fread(&s.EnableGodRays, sizeof(s.EnableGodRays), 1, f);
	fread(&s.EnableTesselation, sizeof(s.EnableTesselation), 1, f);

	fread(&s.EnableSMAA, sizeof(s.EnableSMAA), 1, f);

	fclose(f);

	return XR_SUCCESS;
}

void GothicAPI::SetPendingMovieFrame(BaseTexture* frame)
{
	PendingMovieFrame = frame;
}

BaseTexture* GothicAPI::GetPendingMovieFrame()
{
	return PendingMovieFrame;
}

/** Returns the main-thread id */
DWORD GothicAPI::GetMainThreadID()
{
	return MainThreadID;
}

/** Returns the current cursor position, in pixels */
POINT GothicAPI::GetCursorPosition()
{
	POINT p;
	GetCursorPos(&p);
	ScreenToClient(OutputWindow, &p);

	RECT r;
	GetClientRect(OutputWindow, &r);

	float x = (float)p.x / r.right;
	float y = (float)p.y / r.bottom;

	p.x = (long)(x * (float)Engine::GraphicsEngine->GetBackbufferResolution().x);
	p.y = (long)(y * (float)Engine::GraphicsEngine->GetBackbufferResolution().y);

	return p;
}

/** Clears the array of this frame loaded textures */
void GothicAPI::ClearFrameLoadedTextures()
{
	Engine::GAPI->EnterResourceCriticalSection();
		FrameLoadedTextures.clear();
	Engine::GAPI->LeaveResourceCriticalSection();
}

/** Adds a texture to the list of the loaded textures for this frame */
void GothicAPI::AddFrameLoadedTexture(MyDirectDrawSurface7* srf)
{
	Engine::GAPI->EnterResourceCriticalSection();	
		FrameLoadedTextures.push_back(srf);
	Engine::GAPI->LeaveResourceCriticalSection();
}

/** Sets loaded textures of this frame ready */
void GothicAPI::SetFrameProcessedTexturesReady()
{
	for(unsigned int i=0;i<FrameProcessedTextures.size();i++)
	{
		if(FrameProcessedTextures[i]->MipMapsInQueue()) // Only set ready when all mips are ready as well
			FrameProcessedTextures[i]->SetReady(true);
	}

	FrameProcessedTextures.clear();
}

/** Copys the frame loaded textures to the processed list */
void GothicAPI::MoveLoadedTexturesToProcessedList()
{
	FrameProcessedTextures = FrameLoadedTextures;
}

/** Draws a morphmesh */
void GothicAPI::DrawMorphMesh(zCMorphMesh* msh, float fatness)
{
	D3DXVECTOR3 tri0, tri1, tri2;
	D3DXVECTOR2	uv0, uv1, uv2;
	D3DXVECTOR3 bbmin, bbmax;
	bbmin = D3DXVECTOR3(FLT_MAX, FLT_MAX, FLT_MAX);
	bbmax = D3DXVECTOR3(-FLT_MAX, -FLT_MAX, -FLT_MAX);

	zCProgMeshProto* morphMesh = msh->GetMorphMesh();

	if(!morphMesh)
		return;

	D3DXVECTOR3* posList = (D3DXVECTOR3 *)morphMesh->GetPositionList()->Array;

	msh->AdvanceAnis();
	msh->CalcVertexPositions();

	// Construct unindexed mesh
	for(int i=0; i < morphMesh->GetNumSubmeshes(); i++)
	{
		std::vector<ExVertexStruct> vertices;

		zCSubMesh& sub = morphMesh->GetSubmeshes()[i];
		vertices.reserve(sub.TriList.NumInArray * 3);

		// Get vertices
		for(int t=0;t<sub.TriList.NumInArray;t++)
		{				
			for(int v=0;v<3;v++)
			{
				ExVertexStruct vx;
				vx.Position = posList[sub.WedgeList.Array[morphMesh->GetSubmeshes()[i].TriList.Array[t].wedge[v]].position];
				vx.TexCoord	= morphMesh->GetSubmeshes()[i].WedgeList.Array[sub.TriList.Array[t].wedge[v]].texUV;
				vx.Color = 0xFFFFFFFF;
				vx.Normal = morphMesh->GetSubmeshes()[i].WedgeList.Array[sub.TriList.Array[t].wedge[v]].normal;

				// Do this on GPU probably?
				*vx.Position.toD3DXVECTOR3() += (*vx.Normal.toD3DXVECTOR3()) * fatness;

				vertices.push_back(vx);
			}	
		}

		/*std::vector<VERTEX_INDEX> indices;
		indices.reserve(sub.TriList.NumInArray * 3);

		// Get vertices
		for(int t=0;t<sub.TriList.NumInArray;t++)
		{				
			for(int v=0;v<3;v++)
			{
				indices.push_back(sub.TriList.Array[t].wedge[v]);
			}
		}

		for(int v=0;v<indices.size();v++)
		{
			ExVertexStruct vx;
			vx.Position = posList[sub.WedgeList.Array[indices[v]].position];
			vx.TexCoord	= sub.WedgeList.Array[indices[v]].texUV;
			vx.Color = 0xFFFFFFFF;
			vx.Normal = sub.WedgeList.Array[indices[v]].normal;

			vertices.push_back(vx);
		}*/

		morphMesh->GetSubmeshes()[i].Material->BindTexture(0);

		Engine::GraphicsEngine->DrawVertexArray(&vertices[0], vertices.size());
	}
}

/** Removes the given quadmark */
void GothicAPI::RemoveQuadMark(zCQuadMark* mark)
{
	QuadMarks.erase(mark);
}

/** Returns the quadmark info for the given mark */
QuadMarkInfo* GothicAPI::GetQuadMarkInfo(zCQuadMark* mark)
{
	return &QuadMarks[mark];
}



/** Returns all quad marks */
const stdext::hash_map<zCQuadMark*, QuadMarkInfo>& GothicAPI::GetQuadMarks()
{
	return QuadMarks;
}

/** Returns wether the camera is underwater or not */
bool GothicAPI::IsUnderWater()
{
	if( oCGame::GetGame() && 
		oCGame::GetGame()->_zCSession_world &&
		oCGame::GetGame()->_zCSession_world->GetSkyControllerOutdoor())
	{
		return oCGame::GetGame()->_zCSession_world->GetSkyControllerOutdoor()->GetUnderwaterFX() != 0;
	}

	return false;
}

/** Saves all sections information */
void GothicAPI::SaveSectionInfos()
{
	for(std::map<int, std::map<int, WorldMeshSectionInfo>>::iterator itx = Engine::GAPI->GetWorldSections().begin(); itx != Engine::GAPI->GetWorldSections().end(); itx++)
	{
		for(std::map<int, WorldMeshSectionInfo>::iterator ity = (*itx).second.begin(); ity != (*itx).second.end(); ity++)
		{
			WorldMeshSectionInfo& section = (*ity).second;

			// Save this section to file
			section.SaveMeshInfos(LoadedWorldInfo->WorldName, INT2((*itx).first, (*ity).first));
		}
	}
}

/** Loads all sections information */
void GothicAPI::LoadSectionInfos()
{
	for(std::map<int, std::map<int, WorldMeshSectionInfo>>::iterator itx = Engine::GAPI->GetWorldSections().begin(); itx != Engine::GAPI->GetWorldSections().end(); itx++)
	{
		for(std::map<int, WorldMeshSectionInfo>::iterator ity = (*itx).second.begin(); ity != (*itx).second.end(); ity++)
		{
			WorldMeshSectionInfo& section = (*ity).second;

			// Load this section from file
			section.LoadMeshInfos(LoadedWorldInfo->WorldName, INT2((*itx).first, (*ity).first));
		}
	}
}

/** Returns if the given vob is registered in the world */
SkeletalVobInfo* GothicAPI::GetSkeletalVobByVob(zCVob* vob)
{
	return SkeletalVobMap[vob];
}

/** Returns true if the given string can be found in the commandline */
bool GothicAPI::HasCommandlineParameter(const std::string& param)
{
	return zCOption::GetOptions()->IsParameter(param);
}

/** Gets the int-param from the ini. String must be UPPERCASE. */
int GothicAPI::GetIntParamFromConfig(const std::string& param)
{
	return ConfigIntValues[param];
}

/** Sets the given int param into the internal ini-cache. That does not set the actual value for the game! */
void GothicAPI::SetIntParamFromConfig(const std::string& param, int value)
{
	ConfigIntValues[param] = value;
}

/** Returns the frame particle info collected from all DrawParticleFX-Calls */
std::map<zCTexture*, ParticleRenderInfo>& GothicAPI::GetFrameParticleInfo()
{
	return FrameParticleInfo;
}

/** Checks if the normalmaps are right */
bool GothicAPI::CheckNormalmapFiles()
{
	/** If the directory is empty, FindFirstFile() will only find the entry for 
		the directory itself (".") and FindNextFile() will fail with ERROR_FILE_NOT_FOUND. **/

	WIN32_FIND_DATAA data;
	HANDLE f = FindFirstFile("system\\GD3D11\\Textures\\Replacements\\*.dds", &data);
	if(!FindNextFile(f, &data))
	{
		// Inform the user that he is missing normalmaps
		MessageBoxA(NULL, "You don't seem to have any normalmaps installed. Please make sure you have put all DDS-Files from the package into the right folder:\n"
						  "system\\GD3D11\\Textures\\Replacements\n\n"
						  "If you don't know where to get the package, you can download them from:\n"
						  "http://www.gothic-dx11.de/download/replacements_dds.7z\n\n"
						  "The link has been copied to your clipboard.", "Normalmaps missing", MB_OK | MB_ICONINFORMATION);

		// Put the link into the clipboard
		clipput("http://www.gothic-dx11.de/download/replacements_dds.7z\n\n");
		return false;
	}

	FindClose(f);

	return true;
}

/** Returns the gamma value from the ingame menu */
float GothicAPI::GetGammaValue()
{
	return RendererState.RendererSettings.GammaValue;
	//return zCRndD3D::GetRenderer()->GetGammaValue();
}

/** Returns the brightness value from the ingame menu */
float GothicAPI::GetBrightnessValue()
{
	return RendererState.RendererSettings.BrightnessValue;
}

/** Puts the custom-polygons into the bsp-tree */
void GothicAPI::PutCustomPolygonsIntoBspTree()
{
	PutCustomPolygonsIntoBspTreeRec(&BspLeafVobLists[LoadedWorldInfo->BspTree->GetRootNode()]);
}

void GothicAPI::PutCustomPolygonsIntoBspTreeRec(BspVobInfo* base)
{
	if(!base || !base->OriginalNode)
		return;

	if(base->OriginalNode->IsLeaf())
	{
		// Get all sections this nodes intersects with
		std::vector<WorldMeshSectionInfo*> sections;
		GetIntersectingSections(base->OriginalNode->BBox3D.Min, base->OriginalNode->BBox3D.Max, sections);

		for(int i=0;i<sections.size();i++)
		{
			for(int p=0;p<sections[i]->SectionPolygons.size();p++)
			{
				zCPolygon* poly = sections[i]->SectionPolygons[p];

				if(!poly->GetMaterial() || // Skip stuff with alpha-channel or not set material
					poly->GetMaterial()->HasAlphaTest())
					continue;


				// Check all triangles
				for(int v=0;v<poly->GetNumPolyVertices();v++)
				{
					// Check if one vertex is inside the node // FIXME: This will fail for very large triangles!
					zCVertex** vx = poly->getVertices();

					if(Toolbox::PositionInsideBox(	*vx[v]->Position.toD3DXVECTOR3(), 
						base->OriginalNode->BBox3D.Min, 
						base->OriginalNode->BBox3D.Max))
					{
						base->NodePolygons.push_back(poly);
						break;
					}
				}

				//base->OriginalNode->PolyList = &base->NodePolygons[0];
				//base->OriginalNode->NumPolys = base->NodePolygons.size();
			}
		}

	}else
	{
		PutCustomPolygonsIntoBspTreeRec(base->Front);
		PutCustomPolygonsIntoBspTreeRec(base->Back);
	}
}

/** Returns the sections intersecting the given boundingboxes */
void GothicAPI::GetIntersectingSections(const D3DXVECTOR3& min, const D3DXVECTOR3& max, std::vector<WorldMeshSectionInfo*>& sections)
{
	for(std::map<int, std::map<int, WorldMeshSectionInfo>>::iterator itx = Engine::GAPI->GetWorldSections().begin(); itx != Engine::GAPI->GetWorldSections().end(); itx++)
	{
		for(std::map<int, WorldMeshSectionInfo>::iterator ity = (*itx).second.begin(); ity != (*itx).second.end(); ity++)
		{
			WorldMeshSectionInfo& section = (*ity).second;

			if(Toolbox::AABBsOverlapping(section.BoundingBox.Min, section.BoundingBox.Max, min, max))
			{
				sections.push_back(&section);
			}
		}
	}
}

/** Generates zCPolygons for the loaded sections */
void GothicAPI::CreatezCPolygonsForSections()
{
	for(std::map<int, std::map<int, WorldMeshSectionInfo>>::iterator itx = Engine::GAPI->GetWorldSections().begin(); itx != Engine::GAPI->GetWorldSections().end(); itx++)
	{
		for(std::map<int, WorldMeshSectionInfo>::iterator ity = (*itx).second.begin(); ity != (*itx).second.end(); ity++)
		{
			WorldMeshSectionInfo& section = (*ity).second;

			for(auto it = section.WorldMeshes.begin(); it != section.WorldMeshes.end();it++)
			{
				if(!(*it).first.Material ||
					(*it).first.Material->HasAlphaTest())
					continue;

				(*it).first.Material->SetAlphaFunc(zMAT_ALPHA_FUNC_FUNC_NONE);

				WorldConverter::ConvertExVerticesTozCPolygons((*it).second->Vertices, (*it).second->Indices, (*it).first.Material, section.SectionPolygons);
			}
		}
	}
}

/** Collects polygons in the given AABB */
void GothicAPI::CollectPolygonsInAABB(const zTBBox3D& bbox, zCPolygon **& polyList, int& numFound)
{
	static std::vector<zCPolygon*> list; // This function is defined to only temporary hold the found polygons in the game. 
										 // This is ugly, but that's how they do it.
	list.clear();

	CollectPolygonsInAABBRec(&BspLeafVobLists[LoadedWorldInfo->BspTree->GetRootNode()], bbox, list);

	// Give out data to calling function
	polyList = &list[0];
	numFound = list.size();
}

/** Collects polygons in the given AABB */
void GothicAPI::CollectPolygonsInAABBRec(BspVobInfo* base, const zTBBox3D& bbox, std::vector<zCPolygon *>& list)
{
	zCBspNode* node = (zCBspNode*)base->OriginalNode;

	while(node) 
	{
		if(node->IsLeaf()) 
		{
			zCBspLeaf *leaf = (zCBspLeaf*)node;
			if (leaf->NumPolys>0) 
			{
				// Cancel search in this subtree if this doesn't overlap with our AABB
				if(!Toolbox::AABBsOverlapping(bbox.Min, bbox.Max, leaf->BBox3D.Min, leaf->BBox3D.Max))
					return;

				// Insert all polygons we got here
				list.insert(list.end(), base->NodePolygons.begin(), base->NodePolygons.end());
			}

			// Got all the polygons and this is a leaf, don't need to do tests for more searches
			return;
		}

		// Get next tree to look at
		int sides = bbox.ClassifyToPlane(node->Plane.Distance, node->PlaneSignbits);

		switch (sides) 
		{
		case zTBBox3D::zPLANE_INFRONT:
			node = (zCBspNode*)node->Front;
			base = base->Front;
			break;

		case zTBBox3D::zPLANE_BEHIND:
			node = (zCBspNode*)node->Back; 
			base = base->Back;
			break;

		case zTBBox3D::zPLANE_SPANNING:
			if(base->Front) 
				CollectPolygonsInAABBRec(base->Front, bbox, list);

			node = (zCBspNode*)node->Back;
			base = base->Back;
			break;
		}
	}
}

/** Returns the current ocean-object */
GOcean* GothicAPI::GetOcean()
{
	return Ocean;
}