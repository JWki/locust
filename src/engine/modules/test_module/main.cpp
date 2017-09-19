#include <stdio.h>
#include <foundation/logging/logging.h>
#include <malloc.h>

#include <foundation/math/math.h>
#include <foundation/memory/memory.h>
#include <foundation/memory/allocators.h>
#include <engine/runtime/entities/entities.h>
#include "ImGui/imgui.h"

#include <engine/tools/fbx_importer/fbx_importer.h>


#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <Commdlg.h>
#undef near
#undef far

#define STB_IMAGE_IMPLEMENTATION
//#define STBI_NO_STDIO
#include <stb/stb_image.h>
#pragma warning(push, 0)    // lots of warnings in here  
#include "cro_mipmap.h"
#pragma warning(pop)

#include <engine/runtime/core/api_registry.h>
#include <engine/runtime/renderer/renderer.h>

#include <fontawesome/IconsFontAwesome.h>

#include <engine/runtime/ImGuizmo/ImGuizmo.h>
void EditTransform(float camera[16], float projection[16], float matrix[16])
{
    static ImGuizmo::OPERATION mCurrentGizmoOperation(ImGuizmo::TRANSLATE);
    static ImGuizmo::MODE mCurrentGizmoMode(ImGuizmo::WORLD);
    if (ImGui::IsKeyPressed(90))
        mCurrentGizmoOperation = ImGuizmo::TRANSLATE;
    if (ImGui::IsKeyPressed(69))
        mCurrentGizmoOperation = ImGuizmo::ROTATE;
    if (ImGui::IsKeyPressed(82)) // r Key
        mCurrentGizmoOperation = ImGuizmo::SCALE;
    if (ImGui::RadioButton(" " ICON_FA_ARROWS "  Translation", mCurrentGizmoOperation == ImGuizmo::TRANSLATE))
        mCurrentGizmoOperation = ImGuizmo::TRANSLATE;
    ImGui::SameLine();
    if (ImGui::RadioButton(" " ICON_FA_REFRESH "  Rotation", mCurrentGizmoOperation == ImGuizmo::ROTATE))
        mCurrentGizmoOperation = ImGuizmo::ROTATE;
    ImGui::SameLine();
    if (ImGui::RadioButton(" " ICON_FA_EXPAND "  Scaling", mCurrentGizmoOperation == ImGuizmo::SCALE))
        mCurrentGizmoOperation = ImGuizmo::SCALE;
    fnd::math::float3 matrixTranslation, matrixRotation, matrixScale;
    ImGuizmo::DecomposeMatrixToComponents(matrix, (float*)matrixTranslation, (float*)matrixRotation, (float*)matrixScale);
    ImGui::DragFloat3(" " ICON_FA_ARROWS, (float*)matrixTranslation, 0.01f);
    ImGui::SameLine(); if (ImGui::Button(ICON_FA_UNDO "##translate")) { matrixTranslation = { 0.0f, 0.0f, 0.0f }; }
    ImGui::DragFloat3(" " ICON_FA_REFRESH, (float*)matrixRotation, 0.1f);
    ImGui::SameLine(); if (ImGui::Button(ICON_FA_UNDO "##rotation")) { matrixRotation = { 0.0f, 0.0f, 0.0f }; }
    ImGui::DragFloat3(" " ICON_FA_EXPAND, (float*)matrixScale, 0.1f);
    ImGui::SameLine(); if (ImGui::Button(ICON_FA_UNDO "##scale")) { matrixScale = { 1.0f, 1.0f, 1.0f }; }
    ImGuizmo::RecomposeMatrixFromComponents((float*)matrixTranslation, (float*)matrixRotation, (float*)matrixScale, matrix);

    if (mCurrentGizmoOperation != ImGuizmo::SCALE)
    {
        if (ImGui::RadioButton(" " ICON_FA_CUBE "  Local", mCurrentGizmoMode == ImGuizmo::LOCAL))
            mCurrentGizmoMode = ImGuizmo::LOCAL;
        ImGui::SameLine();
        if (ImGui::RadioButton(" " ICON_FA_GLOBE "  World", mCurrentGizmoMode == ImGuizmo::WORLD))
            mCurrentGizmoMode = ImGuizmo::WORLD;
    }
    static bool useSnap(false);
    if (ImGui::IsKeyPressed(83))
        useSnap = !useSnap;
    // lol
    ImGui::Checkbox("##snap", &useSnap);
    ImGui::SameLine();
    static fnd::math::float3 snap = { 0.1f, 0.1f, 0.1f };
    switch (mCurrentGizmoOperation)
    {
    case ImGuizmo::TRANSLATE:
        //snap = fnd::math::float3(0.1f);
        ImGui::InputFloat3(" " ICON_FA_TH "  Snap", &snap.x);
        break;
    case ImGuizmo::ROTATE:
        //snap = fnd::math::float3(0.1f);
        ImGui::InputFloat(" " ICON_FA_TH "  Snap", &snap.x);
        break;
    case ImGuizmo::SCALE:
        //snap = fnd::math::float3(0.1f);
        ImGui::InputFloat(" " ICON_FA_TH "  Snap", &snap.x);
        break;
    }
    ImGuiIO& io = ImGui::GetIO();
    ImGuizmo::SetRect(0, 0, io.DisplaySize.x, io.DisplaySize.y);
    ImGuizmo::Manipulate(camera, projection, mCurrentGizmoOperation, mCurrentGizmoMode, matrix, NULL, useSnap ? &snap.x : NULL);
}

struct FileInfo
{
    static const size_t MAX_PATH_LEN = 512;
    char path[MAX_PATH_LEN];
};

static bool OpenFileDialog(char* outNameBuf, size_t outNameBufSize, const char* filter, FileInfo* outFiles, size_t maxNumFiles, size_t* numFiles)
{
    OPENFILENAMEA ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = NULL;
    ofn.lpstrFile = outNameBuf;
    ofn.lpstrFile[0] = '\0';
    ofn.nMaxFile = (DWORD)outNameBufSize;
    ofn.lpstrFilter = filter;
    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = NULL;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = NULL;
    ofn.Flags = OFN_EXPLORER | OFN_ALLOWMULTISELECT | OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;  // @NOTE re OFN_NOCHANGEDIR: fuck you win32 api
    auto res = (GetOpenFileNameA(&ofn) == TRUE);
    if (!res) { *numFiles = 0; return false; }

    size_t dirLen = strlen(outNameBuf);
    if (dirLen > ofn.nFileOffset) {
        // file name is contained within the first substring -> only one file has been selected
        *numFiles = 1;
        memset(outFiles[0].path, 0x0, FileInfo::MAX_PATH_LEN);
        memcpy(outFiles[0].path, outNameBuf, dirLen);
        return true;
    }
    // handle multiple files:
    char* filename = outNameBuf + dirLen + 1;
    size_t fileLen = strlen(filename);
    while (fileLen > 0) {
        
        memset(outFiles[*numFiles].path, 0x0, FileInfo::MAX_PATH_LEN);
        memcpy(outFiles[*numFiles].path, outNameBuf, dirLen);
        outFiles[*numFiles].path[dirLen] = '\\';
        memcpy(outFiles[*numFiles].path + dirLen + 1, filename, fileLen);
        
        *numFiles += 1;

        filename += fileLen + 1;
        fileLen = strlen(filename);
    }

    return true;
}


static void* LoadFileContents(const char* path, fnd::memory::MemoryArenaBase* memoryArena, size_t* fileSize = nullptr)
{
    HANDLE handle = CreateFileA(path, GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (!handle) {
        GT_LOG_ERROR("FileSystem", "Failed to load %s\n", path);
        return nullptr;
    }
    DWORD size = GetFileSize(handle, NULL);
    void* buffer = memoryArena->Allocate(size, 16, GT_SOURCE_INFO);
    DWORD bytesRead = 0;
    auto res = ReadFile(handle, buffer, size, &bytesRead, NULL);
    if (res == FALSE || bytesRead != size) {
        GT_LOG_ERROR("FileSystem", "Failed to read %s\n", path);
        memoryArena->Free(buffer);
        return nullptr;
    }
    if (fileSize) { *fileSize = bytesRead; }
    CloseHandle(handle);
    return buffer;
}


#define MOUSE_LEFT 0
#define MOUSE_RIGHT 1
#define MOUSE_MIDDLE 2

class SimpleFilterPolicy
{
public:
    bool Filter(fnd::logging::LogCriteria criteria)
    {
        return true;
    }
};
class SimpleFormatPolicy
{
public:
    void Format(char* buf, size_t bufSize, fnd::logging::LogCriteria criteria, const char* format, va_list args)
    {
        size_t offset = snprintf(buf, bufSize, "[%s]    ", criteria.channel.str);
        vsnprintf(buf + offset, bufSize - offset, format, args);
    }
};
class PrintfWriter
{
public:
    void Write(const char* msg)
    {
        printf("%s\n", msg);
    }
};

typedef fnd::logging::Logger<SimpleFilterPolicy, SimpleFormatPolicy, PrintfWriter> ConsoleLogger;


struct EntityNode
{
    EntityNode* next = nullptr;
    EntityNode* prev = nullptr;
    entity_system::Entity ent;

    bool isFree = true;
};

EntityNode* AllocateEntityNode(EntityNode* pool, size_t poolSize)
{
    for (size_t i = 0; i < poolSize; ++i) {
        if (pool[i].isFree) {
            pool[i].isFree = false;
            pool[i].ent = { entity_system::INVALID_ID };
            return &pool[i];
        }
    }
    return nullptr;
}

struct EntityNodeList
{
    EntityNode* head = nullptr;
};

void FreeEntityNode(EntityNode* node)
{
    node->isFree = true;
    node->ent = { entity_system::INVALID_ID };
    node->next = node->prev = nullptr;
}

void ClearList(EntityNodeList* list)
{
    EntityNode* it = list->head;
    while (it != nullptr) {
        EntityNode* node = it;
        it = it->next;
        FreeEntityNode(node);
    }
    list->head = nullptr;
}

void AddToList(EntityNodeList* list, EntityNode* node)
{
    if (node == nullptr) { return; }
    EntityNode* it = list->head;
    if (it == nullptr) {
        list->head = node;
        node->prev = nullptr;
        node->next = nullptr;
        return;
    }
    while (it->next != nullptr) {
        if (it->ent.id == node->ent.id || node == it) {
            assert(false);
        }
        it = it->next;
    }
    it->next = node;
    node->prev = it;
    node->next = nullptr;
}

void RemoveFromList(EntityNodeList* list, EntityNode* node)
{
    if (node == nullptr) { return; }
    if (node->next != nullptr) {
        node->next->prev = node->prev;
    }
    if (node->prev != nullptr) {
        node->prev->next = node->next;
    }
    if (node == list->head) { list->head = node->next; }
    node->prev = node->next = nullptr;
}

bool IsEntityInList(EntityNodeList* list, entity_system::Entity ent, EntityNode** outNode = nullptr)
{
    EntityNode* it = list->head;
    while (it != nullptr) {
        if (it->ent.id == ent.id) { 
            if (outNode != nullptr) {
                *outNode = it;
            }
            return true; 
        }
        it = it->next;
    }
    return false;
}



#define ENTITY_NODE_POOL_SIZE 512
struct State {
    core::api_registry::APIRegistry* apiRegistry = nullptr;
    core::api_registry::APIRegistryInterface* apiRegistryInterface = nullptr;

    EntityNodeList entitySelection;
    entity_system::Entity lastSelected;

    //EntityNodeGroup entityGroups;

    EntityNode entityNodePool[ENTITY_NODE_POOL_SIZE];

    bool isEditing = false;
    
    float camYaw = 0.0f;
    float camPitch = 0.0f;
    fnd::math::float3 camPos;
    fnd::math::float3 camOffset = fnd::math::float3(0.0f, 0.0f, -5.0f);

    float cameraPos[16];
    float cameraRotation[16];
    float cameraOffset[16];
    float camOffsetWithRotation[16];
    
    fnd::memory::MemoryArenaBase* applicationArena = nullptr;


    static const size_t MAX_NUM_TEXTURE_ASSETS = 512;
    static const size_t FILENAME_BUF_SIZE = 512;

    struct TextureAsset {

    };

    struct MaterialAsset {
        renderer::MaterialDesc desc;
    };

    struct MeshAsset {
        size_t numSubmeshes = 0;
    };

    struct Asset {
        enum Type : uint16_t {
            NONE = 0,
            ASSET_TYPE_TEXTURE, 
            ASSET_TYPE_MATERIAL,
            ASSET_TYPE_MESH
        } type = NONE;

        char name[FILENAME_BUF_SIZE] = "";
        core::Asset asset;

        // @NOTE this should really be a union but alas unions can't have non trivially constructed members in MSVC for some reason
        TextureAsset as_texture;
        MaterialAsset as_material;
        MeshAsset as_mesh;

    };

  
    size_t textureAssetIndex = 0;
    Asset* textureAssets = nullptr;
    size_t materialAssetIndex = 0;
    Asset* materialAssets = nullptr;
    size_t meshAssetIndex = 0;
    Asset* meshAssets = nullptr;


    struct DragContent
    {
        enum Type : uint16_t {
            NONE = 0,
            DRAG_TYPE_ASSET_REF
        } type = NONE;

        union {
            void*   as_void;
            Asset*  as_asset;
        } data;
        bool wasReleased = false;
        DragContent() { data.as_void = nullptr; }
    } drag;

    /*char padding[1024 * 1024 - 
        ((  sizeof(core::api_registry::APIRegistry*) +
            sizeof(core::api_registry::APIRegistryInterface*) +
            sizeof(EntityNodeList) +
            sizeof(entity_system::Entity) * 2 +     // @NOTE because padding between member fields (members are padded to 64 bit in this struct)
            sizeof(bool) * 2 +
            sizeof(EntityNode) * ENTITY_NODE_POOL_SIZE + 
            sizeof(float) * 2 + 
            sizeof(fnd::math::float3) * 2 +
            sizeof(float) * 16 * 3))];*/
};
//static_assert(sizeof(State) == 1024 * 1024, "");

State::Asset* PushAsset(State* state, State::Asset::Type type, const char* path)
{
    State::Asset* asset = nullptr;
    core::Asset assetID;
    switch (type) {
    case State::Asset::ASSET_TYPE_MESH:
        assetID.id = (uint32_t)state->meshAssetIndex;   // @NOTE STARTING WITH 0 IS INTENTIONAL, first asset to be pushed will be default asset
        asset = &state->meshAssets[state->meshAssetIndex++];
        break;
    case State::Asset::ASSET_TYPE_MATERIAL:
        assetID.id = (uint32_t)state->materialAssetIndex;   // @NOTE STARTING WITH 0 IS INTENTIONAL, first asset to be pushed will be default asset
        asset = &state->materialAssets[state->materialAssetIndex++];
        break;
    case State::Asset::ASSET_TYPE_TEXTURE:
        assetID.id = (uint32_t)state->textureAssetIndex;   // @NOTE STARTING WITH 0 IS INTENTIONAL, first asset to be pushed will be default asset
        asset = &state->textureAssets[state->textureAssetIndex++];
        break;
    }
    strncpy_s(asset->name, State::FILENAME_BUF_SIZE, path, State::FILENAME_BUF_SIZE);
    asset->type = type;
    asset->asset = assetID;
    return asset;
}

extern "C" __declspec(dllexport)
void* Initialize(fnd::memory::MemoryArenaBase* memoryArena, core::api_registry::APIRegistry* apiRegistry, core::api_registry::APIRegistryInterface* apiRegistryInterface)
{
    State* state = (State*)GT_NEW(State, memoryArena);
    state->apiRegistry = apiRegistry;
    state->apiRegistryInterface = apiRegistryInterface;

    state->applicationArena = memoryArena;

    state->textureAssets = GT_NEW_ARRAY(State::Asset, State::MAX_NUM_TEXTURE_ASSETS, state->applicationArena);
    state->materialAssets = GT_NEW_ARRAY(State::Asset, State::MAX_NUM_TEXTURE_ASSETS, state->applicationArena);
    state->meshAssets = GT_NEW_ARRAY(State::Asset, State::MAX_NUM_TEXTURE_ASSETS, state->applicationArena);

    PushAsset(state, State::Asset::ASSET_TYPE_MESH, "None");
    PushAsset(state, State::Asset::ASSET_TYPE_MATERIAL, "None");
    PushAsset(state, State::Asset::ASSET_TYPE_TEXTURE, "None");

    util::Make4x4FloatMatrixIdentity(state->cameraRotation);
    util::Make4x4FloatMatrixIdentity(state->cameraOffset);
    util::Make4x4FloatMatrixIdentity(state->camOffsetWithRotation);
    util::Make4x4FloatTranslationMatrixCM(state->cameraPos, { 0.0f, -0.4f, 2.75f });

    return state;
}

State::Asset* AssetRefLabel(State* state, State::Asset* asset, bool acceptDrop)
{
    if (asset == nullptr) { 
        ImGui::Text("null");
        return nullptr; 
    }
    auto AcceptDrop = [](State* state, State::Asset* asset) -> State::Asset* {
        bool isHovered = ImGui::IsItemHoveredRect();
        if (isHovered) {
            ("drag type is %s", state->drag.type != State::DragContent::NONE ? "something" : "none");
            if (!state->drag.wasReleased && state->drag.type != State::DragContent::NONE) {
                if (asset->type == state->drag.data.as_asset->type) {
                    ImGui::SetTooltip("Release mouse to drop %s", state->drag.data.as_asset->name);
                }
                else {
                    ImGui::SetTooltip("Can't drop %s here", state->drag.data.as_asset->name);
                }
            }
            if (state->drag.wasReleased) {
                if (state->drag.type == State::DragContent::DRAG_TYPE_ASSET_REF) {
                    GT_LOG_DEBUG("Editor", "Trying to drag %s onto %s", state->drag.data.as_asset->name, asset->name);

                    if (asset->type == state->drag.data.as_asset->type) {
                        return state->drag.data.as_asset;
                    }
                    else {
                        GT_LOG_DEBUG("Editor", "Trying to match assets of different type");
                    }
                }
            }
        }
        return nullptr;
    };
    State::Asset* res = nullptr;

    ImGui::Selectable(asset->name);
    
    if (acceptDrop) {
        res = AcceptDrop(state, asset);
        if (res != nullptr) { return res; }
    }
    if (ImGui::IsItemActive() && !ImGui::IsItemHovered()) {
        //GT_LOG_DEBUG("Editor", "Trying to drag %s with delta %f, %f", asset->name, ImGui::GetMouseDragDelta(MOUSE_LEFT).x, ImGui::GetMouseDragDelta(MOUSE_LEFT).y);
        state->drag.type = State::DragContent::DRAG_TYPE_ASSET_REF;
        state->drag.data.as_asset = asset;
    }
    return res;
}

extern "C" __declspec(dllexport)
void Update(void* userData, ImGuiContext* guiContext, entity_system::World* world, renderer::RenderWorld* renderWorld, fnd::memory::LinearAllocator* frameAllocator, entity_system::Entity** entitySelection, size_t* numEntitiesSelected)
{
    using namespace fnd;
    ConsoleLogger consoleLogger;

    auto state = (State*)userData;

    auto entitySystem = (entity_system::EntitySystemInterface*) state->apiRegistryInterface->Get(state->apiRegistry, ENTITY_SYSTEM_API_NAME);
    assert(entitySystem);

    auto renderer = (renderer::RendererInterface*) state->apiRegistryInterface->Get(state->apiRegistry, RENDERER_API_NAME);
    assert(renderer);

    auto fbxImporter = (fbx_importer::FBXImportInterface*) state->apiRegistryInterface->Get(state->apiRegistry, FBX_IMPORTER_API_NAME);

    float camera[16];
    float projection[16];
    util::Copy4x4FloatMatrixCM(renderer->GetCameraTransform(renderWorld), camera);
    util::Copy4x4FloatMatrixCM(renderer->GetCameraProjection(renderWorld), projection);
    
    util::Make4x4FloatProjectionMatrixCMLH(projection, 1.0f, (float)1920, (float)1080, 0.1f, 1000.0f);
    renderer->SetCameraProjection(renderWorld, projection);

    ImGui::SetCurrentContext(guiContext);
    
    ImGuizmo::BeginFrame();

    if (state->drag.wasReleased) {
        state->drag = State::DragContent();
    }
    if (!ImGui::IsMouseDown(MOUSE_LEFT)) {
        state->drag.wasReleased = true;
    }

    ImGui::Begin("Drag Debug"); {
        const char* fmt = state->drag.type != State::DragContent::NONE ? "Dragging %s with delta %f, %f" : "Not dragging";
        if (state->drag.type != State::DragContent::NONE) {
            ImGui::Text(fmt, state->drag.data.as_asset->name, ImGui::GetMouseDragDelta().x, ImGui::GetMouseDragDelta().y);
        }
        else {
            ImGui::Text(fmt);
        }

    } ImGui::End();

    enum CameraMode : int {
        CAMERA_MODE_ARCBALL = 0,
        CAMERA_MODE_FREE_FLY = 1
    };

    const char* modeStrings[] = { "Arcball", "Free Fly" };
    static CameraMode mode = CAMERA_MODE_ARCBALL;

    int WINDOW_WIDTH = 1920;
    int WINDOW_HEIGHT = 1080;

    if (mode == CAMERA_MODE_ARCBALL) {
        if (ImGui::IsMouseDragging(MOUSE_RIGHT) || ImGui::IsMouseDragging(MOUSE_MIDDLE)) {
            if (ImGui::IsMouseDown(MOUSE_RIGHT)) {
                if (!ImGui::IsMouseDown(MOUSE_MIDDLE)) {
                    math::float3 camPosDelta;
                    camPosDelta.x = 2.0f * (-ImGui::GetMouseDragDelta(MOUSE_RIGHT).x / WINDOW_WIDTH);
                    camPosDelta.y = 2.0f * (ImGui::GetMouseDragDelta(MOUSE_RIGHT).y / WINDOW_HEIGHT);

                    math::float3 worldSpaceDelta = util::TransformDirectionCM(camPosDelta, state->cameraRotation);
                    state->camPos += worldSpaceDelta;
                }
                else {
                    math::float2 delta;
                    delta.x = 8.0f * (-ImGui::GetMouseDragDelta(MOUSE_RIGHT).x / WINDOW_WIDTH);
                    delta.y = 8.0f * (-ImGui::GetMouseDragDelta(MOUSE_RIGHT).y / WINDOW_HEIGHT);
                    float sign = -1.0f;
                    sign = delta.y > 0.0f ? 1.0f : -1.0f;
                    state->camOffset.z -= math::Length(delta) * sign;
                    state->camOffset.z = state->camOffset.z > -0.1f ? -0.1f : state->camOffset.z;
                }
            }
            else {
                state->camYaw += 180.0f * (-ImGui::GetMouseDragDelta(MOUSE_MIDDLE).x / WINDOW_WIDTH);
                state->camPitch += 180.0f * (-ImGui::GetMouseDragDelta(MOUSE_MIDDLE).y / WINDOW_HEIGHT);
            }
            ImGui::ResetMouseDragDelta(MOUSE_RIGHT);
            ImGui::ResetMouseDragDelta(MOUSE_MIDDLE);
        }
    }
    else {
        state->camOffset = math::float3(0.0f);
        if (ImGui::IsMouseDragging(MOUSE_RIGHT) || ImGui::IsMouseDragging(MOUSE_MIDDLE)) {

            if (ImGui::IsMouseDown(MOUSE_RIGHT)) {
                if (!ImGui::IsMouseDown(MOUSE_MIDDLE)) {
                    math::float3 camPosDelta;
                    camPosDelta.x = 2.0f * (-ImGui::GetMouseDragDelta(MOUSE_RIGHT).x / WINDOW_WIDTH);
                    camPosDelta.y = 2.0f * (ImGui::GetMouseDragDelta(MOUSE_RIGHT).y / WINDOW_HEIGHT);
                    math::float3 worldSpaceDelta = util::TransformDirectionCM(camPosDelta, state->cameraRotation);
                    state->camPos += worldSpaceDelta;
                }
                else {
                    math::float3 camPosDelta;
                    camPosDelta.x = 2.0f * (-ImGui::GetMouseDragDelta(MOUSE_RIGHT).x / WINDOW_WIDTH);
                    camPosDelta.z = 2.0f * (ImGui::GetMouseDragDelta(MOUSE_RIGHT).y / WINDOW_HEIGHT);
                    math::float3 worldSpaceDelta = util::TransformDirectionCM(camPosDelta, state->cameraRotation);
                    state->camPos += worldSpaceDelta;
                }
            }
            else {
                state->camYaw += 180.0f * (-ImGui::GetMouseDragDelta(MOUSE_MIDDLE).x / WINDOW_WIDTH);
                state->camPitch += 180.0f * (-ImGui::GetMouseDragDelta(MOUSE_MIDDLE).y / WINDOW_HEIGHT);
            }
            ImGui::ResetMouseDragDelta(MOUSE_MIDDLE);
            ImGui::ResetMouseDragDelta(MOUSE_RIGHT);
        }
    }

    ImGui::Begin(ICON_FA_CAMERA "  Camera"); {
        static float camOffsetStore = 0.0f;

        if (ImGui::Combo("Mode", (int*)&mode, modeStrings, 2)) {
            if (mode == CAMERA_MODE_ARCBALL) {
                // we were in free cam mode before
                // -> get stored cam offset, calculate what state->camPos must be based off that
                state->camOffset.z = camOffsetStore;
                util::Make4x4FloatTranslationMatrixCM(state->cameraOffset, state->camOffset);
                util::MultiplyMatricesCM(state->cameraRotation, state->cameraOffset, state->camOffsetWithRotation);
                math::float3 transformedOrigin = util::TransformPositionCM(math::float3(), state->camOffsetWithRotation);
                state->camPos = state->camPos - transformedOrigin;
            }
            else {
                // we were in arcball mode before
                // -> fold camera offset + rotation into cam pos, preserve offset 
                float fullTransform[16];
                util::Make4x4FloatTranslationMatrixCM(state->cameraPos, state->camPos);
                util::MultiplyMatricesCM(state->cameraPos, state->camOffsetWithRotation, fullTransform);
                state->camPos = util::Get4x4FloatMatrixColumnCM(fullTransform, 3).xyz;
                camOffsetStore = state->camOffset.z;
                state->camOffset.z = 0.0f;
            }
        }


        ImGui::DragFloat("Camera Yaw", &state->camYaw);
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_UNDO "##yaw")) {
            state->camYaw = 0.0f;
        }

        ImGui::DragFloat("Camera Pitch", &state->camPitch);
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_UNDO "##pitch")) {
            state->camPitch = 0.0f;
        }

        ImGui::DragFloat3("Camera Offset", (float*)&state->camOffset, 0.01f);
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_UNDO "##offset")) {
            state->camOffset = math::float3(0.0f);
        }

        ImGui::DragFloat3("Camera Pos", (float*)&state->camPos, 0.01f);
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_UNDO "##pos")) {
            state->camPos = math::float3(0.0f);
        }
    } ImGui::End();

    float cameraRotX[16], cameraRotY[16];
    util::Make4x4FloatRotationMatrixCMLH(cameraRotX, math::float3(1.0f, 0.0f, 0.0f), state->camPitch * (3.141f / 180.0f));
    util::Make4x4FloatRotationMatrixCMLH(cameraRotY, math::float3(0.0f, 1.0f, 0.0f), state->camYaw * (3.141f / 180.0f));


    util::Make4x4FloatTranslationMatrixCM(state->cameraOffset, state->camOffset);
    util::Make4x4FloatTranslationMatrixCM(state->cameraPos, state->camPos);
    util::MultiplyMatricesCM(cameraRotY, cameraRotX, state->cameraRotation);

    util::MultiplyMatricesCM(state->cameraRotation, state->cameraOffset, state->camOffsetWithRotation);
    util::MultiplyMatricesCM(state->cameraPos, state->camOffsetWithRotation, camera);

    float camInverse[16];
    util::Inverse4x4FloatMatrixCM(camera, camInverse);
    util::Copy4x4FloatMatrixCM(camInverse, camera);

    renderer->SetCameraTransform(renderWorld, camera);

    ImGuiWindowFlags windowFlags = 0;

    entity_system::Entity* entityList = GT_NEW_ARRAY(entity_system::Entity, 512, frameAllocator);
    size_t numEntities = 0;

    if (ImGui::Begin(ICON_FA_DATABASE "  Entity Explorer", nullptr, windowFlags)) {
        

        /* Add / delete of entities */
        
        if (ImGui::Button(ICON_FA_USER_PLUS "  Create New")) {
            entity_system::Entity ent = entitySystem->CreateEntity(world);
            if (!ImGui::GetIO().KeyCtrl) {
                ClearList(&state->entitySelection);
            }
            EntityNode* selectionNode = AllocateEntityNode(state->entityNodePool, ENTITY_NODE_POOL_SIZE);
            selectionNode->ent = ent;
            AddToList(&state->entitySelection, selectionNode);
            state->lastSelected = ent;
        }
        
        entitySystem->GetAllEntities(world, entityList, &numEntities);

        if (state->entitySelection.head != nullptr && entitySystem->IsEntityAlive(world, state->entitySelection.head->ent)) {
            ImGui::SameLine();
            if (ImGui::Button(ICON_FA_USER_TIMES "  Delete")) {
                EntityNode* it = state->entitySelection.head;
                while (it) {
                    entitySystem->DestroyEntity(world, it->ent);
                    renderer::StaticMesh meshComponent = renderer->GetStaticMesh(renderWorld, it->ent.id);
                    if (meshComponent.id != renderer::INVALID_ID) {
                        renderer->DestroyStaticMesh(renderWorld, meshComponent);
                    }


                    it = it->next;
                }
                ClearList(&state->entitySelection);
            }
            ImGui::SameLine();
            if (ImGui::Button(ICON_FA_USERS "  Copy")) {
                EntityNode* it = state->entitySelection.head;
                EntityNodeList copies;
                while (it) {
                    auto newEntity = entitySystem->CopyEntity(world, it->ent);

                    renderer::StaticMesh meshComponent = renderer->GetStaticMesh(renderWorld, it->ent.id);
                    if (meshComponent.id != renderer::INVALID_ID) {
                        renderer->CopyStaticMesh(renderWorld, newEntity.id, meshComponent);
                    }

                    EntityNode* selectionNode = AllocateEntityNode(state->entityNodePool, ENTITY_NODE_POOL_SIZE);
                    selectionNode->ent = newEntity;
                    AddToList(&copies, selectionNode);
                    state->lastSelected = newEntity;
                    it = it->next;
                }
                ClearList(&state->entitySelection);
                it = copies.head;
                while (it) {
                    auto next = it->next;
                    AddToList(&state->entitySelection, it);
                    it = next;
                }
            }
        }

        /* List of alive entities */

        entitySystem->GetAllEntities(world, entityList, &numEntities);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        //ImGui::Text("");
        ImVec2 contentSize = ImGui::GetContentRegionAvail();
        ImGui::BeginChild("##list", contentSize);
        for (size_t i = 0; i < numEntities; ++i) {
            entity_system::Entity entity = entityList[i];
            const char* name = entitySystem->GetEntityName(world, entity);
            ImGui::PushID(entity.id);
            
            auto GetIndex = [](entity_system::Entity entity, entity_system::Entity* entities, size_t numEntities) -> int {
                int index = -1;
                for (size_t i = 0; i < numEntities; ++i) {
                    if (entity.id == entities[i].id) {
                        index = (int)i;
                        break;
                    }
                }
                return index;
            };

            int lastSelectedIndex = GetIndex(state->lastSelected, entityList, numEntities);
            ImGui::PushStyleColor(ImGuiCol_Header, lastSelectedIndex == (int)i ? ImVec4(0.2f, 0.4f, 1.0f, 1.0f) : ImGui::GetStyle().Colors[ImGuiCol_Header]);
            bool select = ImGui::Selectable(name, IsEntityInList(&state->entitySelection, entity));
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(MOUSE_LEFT)) {
                state->camPos = util::Get4x4FloatMatrixColumnCM(entitySystem->GetEntityTransform(world, entity), 3).xyz;
            }
            if (select) {
                ImGui::PopStyleColor();

                if (ImGui::GetIO().KeyShift || ImGui::GetIO().KeyCtrl) {  // multiselection
                    if(ImGui::GetIO().KeyShift) {
                        EntityNode* node = nullptr;

                        int clickedIndex = (int)i;
                        if (clickedIndex > lastSelectedIndex) {
                            for (int i = lastSelectedIndex; i <= clickedIndex; ++i) {
                                if (!IsEntityInList(&state->entitySelection, entityList[i], &node) && !ImGui::GetIO().KeyCtrl) {
                                    EntityNode* selectionNode = AllocateEntityNode(state->entityNodePool, ENTITY_NODE_POOL_SIZE);
                                    selectionNode->ent = entityList[i];
                                    AddToList(&state->entitySelection, selectionNode);
                                }
                                else {
                                    if (ImGui::GetIO().KeyCtrl) {
                                        RemoveFromList(&state->entitySelection, node);
                                    }
                                }
                            }
                        }
                        else {
                            if (clickedIndex < lastSelectedIndex) {
                                for (int i = lastSelectedIndex; i >= clickedIndex; --i) {
                                    if (!IsEntityInList(&state->entitySelection, entityList[i], &node) && !ImGui::GetIO().KeyCtrl) {
                                        EntityNode* selectionNode = AllocateEntityNode(state->entityNodePool, ENTITY_NODE_POOL_SIZE);
                                        selectionNode->ent = entityList[i];
                                        AddToList(&state->entitySelection, selectionNode);
                                    }
                                    else {
                                        if (ImGui::GetIO().KeyCtrl) {
                                            RemoveFromList(&state->entitySelection, node);
                                        }
                                    }
                                }
                            }
                            else {
                                // @TODO what to do in this case?
                                GT_LOG_WARNING("Editor", "Meh.");
                            }
                        }
                    }
                    else {
                        EntityNode* node = nullptr;
                        if (!IsEntityInList(&state->entitySelection, entity, &node)) {  // ADD
                            EntityNode* selectionNode = AllocateEntityNode(state->entityNodePool, ENTITY_NODE_POOL_SIZE);
                            selectionNode->ent = entity;
                            AddToList(&state->entitySelection, selectionNode);
                        }
                        else {  // REMOVE
                            RemoveFromList(&state->entitySelection, node);
                            FreeEntityNode(node);
                        }
                    }
                }
                else {  // SET selection
                    ClearList(&state->entitySelection);
                    EntityNode* selectionNode = AllocateEntityNode(state->entityNodePool, ENTITY_NODE_POOL_SIZE);
                    selectionNode->ent = entity;
                    AddToList(&state->entitySelection, selectionNode);
                }

                if (IsEntityInList(&state->entitySelection, entity)) {
                    state->lastSelected = entity;
                }
            }
            else {
                ImGui::PopStyleColor();
            }
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
                //camPos = util::Get4x4FloatMatrixColumnCM(entity_system::GetEntityTransform(world, state->selectedEntity), 3).xyz;
            }
            ImGui::SameLine();
            ImGui::Text("(id = %i)", entity.id);
            ImGui::PopID();
        }
        if (ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered() && ImGui::IsMouseClicked(0)) {
            ClearList(&state->entitySelection);
        }
        ImGui::EndChild();

    } ImGui::End();

    entitySystem->GetAllEntities(world, entityList, &numEntities);
    for (size_t i = 0; i < numEntities; ++i) {
        //ImGuizmo::DrawCube(camera, projection, entitySystem->GetEntityTransform(world, entityList[i]));
    }

    if (ImGui::Begin(ICON_FA_FILE_O "  Assets")) {
        ImGui::Spacing();
        {   // Textures
            ImGui::Text("Textures");
            ImGui::Spacing();
            ImGui::PushID("##importtex");
            bool pushed = ImGui::Button("Import");
            ImGui::PopID();
            if (pushed) {
                const size_t maxNumItems = 64;
                char* buf = GT_NEW_ARRAY(char, State::FILENAME_BUF_SIZE * maxNumItems, frameAllocator);
                FileInfo* files = GT_NEW_ARRAY(FileInfo, maxNumItems, frameAllocator);
                size_t numItems = 0;
                if (OpenFileDialog(buf, State::FILENAME_BUF_SIZE * maxNumItems, "PNG Image Files\0*.png\0JPG Image Files\0*.jpeg\0", files, maxNumItems, &numItems)) {
                   
                    for (size_t i = 0; i < numItems; ++i) {
                        GT_LOG_DEBUG("Editor", "Trying to import %s", files[i].path);
                        State::Asset* textureAsset = PushAsset(state, State::Asset::ASSET_TYPE_TEXTURE, files[i].path);
                        {
                            int width, height, numComponents;
                            auto image = stbi_load(files[i].path, &width, &height, &numComponents, 4);
                            //image = stbi_load_from_memory(buf, buf_len, &width, &height, &numComponents, 4);
                            if (image == NULL) {
                                GT_LOG_ERROR("Assets", "Failed to load image %s:\n%s\n", files[i].path, stbi_failure_reason());
                            }
                            //assert(numComponents == 4);

                            // mipmap generation
                            int numMipMapLevels = cro_GetMipMapLevels(width, height);
                            int** mipmaps = (int**)frameAllocator->Allocate(sizeof(int*) * numMipMapLevels, alignof(int*));
                            unsigned int w = width;
                            unsigned int h = height;
                            mipmaps[0] = (int*)image;
                            for (int i = 1; i < numMipMapLevels; ++i) {
                                cro_GetMipMapSize(w, h, &w, &h);
                                mipmaps[i] = (int*)frameAllocator->Allocate(sizeof(int) * w * h, alignof(int));
                                cro_GenMipMapAvgI(mipmaps[i - 1], w * 2, h * 2, mipmaps[i]);
                            }

                            gfx::SamplerDesc defaultSamplerStateDesc;
                            defaultSamplerStateDesc.minFilter = gfx::FilterMode::FILTER_LINEAR_MIPMAP_LINEAR;

                            gfx::ImageDesc diffDesc;
                            //paintTextureDesc.usage = gfx::ResourceUsage::USAGE_DYNAMIC;
                            diffDesc.type = gfx::ImageType::IMAGE_TYPE_2D;
                            diffDesc.numMipmaps = numMipMapLevels;
                            diffDesc.width = width;
                            diffDesc.height = height;
                            diffDesc.pixelFormat = gfx::PixelFormat::PIXEL_FORMAT_R8G8B8A8_UNORM;
                            diffDesc.samplerDesc = &defaultSamplerStateDesc;
                            diffDesc.numDataItems = numMipMapLevels;
                            void* data[64];
                            size_t dataSizes[64];
                            //data[0] = image;
                            w = width; h = height;
                            for (int i = 0; i < numMipMapLevels; ++i) {
                                data[i] = mipmaps[i];
                                dataSizes[i] = sizeof(stbi_uc) * 4 * w * h;
                                cro_GetMipMapSize(w, h, &w, &h);
                            }

                            diffDesc.initialData = data;
                            diffDesc.initialDataSizes = dataSizes;

                            renderer::TextureDesc texDesc;
                            texDesc.desc = diffDesc;
                            renderer->UpdateTextureLibrary(renderWorld, textureAsset->asset, &texDesc);

                            stbi_image_free(image);
                        }
                    }
                }
            }
            ImGui::BeginChild("##textures", ImVec2(ImGui::GetContentRegionAvailWidth(), 400), false, ImGuiWindowFlags_HorizontalScrollbar);
            
            ImGui::Spacing();
            static float displayScale = 1.0f;
            ImGui::SliderFloat("##displayScale", &displayScale, 0.5f, 4.0f);
            size_t numDisplayColumns = (size_t)(ImGui::GetContentRegionAvailWidth() / (displayScale * 128.0f));
            numDisplayColumns = numDisplayColumns > 1 ? numDisplayColumns : 1;
            for (size_t i = 1; i < state->textureAssetIndex; ++i) {
                if (state->textureAssets[i].asset.id == 0) { continue; }
                auto texHandle = renderer->GetTextureHandle(renderWorld, state->textureAssets[i].asset);
                if (((i-1) % numDisplayColumns) != 0) {
                    ImGui::SameLine();
                }
                else {
                    ImGui::Spacing();
                }
                ImGui::BeginGroup();
                ImGui::Image((ImTextureID)(uintptr_t)(texHandle.id), ImVec2(128 * displayScale, 128 * displayScale));
                if (ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    ImGui::Text("%s", state->textureAssets[i].name);
                    ImGui::EndTooltip();
                }
                State::Asset* asset = &state->textureAssets[i];
                ImGui::PushID((int)i);
                AssetRefLabel(state, asset, false);
                ImGui::PopID();
                ImGui::EndGroup();
            }
            ImGui::EndChild();
        }
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        {   // materials
            ImGui::Text("Materials");
            
            bool pushed = ImGui::Button("Create New");

            static char nameEditBuf[State::FILENAME_BUF_SIZE];
            State::MaterialAsset* editAsset = nullptr;
            if (pushed) {
                ImGui::OpenPopup("Material Editor");
                snprintf(nameEditBuf, State::FILENAME_BUF_SIZE, "Material%llu", state->materialAssetIndex);
            }

            if (ImGui::BeginPopup("Material Editor")) {
                
                static renderer::MaterialDesc desc;
                static core::Asset* texSlot = nullptr;

                ImGui::InputText("##name", nameEditBuf, State::FILENAME_BUF_SIZE);

                {   // base color
                    ImGui::Text("Base Color");
                    if (desc.baseColorMap.id == 0) {
                        ImGui::PushID("##basecolor");
                        ImGui::Dummy(ImVec2(128, 128));
                        ImGui::PopID();
                    }
                    else {
                        auto texHandle = renderer->GetTextureHandle(renderWorld, desc.baseColorMap);
                        ImGui::Image((ImTextureID)(uintptr_t)(texHandle.id), ImVec2(128, 128));
                    }
                    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(MOUSE_LEFT)) {
                        ImGui::OpenPopup("##texturePicker");
                        texSlot = &desc.baseColorMap;
                    }
                }
                {   // roughness
                    ImGui::Text("Roughness");
                    if (desc.baseColorMap.id == 0) {
                        ImGui::PushID("##roughness");
                        ImGui::Dummy(ImVec2(128, 128));
                        ImGui::PopID();
                    }
                    else {
                        auto texHandle = renderer->GetTextureHandle(renderWorld, desc.roughnessMap);
                        ImGui::Image((ImTextureID)(uintptr_t)(texHandle.id), ImVec2(128, 128));
                    }
                    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(MOUSE_LEFT)) {
                        ImGui::OpenPopup("##texturePicker");
                        texSlot = &desc.roughnessMap;
                    }
                }
                {   // metalness
                    ImGui::Text("Metallic");
                    if (desc.baseColorMap.id == 0) {
                        ImGui::PushID("##metallic");
                        ImGui::Dummy(ImVec2(128, 128));
                        ImGui::PopID();

                    }
                    else {
                        auto texHandle = renderer->GetTextureHandle(renderWorld, desc.metalnessMap);
                        ImGui::Image((ImTextureID)(uintptr_t)(texHandle.id), ImVec2(128, 128));
                    }
                    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(MOUSE_LEFT)) {
                        ImGui::OpenPopup("##texturePicker");
                        texSlot = &desc.metalnessMap;
                    }
                }
                {   // normal map
                    ImGui::Text("Normal Map");
                    if (desc.baseColorMap.id == 0) {
                        ImGui::PushID("##normalmap");
                        ImGui::Dummy(ImVec2(128, 128));
                        ImGui::PopID();

                    }
                    else {
                        auto texHandle = renderer->GetTextureHandle(renderWorld, desc.normalVecMap);
                        ImGui::Image((ImTextureID)(uintptr_t)(texHandle.id), ImVec2(128, 128));
                    }
                    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(MOUSE_LEFT)) {
                        ImGui::OpenPopup("##texturePicker");
                        texSlot = &desc.normalVecMap;
                    }
                }
                {   // ao map
                    ImGui::Text("Occlusion Map");
                    if (desc.baseColorMap.id == 0) {
                        ImGui::PushID("##occlusion");
                        ImGui::Dummy(ImVec2(128, 128));
                        ImGui::PopID();
                    }
                    else {
                        auto texHandle = renderer->GetTextureHandle(renderWorld, desc.occlusionMap);
                        ImGui::Image((ImTextureID)(uintptr_t)(texHandle.id), ImVec2(128, 128));
                    }
                    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(MOUSE_LEFT)) {
                        ImGui::OpenPopup("##texturePicker");
                        texSlot = &desc.occlusionMap;
                    }
                }

                if (ImGui::BeginPopup("##texturePicker")) {
                    if (ImGui::Button("Cancel")) {
                        ImGui::CloseCurrentPopup();
                    }
                    //ImGui::BeginChild("##list", ImGui::GetContentRegionAvail());
                    for (size_t i = 0; i < state->textureAssetIndex; ++i) {
                        if (state->textureAssets[i].asset.id == 0) { continue; }
                        auto texHandle = renderer->GetTextureHandle(renderWorld, state->textureAssets[i].asset);
                        ImGui::Image((ImTextureID)(uintptr_t)(texHandle.id), ImVec2(128, 128));
                        if (ImGui::IsItemHovered()) {
                            ImGui::BeginTooltip();
                            ImGui::Text("%s", state->textureAssets[i].name);
                            ImGui::EndTooltip();

                            if (ImGui::IsMouseClicked(MOUSE_LEFT)) {
                                *texSlot = state->textureAssets[i].asset;
                                //ImGui::CloseCurrentPopup();
                            }
                        }
                    }
                    //ImGui::EndChild();
                    ImGui::EndPopup();
                }

                bool isValid = desc.baseColorMap.id != 0 && desc.roughnessMap.id != 0 && desc.metalnessMap.id != 0 && desc.normalVecMap.id != 0 && desc.occlusionMap.id != 0;
                bool done = ImGui::Button("Done", ImVec2(100, 25)) && isValid;
                ImGui::SameLine();
                bool cancel = ImGui::Button("Cancel", ImVec2(100, 25));
                if (done) {
                    State::Asset* materialAsset = PushAsset(state, State::Asset::ASSET_TYPE_MATERIAL, nameEditBuf);
                    renderer->UpdateMaterialLibrary(renderWorld, materialAsset->asset, &desc);
                    GT_LOG_INFO("Editor", "Created material %s", materialAsset->name);

                    materialAsset->as_material.desc = desc;
                    desc = renderer::MaterialDesc();
                }

                if (done || cancel) {
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
            
            for (size_t i = 0; i < state->materialAssetIndex; ++i) {
                State::Asset* asset = &state->materialAssets[i];
                if (asset->asset.id == 0) { continue; }

                ImGui::PushID((int)i);
                AssetRefLabel(state, asset, false);
                ImGui::PopID();

                auto baseColorHandle = renderer->GetTextureHandle(renderWorld, asset->as_material.desc.baseColorMap);
                auto roughnessHandle = renderer->GetTextureHandle(renderWorld, asset->as_material.desc.roughnessMap);
                auto metalnessHandle = renderer->GetTextureHandle(renderWorld, asset->as_material.desc.metalnessMap);
                auto normalVecHandle = renderer->GetTextureHandle(renderWorld, asset->as_material.desc.normalVecMap);
                auto occlusionHandle = renderer->GetTextureHandle(renderWorld, asset->as_material.desc.occlusionMap);
                
                ImGui::BeginGroup();
                ImGui::Image((ImTextureID)(uintptr_t)(baseColorHandle.id), ImVec2(64, 64));
                ImGui::Text("Base Color");
                ImGui::EndGroup();
                ImGui::SameLine();
                ImGui::BeginGroup();
                ImGui::Image((ImTextureID)(uintptr_t)(roughnessHandle.id), ImVec2(64, 64));
                ImGui::Text("Roughness");
                ImGui::EndGroup();
                ImGui::SameLine();
                ImGui::BeginGroup();
                ImGui::Image((ImTextureID)(uintptr_t)(metalnessHandle.id), ImVec2(64, 64));
                ImGui::Text("Metallic");
                ImGui::EndGroup();
                ImGui::SameLine();
                ImGui::BeginGroup();
                ImGui::Image((ImTextureID)(uintptr_t)(normalVecHandle.id), ImVec2(64, 64));
                ImGui::Text("Normal Map");
                ImGui::EndGroup();
                ImGui::SameLine();
                ImGui::BeginGroup();
                ImGui::Image((ImTextureID)(uintptr_t)(occlusionHandle.id), ImVec2(64, 64));
                ImGui::Text("Occlusion");
                ImGui::EndGroup();
 
            }
        }
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        {   // Meshes
            ImGui::Text("Meshes");
            ImGui::Spacing();
            ImGui::PushID("##importmesh");
            bool pushed = ImGui::Button("Import");
            ImGui::PopID();
            if (pushed) {
                const size_t maxNumItems = 64;
                char* buf = GT_NEW_ARRAY(char, State::FILENAME_BUF_SIZE * maxNumItems, frameAllocator);
                FileInfo* files = GT_NEW_ARRAY(FileInfo, maxNumItems, frameAllocator);
                size_t numItems = 0;
                if (OpenFileDialog(buf, State::FILENAME_BUF_SIZE * maxNumItems, "FBX Files\0*.fbx\0", files, maxNumItems, &numItems)) {

                    for (size_t i = 0; i < numItems; ++i) {
                        GT_LOG_DEBUG("Editor", "Trying to import %s", files[i].path);

                        State::Asset* meshAsset = PushAsset(state, State::Asset::ASSET_TYPE_MESH, files[i].path);
                        {
                            size_t modelFileSize = 0;
                            fnd::memory::SimpleMemoryArena<fnd::memory::LinearAllocator> tempArena(frameAllocator);
                            void* modelFileData = LoadFileContents(files[i].path, &tempArena, &modelFileSize);
                            if (modelFileData && modelFileSize > 0) {
                                GT_LOG_INFO("Assets", "Loaded %s: %llu kbytes", files[i].path, modelFileSize / 1024);

                                renderer::MeshDesc* meshDescs = GT_NEW_ARRAY(renderer::MeshDesc, 512, &tempArena);
                                size_t numSubmeshes = 0;

                                bool res = fbxImporter->FBXImportAsset(&tempArena, (char*)modelFileData, modelFileSize, meshDescs, &numSubmeshes);
                                if (!res) {
                                    GT_LOG_ERROR("Assets", "Failed to import %s", files[i].path);
                                }
                                else {
                                    GT_LOG_INFO("Assets", "Imported %s", files[i].path);

                                    meshAsset->as_mesh.numSubmeshes = numSubmeshes;
                                    renderer->UpdateMeshLibrary(renderWorld, meshAsset->asset, meshDescs, numSubmeshes);
                                }
                            }
                            else {
                                GT_LOG_ERROR("Assets", "Failed to import %s", files[i].path);
                            }
                        }
                    }
                }
            }

            ImGui::BeginChild("##meshes", ImVec2(ImGui::GetContentRegionAvailWidth(), 400), false, ImGuiWindowFlags_HorizontalScrollbar);
            ImGui::Spacing();
            for (size_t i = 0; i < state->meshAssetIndex; ++i) {
                if (state->meshAssets[i].asset.id == 0) { continue; }
                ImGui::PushID((int)i);
                AssetRefLabel(state, &state->meshAssets[i], false);
                ImGui::PopID();
            }
            ImGui::EndChild();
        }

    } ImGui::End();

    //ImGui::ShowTestWindow();

    ImGui::Begin(ICON_FA_WRENCH "  Property Editor"); {
        static entity_system::Entity selectedEntity = { entity_system::INVALID_ID };
        if (!IsEntityInList(&state->entitySelection, selectedEntity)) {
            selectedEntity = { entity_system::INVALID_ID };
        }
        EntityNode* it = state->entitySelection.head;
        if (selectedEntity.id == entity_system::INVALID_ID && it != nullptr) {
            selectedEntity = it->ent;
        }

        math::float3 meanPosition;
        math::float3 meanRotation;
        math::float3 meanScale;
        int numPositions = 0;
        while (it != nullptr && it->ent.id != 0) {

            ImGuiWindowFlags flags = ImGuiWindowFlags_HorizontalScrollbar;

            ImVec2 contentRegion = ImGui::GetContentRegionAvail();

            ImGui::BeginChild("##tabs", ImVec2(contentRegion.x, 50), false, flags);
            ImGui::PushID(it->ent.id);
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, selectedEntity.id != it->ent.id ? ImGui::GetStyle().Alpha * 0.4f : ImGui::GetStyle().Alpha);
            if (ImGui::Button(entitySystem->GetEntityName(world, it->ent))) {
                selectedEntity = it->ent;
            }
            ImGui::PopStyleVar();
            ImGui::SameLine();
            ImGui::PopID();
            ImGui::EndChild();

            float* transform = entitySystem->GetEntityTransform(world, it->ent);
            math::float3 position = util::Get4x4FloatMatrixColumnCM(transform, 3).xyz;

            meanPosition += position;

            numPositions++;

            it = it->next;
        }

        // @NOTE avoid divide by zero
        if (numPositions == 0) { numPositions = 1; }
        meanPosition /= (float)numPositions;
        meanRotation /= (float)numPositions;
        meanScale /= (float)numPositions;

        float groupTransform[16];
        util::Make4x4FloatMatrixIdentity(groupTransform);
        if (selectedEntity.id != entity_system::INVALID_ID) {
            util::Copy4x4FloatMatrixCM(entitySystem->GetEntityTransform(world, selectedEntity), groupTransform);
        }
        util::Set4x4FloatMatrixColumnCM(groupTransform, 3, math::float4(meanPosition, 1.0f));
        //ImGuizmo::RecomposeMatrixFromComponents((float*)meanPosition, (float*)meanRotation, (float*)meanScale, groupTransform);

        ImGui::Text("Editing %s", state->isEditing ? ICON_FA_CHECK : ICON_FA_TIMES);

        ImVec2 contentRegion = ImGui::GetContentRegionAvail();
        ImGui::BeginChild("##properties", contentRegion);
        if (selectedEntity.id != entity_system::INVALID_ID) {
            if (ImGui::TreeNode(ICON_FA_PENCIL "    Object")) {
                if (ImGui::InputText(" " ICON_FA_TAG " Name", entitySystem->GetEntityName(world, selectedEntity), ENTITY_NAME_SIZE, ImGuiInputTextFlags_EnterReturnsTrue)) {
                    //entitySystem->SetEntityName(world, selectedEntity, entitySystem->GetEntityName(world, selectedEntity));
                }
                ImGui::TreePop();
            }
            if (ImGui::TreeNode(ICON_FA_LOCATION_ARROW "    Transform")) {
                EditTransform(camera, projection, groupTransform);
                ImGui::TreePop();
            }

            if (ImGui::TreeNode(ICON_FA_CUBES "    Rendering")) {
               
                bool anyChange = false;

                auto LookupMeshAsset = [](core::Asset ID, State::Asset* assets, size_t numAssets) -> State::Asset* {
                    for (size_t i = 0; i < numAssets; ++i) {
                        if (assets[i].asset == ID && assets[i].type == State::Asset::ASSET_TYPE_MESH) {
                            return &assets[i];
                        }
                    }
                    return nullptr;
                };
                auto LookupMaterialAsset = [](core::Asset ID, State::Asset* assets, size_t numAssets) -> State::Asset* {
                    for (size_t i = 0; i < numAssets; ++i) {
                        if (assets[i].asset == ID && assets[i].type == State::Asset::ASSET_TYPE_MATERIAL) {
                            return &assets[i];
                        }
                    }
                    return nullptr;
                };

                renderer::StaticMesh mesh = renderer->GetStaticMesh(renderWorld, selectedEntity.id);
                
                core::Asset meshAsset = renderer->GetMeshAsset(renderWorld, mesh);
                core::Asset* materials = nullptr;
                
                size_t numSubmeshes = 0;
                renderer->GetMaterials(renderWorld, mesh, nullptr, &numSubmeshes);
                if (numSubmeshes > 0) {
                    materials = GT_NEW_ARRAY(core::Asset, numSubmeshes, frameAllocator);
                }
                renderer->GetMaterials(renderWorld, mesh, materials, &numSubmeshes);

                ImGui::Text("Mesh: ");
                ImGui::SameLine();

                auto asset = LookupMeshAsset(meshAsset, state->meshAssets, state->meshAssetIndex);
                ImGui::PushID(-1);
                auto newMesh = AssetRefLabel(state, asset, true);
                ImGui::PopID();
                if (newMesh != nullptr) {
                    anyChange = true;
                    meshAsset = newMesh->asset;
                }
                if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(MOUSE_LEFT)) {
                    ImGui::OpenPopup("##meshPicker");
                }
                if (ImGui::BeginPopup("##meshPicker")) {
                    for (size_t i = 0; i < state->meshAssetIndex; ++i) {
                        if (state->meshAssets[i].asset.id == 0) { continue; }
                        ImGui::Text("%s", state->meshAssets[i].name);
                        if (ImGui::IsItemHovered() && ImGui::IsItemClicked(MOUSE_LEFT)) {
                            meshAsset = state->meshAssets[i].asset;
                            anyChange = true;
                            ImGui::CloseCurrentPopup();
                        }
                    }
                    ImGui::EndPopup();
                }

                ImGui::Spacing();
                ImGui::Text("Materials (%llu)", numSubmeshes);
                ImGui::BeginChild("##materials", ImVec2(0, 0), true, ImGuiWindowFlags_HorizontalScrollbar);
                
                static int editMaterialIndex = -1;
                if (numSubmeshes > 0) {
                    ImGui::Text("mat_master");
                    if (ImGui::IsItemHovered() && ImGui::IsItemClicked(MOUSE_LEFT)) {
                        editMaterialIndex = -1;
                        ImGui::OpenPopup("##materialPicker");
                    }
                    ImGui::SameLine();
                    State::Asset* asset = LookupMaterialAsset(materials[0], state->materialAssets, state->materialAssetIndex);
                    ImGui::PushID(-1);
                    auto newMat = AssetRefLabel(state, asset, true);
                    ImGui::PopID();
                    if (newMat != nullptr) {
                        for (size_t i = 0; i < numSubmeshes; ++i) {
                            if (materials[i].id != newMat->asset.id) {
                                materials[i] = newMat->asset;
                                anyChange = true;
                            }
                        }
                    }
                }
                for (size_t i = 0; i < numSubmeshes; ++i) {
                    State::Asset* asset = LookupMaterialAsset(materials[i], state->materialAssets, state->materialAssetIndex);
                    ImGui::PushID((int)i);
                    ImGui::Text("mat_%llu: ", i);
                    ImGui::PopID();
                    if (ImGui::IsItemHovered() && ImGui::IsItemClicked(MOUSE_LEFT)) {
                        editMaterialIndex = (int)i;
                        ImGui::OpenPopup("##materialPicker");
                        break;
                    }
                    ImGui::SameLine();
                    ImGui::PushID((int)i);
                    auto newMat = AssetRefLabel(state, asset, true);
                    ImGui::PopID();
                    if (newMat != nullptr) {
                        anyChange = true;
                        materials[i] = newMat->asset;
                    }
                }
                if (ImGui::BeginPopup("##materialPicker")) {
                    for (size_t i = 0; i < state->materialAssetIndex; ++i) {
                        ImGui::Text("%s", state->materialAssets[i].name);
                        if (ImGui::IsItemHovered() && ImGui::IsItemClicked(MOUSE_LEFT)) {
                            if (editMaterialIndex >= 0) {
                                materials[editMaterialIndex] = state->materialAssets[i].asset;
                            }
                            else {
                                for (size_t j = 0; j < numSubmeshes; ++j) {
                                    materials[j] = state->materialAssets[i].asset;
                                }
                            }
                            anyChange = true;
                            ImGui::CloseCurrentPopup();
                            break;
                        }
                    }
                    ImGui::EndPopup();
                }

                ImGui::EndChild();

                if (anyChange) {
                    if (mesh.id != renderer::INVALID_ID) {
                        renderer->DestroyStaticMesh(renderWorld, mesh);
                    }
                    renderer->CreateStaticMesh(renderWorld, selectedEntity.id, meshAsset, materials, numSubmeshes);
                }

                ImGui::TreePop();
            }
        }
        ImGui::EndChild();

        math::float3 newPosition = util::Get4x4FloatMatrixColumnCM(groupTransform, 3).xyz;

        auto posDifference = newPosition - meanPosition;

        if (math::Length(posDifference) > 0.001f) {
            if (!state->isEditing) {
                state->isEditing = true;
                
            }
        }
        else {
            if (state->isEditing) {
                state->isEditing = false;
            }
        }


        it = state->entitySelection.head;
        while (it) {
            math::float3 pos = util::Get4x4FloatMatrixColumnCM(entitySystem->GetEntityTransform(world, it->ent), 3).xyz;

            if (it->ent.id == selectedEntity.id) {
                util::Copy4x4FloatMatrixCM(groupTransform, entitySystem->GetEntityTransform(world, selectedEntity));
            }

            math::float3 newPos = pos + posDifference;
            util::Set4x4FloatMatrixColumnCM(entitySystem->GetEntityTransform(world, it->ent), 3, math::float4(newPos, 1.0f));

            it = it->next;
        }

    } ImGui::End();

    *numEntitiesSelected = 0;
    auto it = state->entitySelection.head;
    while (it != nullptr) {
        (*numEntitiesSelected)++;
        it = it->next;
    }

    if (*numEntitiesSelected > 0) {
        *entitySelection = (entity_system::Entity*)frameAllocator->Allocate(sizeof(entity_system::Entity) * (*numEntitiesSelected), alignof(entity_system::Entity));
        it = state->entitySelection.head;
        int i = 0;
        while (it != nullptr) {
            (*entitySelection)[i++].id = it->ent.id;
            it = it->next;
        }
    }

    return;
}