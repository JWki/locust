#include <stdio.h>
#include <foundation/logging/logging.h>
#include <malloc.h>

#include <foundation/filesystem/filesystem.h>
#include <foundation/math/math.h>
#include <foundation/memory/memory.h>
#include <foundation/memory/allocators.h>
#include <engine/runtime/entities/entities.h>
#include "ImGui/imgui.h"
#include "ImGui/imgui_internal.h"

#include <engine/tools/fbx_importer/fbx_importer.h>


#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <Commdlg.h>
#include <shlobj.h>
#include <shlwapi.h>
#pragma comment(lib, "shlwapi.lib")
#undef near
#undef far
#undef interface
#undef GetObject

#define STB_IMAGE_IMPLEMENTATION
//#define STBI_NO_STDIO
#include <stb/stb_image.h>
#pragma warning(push, 0)    // lots of warnings in here  
#include "cro_mipmap.h"
#pragma warning(pop)

#include <engine/runtime/core/api_registry.h>
#include <engine/runtime/renderer/renderer.h>
#include <engine/runtime/runtime.h>

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

static bool SaveFileDialog(char* outNameBuf, size_t outNameBufSize, const char* filter, const char* defExtension)
{
    OPENFILENAMEA ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = NULL;
    ofn.lpstrFile = outNameBuf;
    ofn.lpstrFile[0] = '\0';
    ofn.nMaxFile = (DWORD)outNameBufSize;
    ofn.lpstrFilter = filter;
    ofn.lpstrDefExt = defExtension;
    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = NULL;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = NULL;
    ofn.Flags = OFN_EXPLORER | OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;  // @NOTE re OFN_NOCHANGEDIR: fuck you win32 api
    auto res = (GetSaveFileNameA(&ofn) == TRUE);
    return res;
}

static bool OpenFileDialog(char* outNameBuf, size_t outNameBufSize, const char* filter, FileInfo* outFiles, size_t maxNumFiles = 1, size_t* numFiles = nullptr)
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
    ofn.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;  // @NOTE re OFN_NOCHANGEDIR: fuck you win32 api
    if (maxNumFiles > 1) {
        ofn.Flags |= OFN_ALLOWMULTISELECT;
    }
    auto res = (GetOpenFileNameA(&ofn) == TRUE);
    if (!res) { 
        if (numFiles != nullptr) {
            *numFiles = 0;
        }
        return false; 
    }

    size_t dirLen = strlen(outNameBuf);
    if (dirLen > ofn.nFileOffset) {
        // file name is contained within the first substring -> only one file has been selected
        if (numFiles != nullptr) {
            *numFiles = 1;
        }
        memset(outFiles[0].path, 0x0, FileInfo::MAX_PATH_LEN);
        memcpy(outFiles[0].path, outNameBuf, dirLen);
        return true;
    }
    // handle multiple files:
    char* filename = outNameBuf + dirLen + 1;
    size_t fileLen = strlen(filename);
    size_t fileCount = 0;
    while (fileLen > 0) {
        
        memset(outFiles[fileCount].path, 0x0, FileInfo::MAX_PATH_LEN);
        memcpy(outFiles[fileCount].path, outNameBuf, dirLen);
        outFiles[fileCount].path[dirLen] = '\\';
        memcpy(outFiles[fileCount].path + dirLen + 1, filename, fileLen);
        
        fileCount += 1;


        filename += fileLen + 1;
        fileLen = strlen(filename);
    }
    if (numFiles != nullptr) {
        *numFiles = fileCount;
    }

    return true;
}

bool ListDirectoryContents(const char *sDir)
{
    WIN32_FIND_DATAA fdFile;
    HANDLE hFind = NULL;

    char sPath[2048];

    //Specify a file mask. *.* = We want everything!
    snprintf(sPath, 2048, "%s\\*.*", sDir);

    if ((hFind = FindFirstFileA(sPath, &fdFile)) == INVALID_HANDLE_VALUE)
    {
        printf("Path not found: [%s]\n", sDir);
        return false;
    }

    do
    {
        //Find first file will always return "."
        //    and ".." as the first two directories.
        if (strcmp(fdFile.cFileName, ".") != 0
            && strcmp(fdFile.cFileName, "..") != 0)
        {
            //Build up our file path using the passed in
            //  [sDir] and the file/foldername we just found:
            snprintf(sPath, 2048, "%s\\%s", sDir, fdFile.cFileName);

            //Is the entity a File or Folder?
            if (fdFile.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            {
                //printf("Directory: %s\n", sPath);
                size_t offset = strlen(sPath);
                while (offset > 0 && sPath[offset] != '\\') {
                    offset--;
                }
                if (sPath[offset] == '\\') { offset++; }

                char formatBuf[MAX_PATH];
                snprintf(formatBuf, MAX_PATH, "%s %s", ICON_FA_FOLDER_O, sPath + offset);

                if (ImGui::TreeNode(sPath, "%s", formatBuf)) {
                    ListDirectoryContents(sPath); //Recursion, I love it!
                    ImGui::TreePop();
                }
            }
            else {
                //printf("File: %s\n", sPath);
                size_t offset = strlen(sPath);
                while (offset > 0 && sPath[offset] != '\\') {
                    offset--;
                } 
                if (sPath[offset] == '\\') { offset++; }
                ImGui::Text("       %s %s", ICON_FA_FILE_O, sPath + offset);
            }
        }
    } while (FindNextFileA(hFind, &fdFile)); //Find the next file.
   

    FindClose(hFind); //Always, Always, clean things up!

    return true;
}

// https://www.codeproject.com/Articles/13088/How-to-Browse-for-a-Folder
bool GetFolder(char* outPath, const char* caption)
{
    bool retVal = false;

    // The BROWSEINFO struct tells the shell 
    // how it should display the dialog.
    BROWSEINFOA bi;
    memset(&bi, 0, sizeof(bi));

    bi.ulFlags = BIF_USENEWUI;
    bi.hwndOwner = NULL;
    bi.lpszTitle = caption;

    // must call this if using BIF_USENEWUI
    ::OleInitialize(NULL);

    // Show the dialog and get the itemIDList for the 
    // selected folder.
    LPITEMIDLIST pIDL = ::SHBrowseForFolderA(&bi);

    if (pIDL != NULL)
    {

        if (::SHGetPathFromIDListA(pIDL, outPath) != 0)
        {
            retVal = true;
        }

        // free the item id list
        CoTaskMemFree(pIDL);
    }

    ::OleUninitialize();

    return retVal;
};

static bool DoesFileExist(const char* path) 
{
    DWORD dwAttrib = GetFileAttributesA(path);

    return (dwAttrib != INVALID_FILE_ATTRIBUTES &&
        !(dwAttrib & FILE_ATTRIBUTE_DIRECTORY));
}

static void* LoadFileContents(const char* path, fnd::memory::MemoryArenaBase* memoryArena, size_t* fileSize = nullptr)
{
    HANDLE handle = CreateFileA(path, GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (!handle) {
        GT_LOG_ERROR("FileSystem", "Failed to load %s", path);
        return nullptr;
    }
    DWORD size = GetFileSize(handle, NULL);
    void* buffer = memoryArena->Allocate(size, 16, GT_SOURCE_INFO);
    DWORD bytesRead = 0;
    auto res = ReadFile(handle, buffer, size, &bytesRead, NULL);
    if (res == FALSE || bytesRead != size) {
        GT_LOG_ERROR("FileSystem", "Failed to read %s - bytes read: %lu / %lu", path, bytesRead, size);
        memoryArena->Free(buffer);
        return nullptr;
    }
    if (fileSize) { *fileSize = bytesRead; }
    CloseHandle(handle);
    return buffer;
}

static bool DumpToFile(const char* path, void* bytes, size_t numBytes)
{
    HANDLE handle = CreateFileA(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (!handle) {
        GT_LOG_ERROR("FileSystem", "Failed to open %s for writing", path);
        return false;
    }
    DWORD bytesWritten = 0;
    auto res = WriteFile(handle, bytes, (DWORD)numBytes, &bytesWritten, NULL);
    bool success = (res == TRUE) && (bytesWritten == numBytes);
    if (!success) {
        GT_LOG_ERROR("FileSystem", "Failed to write %s - bytes written: %lu & %lu", path, bytesWritten, (DWORD)numBytes);
    }
    CloseHandle(handle);
    return success;
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
using namespace fnd;
ConsoleLogger g_consoleLogger;

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


struct NonTrivialFoo
{
    size_t field = 32;
};

struct TrivialFoo
{
    size_t field;
};


struct SumType
{
    union {
        NonTrivialFoo as_non_trivial_foo;
        TrivialFoo as_trivial_foo;
    };

    SumType() { as_non_trivial_foo = NonTrivialFoo(); }
};



struct Project
{
    fnd::filesystem::Path basePath;
    char name[MAX_PATH];
};

#define ENTITY_NODE_POOL_SIZE 512
struct Editor {
    size_t frameIndex = 0;

    fnd::filesystem::Path prefPath;
    fnd::filesystem::Path currentDirectory;

    size_t numRecentProjects = 0;
    char** recentProjectPaths = nullptr;

    Project currentProject;

    core::api_registry::APIRegistry* apiRegistry = nullptr;
    core::api_registry::APIRegistryInterface* apiRegistryInterface = nullptr;

    EntityNodeList entitySelection;
    entity_system::Entity lastSelected;

    EntityNode entityNodePool[ENTITY_NODE_POOL_SIZE];

    entity_system::World* currentWorld = nullptr;
    renderer::RenderWorld* currentRenderWorld = nullptr;
    renderer::RendererInterface* renderer = nullptr;

    runtime::RuntimeInterface* runtime = nullptr;

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
        renderer::TextureDesc desc;
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

        // @NOTE because we want this to be a union, we're required to explicitely create the non-trivially constructible fields in it 
        union {
            TextureAsset as_texture;
            MaterialAsset as_material;
            MeshAsset as_mesh;
        };

        Asset() { as_texture = TextureAsset(); }
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


    struct Views {
        
        enum View : uint16_t {
            NONE = 0,

            CAMERA_CONTROLS,
            ENTITY_EXPLORER, 
            PROPERTY_EDITOR,
            ASSET_BROWSER,
            RENDERER_SETTINGS,
            DRAG_DEBUG,

            PROJECT_WIZARD
        };

        static const size_t MAX_NUM_VIEWS = 128;
        bool    enableView[MAX_NUM_VIEWS];   

        Views() {
            memset(enableView, 0x0, sizeof(enableView));

            enableView[ENTITY_EXPLORER] = true;
            enableView[PROPERTY_EDITOR] = true;
            enableView[ASSET_BROWSER] = true;
        }

    } views; 

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

Editor::Asset* PushAsset(Editor* editor, Editor::Asset::Type type, const char* path)
{
    Editor::Asset* asset = nullptr;
    core::Asset assetID;
    switch (type) {
    case Editor::Asset::ASSET_TYPE_MESH:
        assetID.id = (uint32_t)editor->meshAssetIndex;   // @NOTE STARTING WITH 0 IS INTENTIONAL, first asset to be pushed will be default asset
        asset = &editor->meshAssets[editor->meshAssetIndex++];
        break;
    case Editor::Asset::ASSET_TYPE_MATERIAL:
        assetID.id = (uint32_t)editor->materialAssetIndex;   // @NOTE STARTING WITH 0 IS INTENTIONAL, first asset to be pushed will be default asset
        asset = &editor->materialAssets[editor->materialAssetIndex++];
        break;
    case Editor::Asset::ASSET_TYPE_TEXTURE:
        assetID.id = (uint32_t)editor->textureAssetIndex;   // @NOTE STARTING WITH 0 IS INTENTIONAL, first asset to be pushed will be default asset
        asset = &editor->textureAssets[editor->textureAssetIndex++];
        break;
    }
    strncpy_s(asset->name, Editor::FILENAME_BUF_SIZE, path, Editor::FILENAME_BUF_SIZE);
    asset->type = type;
    asset->asset = assetID;
    return asset;
}


extern "C" __declspec(dllexport)
void* Initialize(fnd::memory::MemoryArenaBase* memoryArena, core::api_registry::APIRegistry* apiRegistry, core::api_registry::APIRegistryInterface* apiRegistryInterface)
{
    Editor* editor = (Editor*)GT_NEW(Editor, memoryArena);
    editor->apiRegistry = apiRegistry;
    editor->apiRegistryInterface = apiRegistryInterface;

    editor->applicationArena = memoryArena;

    editor->runtime = (runtime::RuntimeInterface*)apiRegistryInterface->Get(apiRegistry, RUNTIME_API_NAME);

    editor->textureAssets = GT_NEW_ARRAY(Editor::Asset, Editor::MAX_NUM_TEXTURE_ASSETS, editor->applicationArena);
    editor->materialAssets = GT_NEW_ARRAY(Editor::Asset, Editor::MAX_NUM_TEXTURE_ASSETS, editor->applicationArena);
    editor->meshAssets = GT_NEW_ARRAY(Editor::Asset, Editor::MAX_NUM_TEXTURE_ASSETS, editor->applicationArena);

    PushAsset(editor, Editor::Asset::ASSET_TYPE_MESH, "None");
    PushAsset(editor, Editor::Asset::ASSET_TYPE_MATERIAL, "None");
    PushAsset(editor, Editor::Asset::ASSET_TYPE_TEXTURE, "None");

    util::Make4x4FloatMatrixIdentity(editor->cameraRotation);
    util::Make4x4FloatMatrixIdentity(editor->cameraOffset);
    util::Make4x4FloatMatrixIdentity(editor->camOffsetWithRotation);
    util::Make4x4FloatTranslationMatrixCM(editor->cameraPos, { 0.0f, -0.4f, 2.75f });

   
    if (!SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_APPDATA | CSIDL_FLAG_CREATE, NULL, 0, editor->prefPath._buffer))) {
        GT_LOG_ERROR("Editor", "Failed to locate user preferences path");
    }
    editor->prefPath._length = strlen(editor->prefPath._buffer);
    editor->prefPath.Append("GTEditor");

    if (!SUCCEEDED(CreateDirectoryA((const char*)editor->prefPath, NULL))) {
        GT_LOG_ERROR("Editor", "Failed to create directory for user preferences at %s", (const char*)editor->prefPath);
        editor->prefPath = fnd::filesystem::Path();
    }
    else {
        GT_LOG_INFO("Editor", "Directory for user preferences: %s", (const char*)editor->prefPath);
    }

    editor->currentProject.basePath.Set((const char*)editor->prefPath);
    strncpy_s(editor->currentProject.name, MAX_PATH, "temp", MAX_PATH);
    editor->currentDirectory.Set((const char*)editor->currentDirectory);
    return editor;
}

Editor::Asset* AssetRefLabel(Editor* editor, Editor::Asset* asset, bool acceptDrop, float maxWidth = -1.0f)
{
    if (asset == nullptr) { 
        ImGui::Text("null");
        return nullptr; 
    }
    auto AcceptDrop = [](Editor* editor, Editor::Asset* asset) -> Editor::Asset* {
        bool isHovered = ImGui::IsItemHoveredRect();
        if (isHovered) {
            ("drag type is %s", editor->drag.type != Editor::DragContent::NONE ? "something" : "none");
            if (!editor->drag.wasReleased && editor->drag.type != Editor::DragContent::NONE) {
                if (asset->type == editor->drag.data.as_asset->type) {
                    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.0f, 1.0f, 0.0f, 0.6f));
                    ImGui::SetTooltip("Release mouse to drop %s", editor->drag.data.as_asset->name);
                }
                else {
                    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(1.0f, 0.0f, 0.0f, 0.6f));
                    ImGui::SetTooltip("Can't drop %s here", editor->drag.data.as_asset->name);
                }
                ImGui::PopStyleColor();
            }
            if (editor->drag.wasReleased) {
                if (editor->drag.type == Editor::DragContent::DRAG_TYPE_ASSET_REF) {
                    GT_LOG_DEBUG("Editor", "Trying to drag %s onto %s", editor->drag.data.as_asset->name, asset->name);

                    if (asset->type == editor->drag.data.as_asset->type) {
                        return editor->drag.data.as_asset;
                    }
                    else {
                        GT_LOG_DEBUG("Editor", "Trying to match assets of different type");
                    }
                }
            }
        }
        return nullptr;
    };
    Editor::Asset* res = nullptr;

    char* displayString = asset->name;
    char formatBuf[512] = "";
    if (maxWidth > 0.0f) {
        ImFont* font = ImGui::GetCurrentContext()->Font;
        const float fontSize = ImGui::GetCurrentContext()->FontSize;

        // @NOTE this is probably a very inefficient way to do things
        char* str = asset->name;
        snprintf(formatBuf, 512, "...%s", str);
        ImVec2 textSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, formatBuf);
        while (textSize.x > maxWidth * 0.8f) {
            str++;
            snprintf(formatBuf, 512, "...%s", str);
            textSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, str);
        }
        displayString = formatBuf;
    }
    ImGui::Selectable(displayString);

    if (acceptDrop) {
        res = AcceptDrop(editor, asset);
        if (res != nullptr) { return res; }
    }
    if (ImGui::IsItemActive() && !ImGui::IsItemHovered()) {
        //GT_LOG_DEBUG("Editor", "Trying to drag %s with delta %f, %f", asset->name, ImGui::GetMouseDragDelta(MOUSE_LEFT).x, ImGui::GetMouseDragDelta(MOUSE_LEFT).y);
        editor->drag.type = Editor::DragContent::DRAG_TYPE_ASSET_REF;
        editor->drag.data.as_asset = asset;
    }
    return res;
}

struct SceneFileHeader
{
    static const uint32_t MAX_NAME_STRING_LEN = 512;
    uint32_t    nameStringLen = 0;
    char        nameString[MAX_NAME_STRING_LEN];

    uint32_t    numAssets = 0;

    void        SetName(const char* name)
    {
        memset(nameString, 0x0, MAX_NAME_STRING_LEN);
        nameStringLen = (uint32_t)strlen(name);
        memcpy(nameString, name, nameStringLen);
    }
};

#define HANDLE_INDEX(handle)        (uint16_t)(handle)
#define HANDLE_GENERATION(handle)   (uint16_t)(handle >> 16)

#define HANDLE_GENERATION_START 1

#define MAKE_HANDLE(index, generation) (uint32_t)(((uint32_t)generation) << 16 | index); 

template <class TResource>
struct ResourcePool
{
    uint32_t    size = 0;
    uint32_t    numElements = 0;
    TResource*  buffer = nullptr;
    uint16_t*   indexList = nullptr;
    uint32_t    indexListHead = 0;
    uint32_t    indexListTail = 0;

    void Initialize(uint32_t bufferSize, fnd::memory::MemoryArenaBase* memoryArena)
    {
        numElements = 0;
        size = bufferSize;
        buffer = GT_NEW_ARRAY(TResource, size, memoryArena);
        indexList = GT_NEW_ARRAY(uint16_t, size, memoryArena);
        indexListHead = indexListTail = 0;
        for (uint32_t i = 0; i < size; ++i) {
            indexList[i] = i;
        }
        indexListTail = size - 1;
    }

    bool GetNextIndex(uint16_t* outIndex)
    {
        if (indexListHead == indexListTail) { return false; }
        *outIndex = indexList[indexListHead];
        indexListHead = (indexListHead + 1) % size;
        return true;
    }

    void ReleaseIndex(uint16_t index)
    {
        indexListTail = (indexListTail + 1) % size;
        indexList[indexListTail] = index;
    }

    bool Allocate(TResource** resource, uint32_t* id)
    {
        uint16_t index = 0;
        if (!GetNextIndex(&index)) {
            return false;
        }
        TResource* res = &buffer[index];
        //res->resState = _ResourceState::STATE_ALLOC;
        *resource = res;
        *id = MAKE_HANDLE(index, res->generation);
        numElements++;
        return true;
    }

    void Free(uint32_t id)
    {
        uint16_t index = HANDLE_INDEX(id);
        TResource* res = &buffer[index];
        assert(res->generation == HANDLE_GENERATION(id));
        //D3D11ReleaseResource(res);

        res->generation++;
        //res->resState = _ResourceState::STATE_EMPTY;
        numElements--;
        ReleaseIndex(index);
    }

    TResource* Get(uint32_t id)
    {
        uint16_t index = HANDLE_INDEX(id);
        TResource* res = &buffer[index];
        if (res->generation != HANDLE_GENERATION(id)) { return nullptr; }
        //assert(res->generation == HANDLE_GENERATION(id));
        return res;
    }
};


enum class PropertyType
{
    NONE = 0,
    OBJECT,
    FLOAT,
    BOOL,
    INT,
    FLOAT3,
    COLOR_RGBA,
    STRING
};

static const char* g_propertyTypeNameTable[] = {
    "NONE", "OBJECT", "FLOAT", "BOOL", "INT", "FLOAT3", "COLOR_RGBA", "STRING"
};

typedef uint32_t ObjectType;
typedef uint32_t ObjectHandle;
typedef uint32_t TypeHandle;

struct PropertyDesc
{
    PropertyType type = PropertyType::NONE;
    const char* name = "";
    TypeHandle objType = 0;
};


struct ObjectTypeDesc
{
    const char*     name            = nullptr;
    size_t          numProperties   = 0;
    PropertyDesc*   properties      = nullptr;

    TypeHandle      baseType        = 0;
};


#define STRING_PROPERTY_SIZE 1024
struct Property
{
    PropertyType type;

    union {
        struct {
            ObjectHandle handle;
            TypeHandle type;
        } as_object;
        float as_float;
        bool as_bool;
        int as_int;
        float as_float3[3];
        float as_colorRGBA[4];
        struct {
            char buf[STRING_PROPERTY_SIZE];
        } as_string;
    };
};




struct ObjectDatabase
{
    fnd::memory::TLSFAllocator* blockAllocator = nullptr;
    
    struct IndexEntry
    {
        void* ptr = nullptr;
        uint16_t generation = HANDLE_GENERATION_START;
    };
    ResourcePool<IndexEntry> typeIndex;
    ResourcePool<IndexEntry> objectIndex;

    struct NodeHeader
    {
        const char* name = nullptr;
        TypeHandle  typeHandle;

        union {
            ObjectHandle objectHandle;
            ObjectHandle prototypeHandle;
        };
        uint32_t numProperties = 0;
        uint32_t numInheritedProperties = 0;

        NodeHeader* parent = nullptr;
        NodeHeader* next = nullptr;
        NodeHeader* prev = nullptr;
    };

    NodeHeader*   firstObj = nullptr;
    NodeHeader*   firstType = nullptr;
    
    TypeHandle RegisterType(ObjectTypeDesc* typeDesc)
    {
        IndexEntry* indexEntry;
        TypeHandle handle = 0;
        typeIndex.Allocate(&indexEntry, &handle);
        
        size_t numInheritedProperties = 0;
        size_t numProperties = 0;
        
        NodeHeader* parent = nullptr;

        if (typeDesc->baseType != 0) {
            auto parentIndexEntry = typeIndex.Get(typeDesc->baseType);
            parent = (NodeHeader*)parentIndexEntry->ptr;
            numInheritedProperties = parent->numProperties;
        }
        numProperties = numInheritedProperties + typeDesc->numProperties;

        size_t blockSize = sizeof(NodeHeader) + sizeof(PropertyDesc) * numProperties;
        union {
            void* as_void;
            NodeHeader* as_header;
            PropertyDesc* as_property;
        };
        
        as_void = indexEntry->ptr = blockAllocator->Allocate(blockSize, 4);
        as_header->numProperties = (uint32_t)numProperties;
        as_header->numInheritedProperties = (uint32_t)numInheritedProperties;
        as_header->name = typeDesc->name;
        as_header->typeHandle = handle;
        as_header->prototypeHandle = 0;
        as_header->next = nullptr;
        as_header->prev = nullptr;
        as_header->parent = parent;


        auto it = firstType;
        while (it != nullptr && it->next != nullptr) {
            it = it->next;
        }
        if (firstType == it && firstType == nullptr) {
            firstType = as_header;
        }
        else {
            it->next = as_header;
            as_header->prev = it;
        }

        as_header++;
        if (parent != nullptr) {
            union {
                NodeHeader* parent_as_header;
                PropertyDesc* parent_property;
            };
            parent_as_header = parent;
            parent_as_header++;
            for (size_t i = 0; i < numInheritedProperties; ++i) {
                *as_property = *parent_property;
                as_property++;
                parent_property++;
            }
        }
        for (size_t i = 0; i < numProperties - numInheritedProperties; ++i) {
            *as_property = typeDesc->properties[i];
            as_property++;
        }
        return handle;
    }

    void SetPrototype(TypeHandle type, ObjectHandle object)
    {
        if (type == 0) { return; }
        auto indexEntry = typeIndex.Get(type);
        if (!indexEntry) { return; }

        {   // check validity of object
            if (object == 0) { return; }
            auto objIndexEntry = objectIndex.Get(object);
            if (!objIndexEntry) { return; }
        }

        auto header = (NodeHeader*)indexEntry->ptr;
        header->prototypeHandle = object;
    }

    ObjectHandle CreateObjectWithType(const char* name, TypeHandle type)
    {
        IndexEntry* objIndexEntry;
        ObjectHandle handle = 0;
        objectIndex.Allocate(&objIndexEntry, &handle);

        union {
            void* as_void;
            NodeHeader* as_header;
            PropertyDesc* as_property;
        } source;
        {
            auto indexEntry = typeIndex.Get(type);
            source.as_void = indexEntry->ptr;
        }

        size_t blockSize = sizeof(NodeHeader) + sizeof(Property) * source.as_header->numProperties;
        union {
            void* as_void;
            NodeHeader* as_header;
            Property* as_property;
        } target;
        target.as_void = objIndexEntry->ptr = blockAllocator->Allocate(blockSize, 4);
        size_t numProperties = target.as_header->numProperties = source.as_header->numProperties;
        target.as_header->name = name;
        target.as_header->typeHandle = type;
        target.as_header->objectHandle = handle;
        target.as_header->next = nullptr;
        target.as_header->prev = nullptr;
        auto it = firstObj;
        while (it != nullptr && it->next != nullptr) {
            it = it->next;
        }
        if (firstObj == it && firstObj == nullptr) {
            firstObj = target.as_header;
        }
        else {
            it->next = target.as_header;
            target.as_header->prev = it;
        }
        
        auto prototype = GetObject(source.as_header->prototypeHandle);
        if (prototype.objHandle == 0 && source.as_header->prototypeHandle != 0) {
            // log and lazily remove reference to prototype
            GT_LOG_DEBUG("Object System", "Someone deleted a prototype object");
            source.as_header->prototypeHandle = 0;
        }

        target.as_header++;
        source.as_header++;

        for (int i = 0; i < numProperties; ++i) {

            target.as_property->type = source.as_property->type;
            
            if (prototype.objHandle == 0) {
                memset(&target.as_property->as_object, 0x0, sizeof(*target.as_property) - sizeof(target.as_property->type));
            }
            else {
                memcpy(target.as_property, &prototype.properties[i], sizeof(Property));
            }

            if (source.as_property->type == PropertyType::OBJECT) {
                target.as_property->as_object.type = source.as_property->objType;
            }

            source.as_property++;
            target.as_property++;
        }
        return handle;
    }

    void DestroyObject(ObjectHandle handle)
    {
        if (handle == 0) { return; }
        auto indexEntry = objectIndex.Get(handle);
        if (!indexEntry) { return; }

        auto header = (NodeHeader*)indexEntry->ptr;
        if (header->prev) {
            header->prev->next = header->next;
        }
        if (header->next) {
            header->next->prev = header->prev;
        }
        if (header == firstObj) { firstObj = header->next; }

        blockAllocator->Free(indexEntry->ptr);
        objectIndex.Free(handle);
    }

    struct TypeInfo
    {
        TypeHandle      typeHandle;
        TypeHandle      baseType;

        const char*     name        = nullptr;
        PropertyDesc*   properties  = nullptr;
        size_t          numProperties = 0;
    };

    struct Object
    {
        ObjectHandle    objHandle;
        TypeHandle      type;
        const char*     name = nullptr;
        Property*       properties = nullptr;
        size_t          numProperties = 0;
    };

    TypeInfo GetTypeInfo(TypeHandle handle)
    {
        if (handle == 0) { return TypeInfo(); }
        auto indexEntry = typeIndex.Get(handle);
        if (!indexEntry) { return TypeInfo(); }
        union {
            void* as_void;
            NodeHeader* as_header;
            PropertyDesc* as_property;
        };
        TypeInfo result;
        as_void = indexEntry->ptr;
        auto numProperties = as_header->numProperties;
        result.name = as_header->name;
        result.numProperties = numProperties;
        result.typeHandle = handle;
        if (as_header->parent != nullptr) {
            result.baseType = as_header->parent->typeHandle;
        } else {
            result.baseType = 0;
        }
        as_header++;
        result.properties = as_property;
        return result;
    }

    void GetAllTypeInfos(TypeInfo* outTypeInfo, size_t* outNumTypes)
    {
        auto it = firstType;
        size_t numTypes = 0;
        while (it != nullptr) {
            if (outTypeInfo) {
                union {
                
                    NodeHeader* as_header;
                    PropertyDesc* as_property;
                };
                TypeInfo result;
                as_header = it;
                auto numProperties = as_header->numProperties;
                result.name = as_header->name;
                result.numProperties = numProperties;
                result.typeHandle = it->typeHandle;
                if (as_header->parent != nullptr) {
                    result.baseType = as_header->parent->typeHandle;
                }
                else {
                    result.baseType = 0;
                }
                as_header++;
                result.properties = as_property;
                outTypeInfo[numTypes] = result;
            }
            numTypes++;
            it = it->next;
        }
        if (outNumTypes) { *outNumTypes = numTypes; }
    }

    Object GetObject(ObjectHandle handle)
    {
        if (handle == 0) { return Object(); }
        auto indexEntry = objectIndex.Get(handle);
        if (!indexEntry) { return Object(); }
        union {
            void* as_void;
            NodeHeader* as_header;
            Property* as_property;
        };
        Object result;
        as_void = indexEntry->ptr;
        auto numProperties = as_header->numProperties;
        result.name = as_header->name;
        result.numProperties = numProperties;
        result.objHandle = handle;
        result.type = as_header->typeHandle;
        as_header++;
        result.properties = as_property;
        return result;
    }

    void GetAllObjects(Object* outObject, size_t* outNumObjects)
    {
        auto it = firstObj;
        size_t numObjects = 0;
        while (it != nullptr) {
            if (outObject) {
                union {
                    NodeHeader* as_header;
                    Property* as_property;
                };
                Object result;
                as_header = it;
                auto numProperties = as_header->numProperties;
                result.name = as_header->name;
                result.numProperties = numProperties;
                result.type = as_header->typeHandle;
                result.objHandle = as_header->objectHandle;
                as_header++;
                result.properties = as_property;
                outObject[numObjects] = result;
            }
            numObjects++;
            it = it->next;
        }
        if (outNumObjects) { *outNumObjects = numObjects; }
    }

    // true -> a is derived from b somehow
    bool IsTypeDerivedFrom(TypeHandle a, TypeHandle b)
    {
        if (a == b) { return true; }
        auto nodeA = (NodeHeader*)typeIndex.Get(a)->ptr;
        auto it = nodeA->parent;
        while (it != nullptr) {
            if (it->typeHandle == b) { return true; }
            it = it->parent;
        }
        return false;
    }
};

void PropertyView(ObjectDatabase::Object obj, ObjectDatabase* objDatabase, ObjectDatabase::Object* objects, size_t numObjects, int level = 0) 
{
    if (obj.objHandle == 0) { 
        
        return; 
    }
    ObjectDatabase::TypeInfo typeInfo = objDatabase->GetTypeInfo(obj.type);

    if (obj.objHandle != 0) {
        ImGui::PushID(level);
        for (size_t i = 0; i < obj.numProperties; ++i) {

            ImGui::PushID((int)i);
            ImGui::Text("%s", typeInfo.properties[i].name);
            ImGui::SameLine(150.0f + level * 20.0f);
            switch (obj.properties[i].type) {
            case PropertyType::FLOAT3: {
                ImGui::DragFloat3("", obj.properties[i].as_float3);
            } break;
            case PropertyType::STRING: {
                ImGui::InputText("", obj.properties[i].as_string.buf, STRING_PROPERTY_SIZE);
            } break;
            case PropertyType::OBJECT: {
                if (obj.properties[i].as_object.handle != 0) {
                    if (ImGui::Button(ICON_FA_CHAIN_BROKEN "  Break Tie")) {
                        obj.properties[i].as_object.handle = 0;
                    }
                    auto handle = objDatabase->GetObject(obj.properties[i].as_object.handle);
                    if (handle.objHandle != 0) {
                        ImGui::SameLine();
                        PropertyView(handle, objDatabase, objects, numObjects, level + 1);
                    }
                    else {
                        obj.properties[i].as_object.handle = 0;
                    }
                }
                else {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 0.6f));
                    ImGui::Text("null");
                    if (ImGui::IsItemClicked()) {
                        ImGui::OpenPopup("##objectSelector");
                    }
                    ImGui::PopStyleColor();

                    if (ImGui::BeginPopup("##objectSelector")) {
                        for (size_t j = 0; j < numObjects; ++j) {
                            if (obj.properties[i].as_object.type == 0 || objDatabase->IsTypeDerivedFrom(objects[j].type, obj.properties[i].as_object.type)) {
                                if (ImGui::Selectable(objects[j].name, false)) {
                                    obj.properties[i].as_object.handle = objects[j].objHandle;
                                    ImGui::CloseCurrentPopup();
                                }
                            }
                        }
                        ImGui::EndPopup();
                    } 
                }
            } break;
            case PropertyType::FLOAT: {
                ImGui::DragFloat("", &obj.properties[i].as_float);
            } break;
            case PropertyType::INT: {
                ImGui::DragInt("", &obj.properties[i].as_int);
            } break;
            case PropertyType::BOOL: {
                ImGui::Checkbox("", &obj.properties[i].as_bool);
            } break;
            case PropertyType::COLOR_RGBA: {
                ImGui::ColorEdit4("", obj.properties[i].as_colorRGBA);
            } break;
            default: {
                ImGui::Text("");
            } break;
            }
            ImGui::PopID();
        }
        ImGui::PopID();
    }
}


extern "C" __declspec(dllexport)
void Update(void* userData, ImGuiContext* imguiContext, runtime::UIContext* uiCtx, entity_system::World* world, renderer::RenderWorld* renderWorld, fnd::memory::LinearAllocator* frameAllocator, entity_system::Entity** entitySelection, size_t* numEntitiesSelected)
{

    auto editor = (Editor*)userData;

    editor->runtime->SetMainWindowTitle(editor->currentProject.basePath);

    auto entitySystem = (entity_system::EntitySystemInterface*) editor->apiRegistryInterface->Get(editor->apiRegistry, ENTITY_SYSTEM_API_NAME);
    assert(entitySystem);

    auto renderer = (renderer::RendererInterface*) editor->apiRegistryInterface->Get(editor->apiRegistry, RENDERER_API_NAME);
    assert(renderer);

    auto fbxImporter = (fbx_importer::FBXImportInterface*) editor->apiRegistryInterface->Get(editor->apiRegistry, FBX_IMPORTER_API_NAME);

    editor->currentWorld = world;
    editor->currentRenderWorld = renderWorld;
    editor->renderer = renderer;

    float camera[16];
    float projection[16];
    util::Copy4x4FloatMatrixCM(renderer->GetCameraTransform(renderWorld), camera);
    util::Copy4x4FloatMatrixCM(renderer->GetCameraProjection(renderWorld), projection);

    util::Make4x4FloatProjectionMatrixCMLH(projection, 1.0f, (float)1920, (float)1080, 0.1f, 1000.0f);
    renderer->SetCameraProjection(renderWorld, projection);

    ImGui::SetCurrentContext(imguiContext);

    ImGuizmo::BeginFrame();

    int WINDOW_WIDTH = 1920;
    int WINDOW_HEIGHT = 1080;

    static ObjectDatabase* objDatabase = nullptr;

    if (objDatabase == nullptr) {
        objDatabase = GT_NEW(ObjectDatabase, editor->applicationArena);

        static const size_t blockMemorySize = 1024 * 1024 * 5;
        void* blockMemory = editor->applicationArena->Allocate(blockMemorySize, 4, GT_SOURCE_INFO);
        objDatabase->blockAllocator = GT_NEW(fnd::memory::TLSFAllocator, editor->applicationArena)(blockMemory, blockMemorySize);
        objDatabase->objectIndex.Initialize(1024, editor->applicationArena);
        objDatabase->typeIndex.Initialize(1024, editor->applicationArena);
     
        static ObjectHandle rootGameObj = 0;
        static TypeHandle gameObjType = 0;

        GT_LOG_DEBUG("Object Database", "Created");
    
        {
            PropertyDesc gameObjectProperties[] = {
                { PropertyType::FLOAT3, "position", 0 },
                { PropertyType::FLOAT3, "rotation", 0 },
                { PropertyType::FLOAT3, "scale", 0 },

                { PropertyType::STRING, "description", 0 },
                { PropertyType::OBJECT, "parent", 0 }

            };
            ObjectTypeDesc gameObjectTypeDesc;
            gameObjectTypeDesc.name = "GameObject";
            gameObjectTypeDesc.numProperties = 5;
            gameObjectTypeDesc.properties = gameObjectProperties;
            gameObjType = objDatabase->RegisterType(&gameObjectTypeDesc);
            auto got = objDatabase->GetTypeInfo(gameObjType);
            got.properties[4].objType = gameObjType;

            rootGameObj = objDatabase->CreateObjectWithType("Root", gameObjType);
        }

        static TypeHandle doorType = 0;
        {
            PropertyDesc doorProperties[] = {
                { PropertyType::BOOL, "open", 0 },
                { PropertyType::BOOL, "locked", 0 },

            };
            ObjectTypeDesc doorTypeDesc;
            doorTypeDesc.name = "Door";
            doorTypeDesc.numProperties = 2;
            doorTypeDesc.properties = doorProperties;
            doorTypeDesc.baseType = gameObjType;
            doorType = objDatabase->RegisterType(&doorTypeDesc);
        }

        {
            PropertyDesc doorProperties[] = {
                { PropertyType::FLOAT, "angle", 0 }
            };
            ObjectTypeDesc doorTypeDesc;
            doorTypeDesc.name = "RevoltingDoor";
            doorTypeDesc.numProperties = 1;
            doorTypeDesc.properties = doorProperties;
            doorTypeDesc.baseType = gameObjType;
            doorTypeDesc.baseType = doorType;
            objDatabase->RegisterType(&doorTypeDesc);
        }

        static TypeHandle baseComponentType = 0;
        {
            PropertyDesc baseComponentProperties[] = {
                { PropertyType::OBJECT, "owner", gameObjType }
            };
            ObjectTypeDesc baseComponentTypeDesc;
            baseComponentTypeDesc.name = "BaseComponent";
            baseComponentTypeDesc.numProperties = 1;
            baseComponentTypeDesc.properties = baseComponentProperties;
            baseComponentType = objDatabase->RegisterType(&baseComponentTypeDesc);
        }

        {
            PropertyDesc healthComponentProperties[] = {
                { PropertyType::INT, "value", 0 }
            };
            ObjectTypeDesc healthComponentTypeDesc;
            healthComponentTypeDesc.name = "HealthComponent";
            healthComponentTypeDesc.numProperties = 1;
            healthComponentTypeDesc.properties = healthComponentProperties;
            healthComponentTypeDesc.baseType = baseComponentType;
            auto healthComponentType = objDatabase->RegisterType(&healthComponentTypeDesc);

            auto healthComponentPrototype = objDatabase->GetObject(objDatabase->CreateObjectWithType("health_component_prototype", healthComponentType));
            objDatabase->SetPrototype(healthComponentType, healthComponentPrototype.objHandle);
            healthComponentPrototype.properties[1].as_int = 100;
            healthComponentPrototype.properties[0].as_object.handle = 0;
        }

        static TypeHandle baseNodeType = 0;
        {

            ObjectTypeDesc blendNodeTypeDesc;
            blendNodeTypeDesc.name = "BaseNode";
            blendNodeTypeDesc.numProperties = 0;
            blendNodeTypeDesc.properties = nullptr;
            baseNodeType = objDatabase->RegisterType(&blendNodeTypeDesc);
        }


        {
            PropertyDesc blendNodeProperties[] = {
                { PropertyType::OBJECT, "inputA", baseNodeType },
                { PropertyType::OBJECT, "inputB", baseNodeType },
                { PropertyType::OBJECT, "output", baseNodeType }
            };
            ObjectTypeDesc blendNodeTypeDesc;
            blendNodeTypeDesc.name = "BlendNode";
            blendNodeTypeDesc.numProperties = 3;
            blendNodeTypeDesc.properties = blendNodeProperties;
            blendNodeTypeDesc.baseType = baseNodeType;
            objDatabase->RegisterType(&blendNodeTypeDesc);
        }

        {
            PropertyDesc colorNodeProperties[] = {
                { PropertyType::COLOR_RGBA, "color", 0 },
                { PropertyType::OBJECT, "output", baseNodeType }
            };
            ObjectTypeDesc colorNodeTypeDesc;
            colorNodeTypeDesc.name = "ColorConstantNode";
            colorNodeTypeDesc.numProperties = 2;
            colorNodeTypeDesc.properties = colorNodeProperties;
            colorNodeTypeDesc.baseType = baseNodeType;
            objDatabase->RegisterType(&colorNodeTypeDesc);
        }

        {
            PropertyDesc constantFloatNodeProperties[] = {
                { PropertyType::FLOAT, "value", 0 },
                { PropertyType::OBJECT, "output", baseNodeType }
            };
            ObjectTypeDesc typeDesc;
            typeDesc.name = "FloatConstantNode";
            typeDesc.numProperties = 2;
            typeDesc.properties = constantFloatNodeProperties;
            typeDesc.baseType = baseNodeType;
            objDatabase->RegisterType(&typeDesc);
        }

        static TypeHandle assetType = 0;
        {
            PropertyDesc assetProperties[] = {
                { PropertyType::INT, "guid", 0},
                { PropertyType::STRING, "display_name", 0},
                { PropertyType::INT, "version", 0}
            };
            ObjectTypeDesc typeDesc;
            typeDesc.name = "Asset";
            typeDesc.numProperties = 3;
            typeDesc.properties = assetProperties;
            assetType = objDatabase->RegisterType(&typeDesc);
        }


        {
            PropertyDesc meshAssetProperties[] = {
                { PropertyType::INT, "numVertices", 0 },
                { PropertyType::INT, "vertex_data_guid", 0 }
            };
            ObjectTypeDesc typeDesc;
            typeDesc.name = "MeshAsset";
            typeDesc.numProperties = 2;
            typeDesc.properties = meshAssetProperties;
            typeDesc.baseType = assetType;
            objDatabase->RegisterType(&typeDesc);
        }
    }
    else {
        if (ImGui::Begin("Object Database")) {
            size_t numTypes = 0;
            objDatabase->GetAllTypeInfos(nullptr, &numTypes);
            ObjectDatabase::TypeInfo* typeInfos = GT_NEW_ARRAY(ObjectDatabase::TypeInfo, numTypes, frameAllocator);
            objDatabase->GetAllTypeInfos(typeInfos, &numTypes);
            ImGui::Text("Types");
            ImGui::BeginChild("##typeList", ImVec2(ImGui::GetContentRegionAvailWidth(), 200), true);
            for (size_t i = 0; i < numTypes; ++i) {
                ObjectDatabase::TypeInfo baseTypeInfo;
                
                ImGui::Text("%s", typeInfos[i].name);
                if (typeInfos[i].baseType != 0) {
                    baseTypeInfo = objDatabase->GetTypeInfo(typeInfos[i].baseType);
                }

                if (ImGui::IsItemHovered()) {
                    auto bgColor = ImGui::GetStyleColorVec4(ImGuiCol_PopupBg);
                    bgColor.w = 0.9f;
                    ImGui::PushStyleColor(ImGuiCol_PopupBg, bgColor);
                    ImGui::BeginTooltip();
                    ImGui::Text("%s", typeInfos[i].name);
                    if (typeInfos[i].baseType != 0) {
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 0.5f));
                        ImGui::Text("%s", baseTypeInfo.name);
                        ImGui::PopStyleColor();
                    }
                    ImGui::Separator();
                    for (size_t j = 0; j < typeInfos[i].numProperties; ++j) {
                        ImGui::Text(ICON_FA_ARROW_RIGHT "  %s", typeInfos[i].properties[j].name);
                        ImGui::SameLine(150);
                        ImGui::Text("%s", g_propertyTypeNameTable[(int)typeInfos[i].properties[j].type]);
                    }
                    ImGui::EndTooltip();
                    ImGui::PopStyleColor();
                }
                ImGui::SameLine(150);
                ImGui::PushID((int)i);
                
                if (ImGui::Button(ICON_FA_USER_PLUS "  Create Instance")) {
                    objDatabase->CreateObjectWithType(typeInfos[i].name, typeInfos[i].typeHandle);
                }
                
                ImGui::PopID();
            }
            ImGui::EndChild();

            size_t numObjects = 0;
            objDatabase->GetAllObjects(nullptr, &numObjects);
            ObjectDatabase::Object* objects = GT_NEW_ARRAY(ObjectDatabase::Object, numObjects, frameAllocator);
            objDatabase->GetAllObjects(objects, &numObjects);

            ImGui::Text("Objects");
            ImGui::BeginChild("##properties", ImGui::GetContentRegionAvail(), true);
            for (size_t i = 0; i < numObjects; ++i) {
                auto fooObj = objects[i];
                ImGui::PushID((int)i);

                auto typeInfo = objDatabase->GetTypeInfo(objects[i].type);

                ImGui::BeginGroup();
                ImGui::Text("%s", objects[i].name);
                ImGui::Spacing();
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 0.5f));
                ImGui::Text("%s", typeInfo.name);
                ImGui::PopStyleColor();
                ImGui::EndGroup();
               
                ImGui::SameLine(ImGui::GetContentRegionAvailWidth() - 155);

                ImGui::BeginGroup();
                ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.0f, 0.0f));
                if (ImGui::Button(ICON_FA_USER_TIMES    "  Remove", ImVec2(150, 25))) {
                    objDatabase->DestroyObject(objects[i].objHandle);
                }
                if (ImGui::Button(ICON_FA_USER_CIRCLE   "  Make Prototype", ImVec2(150, 25))) {
                    objDatabase->SetPrototype(objects[i].type, objects[i].objHandle);
                }
                ImGui::PopStyleVar();
                ImGui::EndGroup();
                ImGui::PopID();

                ImGui::PushID((int)i);
                PropertyView(fooObj, objDatabase, objects, numObjects);
                ImGui::PopID();

                ImGui::Separator();
            }
            ImGui::EndChild();
        } ImGui::End();
    }

    auto canvasFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoInputs;
    ImGui::SetNextWindowSize(ImVec2((float)WINDOW_WIDTH, (float)WINDOW_HEIGHT));
    ImGui::Begin("##canvas", nullptr, ImVec2(0, 0), 0.0f, canvasFlags);
    ImGui::End();

    if (editor->drag.wasReleased) {
        editor->drag = Editor::DragContent();
    }
    if (!ImGui::IsMouseDown(MOUSE_LEFT)) {
        editor->drag.wasReleased = true;
    }

    if (editor->drag.type == Editor::DragContent::DRAG_TYPE_ASSET_REF) {
        ImGui::Begin("##canvas");
        ImGui::BeginTooltip();
        if (editor->drag.data.as_asset->type == Editor::Asset::Type::ASSET_TYPE_TEXTURE) {
            auto texHandle = renderer->GetTextureHandle(renderWorld, editor->drag.data.as_asset->asset);
            ImGui::Image((ImTextureID)(uintptr_t)(texHandle.id), ImVec2(128, 128));
        }
        char* displayString = editor->drag.data.as_asset->name;
        char formatBuf[512] = "";
        auto maxWidth = 128.0f;
        if (maxWidth > 0.0f) {
            ImFont* font = ImGui::GetCurrentContext()->Font;
            const float fontSize = ImGui::GetCurrentContext()->FontSize;

            // @NOTE this is probably a very inefficient way to do things
            char* str = editor->drag.data.as_asset->name;
            snprintf(formatBuf, 512, "...%s", str);
            ImVec2 textSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, formatBuf);
            while (textSize.x > maxWidth * 0.8f) {
                str++;
                snprintf(formatBuf, 512, "...%s", str);
                textSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, str);
            }
            displayString = formatBuf;
        }
        ImGui::Text("%s", displayString);
        ImGui::EndTooltip();
        ImGui::End();
    }

    //
    const char* VIEW_LABELS[] = {
        "NONE",

        ICON_FA_CAMERA "  Camera",
        ICON_FA_DATABASE "  Entity Explorer",
        ICON_FA_WRENCH "  Property Editor",
        ICON_FA_FILE_O "  Asset Browser",
        ICON_FA_CAMERA_RETRO "  Renderer",
        "Drag Debug",
        "Project Wizard"
    };


    static auto ImportTextureFromFile = [](Editor* state, const char* path, fnd::memory::LinearAllocator* allocator, renderer::RendererInterface* renderer, renderer::RenderWorld* renderWorld) -> bool {

        Editor::Asset* textureAsset = PushAsset(state, Editor::Asset::ASSET_TYPE_TEXTURE, path);
        {
            int width, height, numComponents;
            auto image = stbi_load(path, &width, &height, &numComponents, 4);
            //image = stbi_load_from_memory(buf, buf_len, &width, &height, &numComponents, 4);
            if (image == NULL) {
                GT_LOG_ERROR("Assets", "Failed to load image %s:\n%s\n", path, stbi_failure_reason());
                return false;
            }
            //assert(numComponents == 4);

            // mipmap generation

            static auto GetMipmapSize = [](int width, int height, int* widthOut, int* heightOut) -> void {

                *widthOut = width >> 1;
                *heightOut = height >> 1;
            };

            static auto GenMipmaps = [](uint8_t* imageIn, int width, int height, int numCompontents, uint8_t* imageOut) -> bool {
                if (width == 0 || height == 0) { return false; }
                if (numCompontents != 4) { return false; }

                cro_GenMipMapAvgI((int*)imageIn, width, height, (int*)imageOut);

                return true;
            };

            int numMipMapLevels = cro_GetMipMapLevels(width, height);
            uint8_t** mipmaps = (uint8_t**)allocator->Allocate(sizeof(uint8_t*) * numMipMapLevels, alignof(uint8_t*));
            int w = width;
            int h = height;
            mipmaps[0] = (uint8_t*)image;
            for (int i = 1; i < numMipMapLevels; ++i) {
                GetMipmapSize(w, h, &w, &h);
                mipmaps[i] = (uint8_t*)allocator->Allocate(sizeof(uint8_t) * 4 * w * h, alignof(int));
                auto res = GenMipmaps(mipmaps[i - 1], w * 2, h * 2, 4, mipmaps[i]);
                if (!res) {
                    GT_LOG_ERROR("Editor", "Failed to generate mipmap level %i for %s", i, path);
                    return false;
                }
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
                GetMipmapSize(w, h, &w, &h);
            }

            diffDesc.initialData = data;
            diffDesc.initialDataSizes = dataSizes;

            renderer::TextureDesc texDesc;
            texDesc.desc = diffDesc;
            textureAsset->as_texture.desc = texDesc;
            renderer->UpdateTextureLibrary(renderWorld, textureAsset->asset, &texDesc);

            stbi_image_free(image);
        }
        return true;
    };


    static auto ImportMaterialAsset = [](Editor* state, const char* name, renderer::MaterialDesc* desc, renderer::RendererInterface* renderer, renderer::RenderWorld* renderWorld) -> bool {
        Editor::Asset* materialAsset = PushAsset(state, Editor::Asset::ASSET_TYPE_MATERIAL, name);
        materialAsset->as_material.desc = *desc;
        renderer->UpdateMaterialLibrary(renderWorld, materialAsset->asset, desc);
        return true;
    };

    static auto ImportMeshAssetFromFile = [](Editor* state, const char* path, fnd::memory::LinearAllocator* allocator, renderer::RendererInterface* renderer, renderer::RenderWorld* renderWorld) -> bool {
        
        auto fbxImporter = (fbx_importer::FBXImportInterface*) state->apiRegistryInterface->Get(state->apiRegistry, FBX_IMPORTER_API_NAME);
        if (!fbxImporter) { return false; }

        Editor::Asset* meshAsset = PushAsset(state, Editor::Asset::ASSET_TYPE_MESH, path);
        {
            size_t modelFileSize = 0;
            fnd::memory::SimpleMemoryArena<fnd::memory::LinearAllocator> tempArena(allocator);
            void* modelFileData = LoadFileContents(path, &tempArena, &modelFileSize);
            if (modelFileData && modelFileSize > 0) {
                GT_LOG_INFO("Assets", "Loaded %s: %llu kbytes", path, modelFileSize / 1024);

                renderer::MeshDesc* meshDescs = GT_NEW_ARRAY(renderer::MeshDesc, 512, &tempArena);
                size_t numSubmeshes = 0;

                bool res = fbxImporter->FBXImportAsset(&tempArena, (char*)modelFileData, modelFileSize, meshDescs, &numSubmeshes);
                if (!res) {
                    GT_LOG_ERROR("Assets", "Failed to import %s", path);
                    return false;
                }
                else {
                    GT_LOG_INFO("Assets", "Imported %s", path);

                    meshAsset->as_mesh.numSubmeshes = numSubmeshes;
                    renderer->UpdateMeshLibrary(renderWorld, meshAsset->asset, meshDescs, numSubmeshes);
                }
            }
            else {
                GT_LOG_ERROR("Assets", "Failed to import %s", path);
                return false;
            }
        }
        return true;
    };

    static auto LoadScene = [](Editor* editor, const char* path, fnd::memory::LinearAllocator* allocator, renderer::RendererInterface* renderer, renderer::RenderWorld* renderWorld) -> bool {
        fnd::memory::SimpleMemoryArena<fnd::memory::LinearAllocator> arena(allocator);

        auto entitySystem = (entity_system::EntitySystemInterface*) editor->apiRegistryInterface->Get(editor->apiRegistry, ENTITY_SYSTEM_API_NAME);
        assert(entitySystem);


        size_t fileSize = 0;
        auto fileContents = LoadFileContents(path, &arena, &fileSize);
        if (!fileContents) {
            GT_LOG_ERROR("Editor", "Failed to load %s", path);
        }

        union {
            void* as_void;
            char* as_char;
            SceneFileHeader* as_header;
            Editor::Asset* as_asset;
        };
        as_void = fileContents;

        GT_LOG_DEBUG("Editor", "Loaded scene with header: { name = '%s', num assets = %i}", as_header->nameString, as_header->numAssets);

        auto numAssets = as_header->numAssets;
        as_header++;
        const char* assetTypeNames[] = { "NONE", "Texture", "Material", "Mesh" };
        for (uint32_t i = 0; i < numAssets; ++i) {

            switch (as_asset->type) {
                case Editor::Asset::ASSET_TYPE_TEXTURE: {
                    ImportTextureFromFile(editor, as_asset->name, allocator, renderer, renderWorld);
                } break;
                case Editor::Asset::ASSET_TYPE_MATERIAL: {
                    ImportMaterialAsset(editor, as_asset->name, &as_asset->as_material.desc, renderer, renderWorld);
                } break;
                case Editor::Asset::ASSET_TYPE_MESH: {
                    ImportMeshAssetFromFile(editor, as_asset->name, allocator, renderer, renderWorld);
                } break;
                default: {
                
                };
            }

            GT_LOG_DEBUG("Editor", "Loaded asset: { id = %i, path = '%s', type = %s }", as_asset->asset.id, as_asset->name, assetTypeNames[as_asset->type]);
            as_asset++;
        }

        size_t worldSize = 0;
        entitySystem->DeserializeWorld(editor->currentWorld, as_void, fileSize, &worldSize);    // @TODO use proper size
        as_char += worldSize;
        renderer->DeserializeRenderWorld(editor->currentRenderWorld, as_void, fileSize - worldSize, nullptr);   // @TODO use proper size

        return true;
    };

    static auto SaveScene = [](Editor* editor, const char* path, fnd::memory::LinearAllocator* allocator) -> bool {
        
        auto entitySystem = (entity_system::EntitySystemInterface*) editor->apiRegistryInterface->Get(editor->apiRegistry, ENTITY_SYSTEM_API_NAME);
        assert(entitySystem);

        SceneFileHeader header;
        header.SetName("Foo");
        header.numAssets = uint32_t(editor->materialAssetIndex + editor->meshAssetIndex + editor->textureAssetIndex) - 3;  // @NOTE -3 accounts for reserved zero assets

        union {
            char* as_char;
            SceneFileHeader* as_header;
            Editor::Asset* as_asset;
        };

        size_t worldSize = 0;
        entitySystem->SerializeWorld(editor->currentWorld, nullptr, 0, &worldSize);
        
        size_t renderWorldSize = 0;
        editor->renderer->SerializeRenderWorld(editor->currentRenderWorld, nullptr, 0, &renderWorldSize);

        size_t totalSize = sizeof(SceneFileHeader) + sizeof(Editor::Asset) * header.numAssets + worldSize + renderWorldSize;
        char* buf = as_char = (char*)allocator->Allocate(totalSize, 4);

        *as_header = header;
        as_header++;
        for (int i = 1; i < editor->textureAssetIndex; ++i) {
            *as_asset = editor->textureAssets[i];
            as_asset++;
        }
        for (int i = 1; i < editor->meshAssetIndex; ++i) {
            *as_asset = editor->meshAssets[i];
            as_asset++;
        }
        for (int i = 1; i < editor->materialAssetIndex; ++i) {
            *as_asset = editor->materialAssets[i];
            as_asset++;
        }

        entitySystem->SerializeWorld(editor->currentWorld, as_char, worldSize, nullptr);
        as_char += worldSize;
        editor->renderer->SerializeRenderWorld(editor->currentRenderWorld, as_char, renderWorldSize, nullptr);


        return DumpToFile(path, buf, totalSize);
    };

    //
    static void* tempScene = nullptr;
    size_t tempFileSize = 0;
    if (editor->frameIndex++ == 0) {
        fnd::memory::SimpleMemoryArena<fnd::memory::LinearAllocator> tempArena(frameAllocator);

        if (tempScene = LoadFileContents("temp.scene", editor->applicationArena, &tempFileSize)) {
            //ImGui::OpenPopup("Restore Session");
        }
    }

    if (tempScene != nullptr) {
        
        
        if (ImGui::Begin("Restore Session", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Want to restore last session?");

           
        } ImGui::End();
    }

    //
    if (ImGui::BeginMainMenuBar()) {

        // Project
        if (ImGui::BeginMenu(ICON_FA_FILE_O "  Project")) {
            if (ImGui::MenuItem("Create New")) {
                editor->views.enableView[Editor::Views::PROJECT_WIZARD] = true;
            }
            if (ImGui::MenuItem("Open")) {
                char* buf = (char*)frameAllocator->Allocate(sizeof(char) * Editor::FILENAME_BUF_SIZE, alignof(char));
                memset(buf, 0x0, sizeof(char) * Editor::FILENAME_BUF_SIZE);

                FileInfo fileInfo;
                if (OpenFileDialog(buf, Editor::FILENAME_BUF_SIZE, "Project Files\0*.gtproj\0", &fileInfo, 1, nullptr)) {

                    {   // switch project
                        fnd::memory::SimpleMemoryArena<fnd::memory::LinearAllocator> tempArena(frameAllocator);
                        LoadFileContents(fileInfo.path, &tempArena);

                        char* basePath = nullptr;
                        char* name = nullptr;

                        memset(editor->currentProject.basePath, 0x0, MAX_PATH);
                        memset(editor->currentProject.name, 0x0, MAX_PATH);

                        memcpy(editor->currentProject.basePath, buf, Editor::FILENAME_BUF_SIZE);
                        PathRemoveFileSpecA(editor->currentProject.basePath);
                        name = buf + strlen(buf) - 1 - strlen(".gtproj");
                        size_t nameLen = 0;
                        while (*name != '\\' && name != buf) {
                            name--;
                            nameLen++;
                        }
                        memcpy(editor->currentProject.name, name + 1, nameLen);
                        strncpy_s(editor->currentDirectory, MAX_PATH, editor->currentProject.basePath, MAX_PATH);

                    }
                }
            }
            if (ImGui::MenuItem("Save")) {
                auto fullPath = (char*)GT_NEW_ARRAY(char, MAX_PATH, frameAllocator);
                snprintf(fullPath, MAX_PATH, "%s%s.gtproj", (const char*)editor->currentProject.basePath, editor->currentProject.name);

                if (!DumpToFile(fullPath, &editor->currentProject, sizeof(Project))) {
                    GT_LOG_ERROR("Editor", "Failed to save project");
                }
            }
            if (ImGui::MenuItem("Save As...")) {
                char* buf = (char*)frameAllocator->Allocate(sizeof(char) * Editor::FILENAME_BUF_SIZE, alignof(char));
                memset(buf, 0x0, sizeof(char) * Editor::FILENAME_BUF_SIZE);

                if (SaveFileDialog(buf, Editor::FILENAME_BUF_SIZE, "Project Files\0*.gtproj\0", editor->currentProject.name)) {
                    if (!DumpToFile(buf, &editor->currentProject, sizeof(Project))) {
                        GT_LOG_ERROR("Editor", "Failed to save project");
                    }

                    char* basePath = nullptr;
                    char* name = nullptr;

                    memset(editor->currentProject.basePath, 0x0, MAX_PATH);
                    memset(editor->currentProject.name, 0x0, MAX_PATH);

                    memcpy(editor->currentProject.basePath, buf, Editor::FILENAME_BUF_SIZE);
                    PathRemoveFileSpecA(editor->currentProject.basePath);
                    name = buf + strlen(buf) - 1 - strlen(".gtproj");
                    size_t nameLen = 0;
                    while (*name != '\\' && name != buf) {
                        name--;
                        nameLen++;
                    }
                    memcpy(editor->currentProject.name, name + 1, nameLen);
                    strncpy_s(editor->currentDirectory, MAX_PATH, editor->currentProject.basePath, MAX_PATH);

                }
            }
            ImGui::EndMenu();
        }

        // File
        if (ImGui::BeginMenu(ICON_FA_FILE "  File")) {

            if (ImGui::MenuItem(ICON_FA_FLOPPY_O "  Save")) {
                char* buf = (char*)frameAllocator->Allocate(sizeof(char) * Editor::FILENAME_BUF_SIZE, alignof(char));
                memset(buf, 0x0, sizeof(char) * Editor::FILENAME_BUF_SIZE);

                if (SaveFileDialog(buf, Editor::FILENAME_BUF_SIZE, "Scene Files\0*.scene\0", "lcscene")) {
                    if (!SaveScene(editor, buf, frameAllocator)) {
                        GT_LOG_ERROR("Editor", "Failed to save scene");
                    }
                }
            }

            if (ImGui::MenuItem(ICON_FA_FOLDER_OPEN "  Open")) {
                char* buf = (char*)frameAllocator->Allocate(sizeof(char) * Editor::FILENAME_BUF_SIZE, alignof(char));
                memset(buf, 0x0, sizeof(char) * Editor::FILENAME_BUF_SIZE);
                
                FileInfo fileInfo;
                if (OpenFileDialog(buf, Editor::FILENAME_BUF_SIZE, "Scene Files\0*.scene\0", &fileInfo, 1, nullptr)) {
                    if (!LoadScene(editor, fileInfo.path, frameAllocator, renderer, renderWorld)) {
                        GT_LOG_ERROR("Editor", "Failed to load scene");
                    }
                }

            }

            ImGui::EndMenu();
        }   
        // View
        if (ImGui::BeginMenu(ICON_FA_WINDOWS "  View")) {
            for (size_t i = 1; i < Editor::Views::MAX_NUM_VIEWS && i < ARRAYSIZE(VIEW_LABELS); ++i) {
                if (ImGui::MenuItem(VIEW_LABELS[i], nullptr, &editor->views.enableView[i])) { }
            }
            ImGui::EndMenu();
        }

        ImGui::Text("%s%s.gtproj", (const char*)editor->currentProject.basePath, editor->currentProject.name);
        ImGui::EndMainMenuBar();
    } 

    //
    //
    
    static bool isEditProjectInitialized = false;
    if (editor->views.enableView[Editor::Views::PROJECT_WIZARD]) {
        if(editor->runtime->BeginView(uiCtx, VIEW_LABELS[Editor::Views::PROJECT_WIZARD])) {
        //if (ImGui::Begin(VIEW_LABELS[Editor::Views::PROJECT_WIZARD], &editor->views.enableView[Editor::Views::PROJECT_WIZARD], ImGuiWindowFlags_AlwaysAutoResize)) {

            static Project edit;
            static const char* error = "";
            static bool errorIsBad = false;

            if(!isEditProjectInitialized) {
                strncpy_s(edit.basePath, MAX_PATH, editor->currentProject.basePath, MAX_PATH);
                strncpy_s(edit.name, MAX_PATH, "MyProject", MAX_PATH);

                isEditProjectInitialized = true;
            };

            auto editFlags = ImGuiInputTextFlags_CharsNoBlank | ImGuiInputTextFlags_AutoSelectAll;

            ImGui::Text("Name"); ImGui::SameLine(80);
            ImGui::PushID("Name");
            ImGui::InputText("", edit.name, MAX_PATH, editFlags);
            ImGui::PopID();
            ImGui::SameLine();
            ImGui::Text(".gtproj");

            ImGui::Text("Location"); ImGui::SameLine(80);
            ImGui::InputText("", edit.basePath, MAX_PATH, editFlags);
            if (ImGui::IsItemHovered() && !ImGui::IsItemActive()) {
                ImGui::SetTooltip("%s", edit.basePath);
            }
        
           

            ImGui::SameLine();
            if (ImGui::Button(ICON_FA_FOLDER "  Browse...")) {
                if (!GetFolder(edit.basePath, "Choose Project Directory")) {

                }
                else {
                    
                }
            }

            auto fullPath = (char*)GT_NEW_ARRAY(char, MAX_PATH, frameAllocator);
            snprintf(fullPath, MAX_PATH, "%s%s.gtproj", (const char*)edit.basePath, edit.name);

            auto fileExists = DoesFileExist(fullPath);
            auto isPathValid = strlen(edit.basePath) > 0 && strlen(edit.name) > 0;
            bool canCreate = !fileExists && isPathValid;
            if (!canCreate) {
                errorIsBad = true;

                if (fileExists) {
                    error = "Project already exists!";
                }

                if (!isPathValid) {
                    error = "Path is invalid!";
                }
            }
            else {
                error = "Can create that project there!";
                errorIsBad = false;
            }


            if (ImGui::Button("Create")) {
                
                editor->currentProject = edit;

                if (canCreate) {
                    if (DumpToFile(fullPath, &editor->currentProject, sizeof(Project))) {

                    }
                    else {
                        error = "Failed to create project";
                        errorIsBad = true;
                    }
                    editor->views.enableView[Editor::Views::PROJECT_WIZARD] = false;
                }
                else {
                    
                }
            }
            
            ImGui::PushID("##error");
            ImGui::TextColored(errorIsBad ? ImVec4(1.0f, 0.0f, 0.0f, 1.0f) : ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "%s", error);
            ImGui::PopID();
        } ImGui::End();
    }
    else {  
        isEditProjectInitialized = false;
    }

    //
    //
    if (editor->views.enableView[Editor::Views::DRAG_DEBUG]) {
        if(ImGui::Begin(VIEW_LABELS[Editor::Views::DRAG_DEBUG], &editor->views.enableView[Editor::Views::DRAG_DEBUG])) {
        const char* fmt = editor->drag.type != Editor::DragContent::NONE ? "Dragging %s with delta %f, %f" : "Not dragging";
        if (editor->drag.type != Editor::DragContent::NONE) {
            ImGui::Text(fmt, editor->drag.data.as_asset->name, ImGui::GetMouseDragDelta().x, ImGui::GetMouseDragDelta().y);
        }
        else {
            ImGui::Text(fmt);
        }

    } ImGui::End();
    }

    enum CameraMode : int {
        CAMERA_MODE_ARCBALL = 0,
        CAMERA_MODE_FREE_FLY = 1
    };

    const char* modeStrings[] = { "Arcball", "Free Fly" };
    static CameraMode mode = CAMERA_MODE_ARCBALL;

    if (mode == CAMERA_MODE_ARCBALL) {
        if (ImGui::IsMouseDragging(MOUSE_RIGHT) || ImGui::IsMouseDragging(MOUSE_MIDDLE)) {
            if (ImGui::IsMouseDown(MOUSE_RIGHT)) {
                if (!ImGui::IsMouseDown(MOUSE_MIDDLE)) {
                    math::float3 camPosDelta;
                    camPosDelta.x = 2.0f * (-ImGui::GetMouseDragDelta(MOUSE_RIGHT).x / WINDOW_WIDTH);
                    camPosDelta.y = 2.0f * (ImGui::GetMouseDragDelta(MOUSE_RIGHT).y / WINDOW_HEIGHT);

                    math::float3 worldSpaceDelta = util::TransformDirectionCM(camPosDelta, editor->cameraRotation);
                    editor->camPos += worldSpaceDelta;
                }
                else {
                    math::float2 delta;
                    delta.x = 8.0f * (-ImGui::GetMouseDragDelta(MOUSE_RIGHT).x / WINDOW_WIDTH);
                    delta.y = 8.0f * (-ImGui::GetMouseDragDelta(MOUSE_RIGHT).y / WINDOW_HEIGHT);
                    float sign = -1.0f;
                    sign = delta.y > 0.0f ? 1.0f : -1.0f;
                    editor->camOffset.z -= math::Length(delta) * sign;
                    editor->camOffset.z = editor->camOffset.z > -0.1f ? -0.1f : editor->camOffset.z;
                }
            }
            else {
                editor->camYaw += 180.0f * (-ImGui::GetMouseDragDelta(MOUSE_MIDDLE).x / WINDOW_WIDTH);
                editor->camPitch += 180.0f * (-ImGui::GetMouseDragDelta(MOUSE_MIDDLE).y / WINDOW_HEIGHT);
            }
            ImGui::ResetMouseDragDelta(MOUSE_RIGHT);
            ImGui::ResetMouseDragDelta(MOUSE_MIDDLE);
        }
    }
    else {
        editor->camOffset = math::float3(0.0f);
        if (ImGui::IsMouseDragging(MOUSE_RIGHT) || ImGui::IsMouseDragging(MOUSE_MIDDLE)) {

            if (ImGui::IsMouseDown(MOUSE_RIGHT)) {
                if (!ImGui::IsMouseDown(MOUSE_MIDDLE)) {
                    math::float3 camPosDelta;
                    camPosDelta.x = 2.0f * (-ImGui::GetMouseDragDelta(MOUSE_RIGHT).x / WINDOW_WIDTH);
                    camPosDelta.y = 2.0f * (ImGui::GetMouseDragDelta(MOUSE_RIGHT).y / WINDOW_HEIGHT);
                    math::float3 worldSpaceDelta = util::TransformDirectionCM(camPosDelta, editor->cameraRotation);
                    editor->camPos += worldSpaceDelta;
                }
                else {
                    math::float3 camPosDelta;
                    camPosDelta.x = 2.0f * (-ImGui::GetMouseDragDelta(MOUSE_RIGHT).x / WINDOW_WIDTH);
                    camPosDelta.z = 2.0f * (ImGui::GetMouseDragDelta(MOUSE_RIGHT).y / WINDOW_HEIGHT);
                    math::float3 worldSpaceDelta = util::TransformDirectionCM(camPosDelta, editor->cameraRotation);
                    editor->camPos += worldSpaceDelta;
                }
            }
            else {
                editor->camYaw += 180.0f * (-ImGui::GetMouseDragDelta(MOUSE_MIDDLE).x / WINDOW_WIDTH);
                editor->camPitch += 180.0f * (-ImGui::GetMouseDragDelta(MOUSE_MIDDLE).y / WINDOW_HEIGHT);
            }
            ImGui::ResetMouseDragDelta(MOUSE_MIDDLE);
            ImGui::ResetMouseDragDelta(MOUSE_RIGHT);
        }
    }

    if (editor->views.enableView[Editor::Views::CAMERA_CONTROLS]) {
        if (ImGui::Begin(VIEW_LABELS[Editor::Views::CAMERA_CONTROLS], &editor->views.enableView[Editor::Views::CAMERA_CONTROLS])) {
            static float camOffsetStore = 0.0f;

            if (ImGui::Combo("Mode", (int*)&mode, modeStrings, 2)) {
                if (mode == CAMERA_MODE_ARCBALL) {
                    // we were in free cam mode before
                    // -> get stored cam offset, calculate what state->camPos must be based off that
                    editor->camOffset.z = camOffsetStore;
                    util::Make4x4FloatTranslationMatrixCM(editor->cameraOffset, editor->camOffset);
                    util::MultiplyMatricesCM(editor->cameraRotation, editor->cameraOffset, editor->camOffsetWithRotation);
                    math::float3 transformedOrigin = util::TransformPositionCM(math::float3(), editor->camOffsetWithRotation);
                    editor->camPos = editor->camPos - transformedOrigin;
                }
                else {
                    // we were in arcball mode before
                    // -> fold camera offset + rotation into cam pos, preserve offset 
                    float fullTransform[16];
                    util::Make4x4FloatTranslationMatrixCM(editor->cameraPos, editor->camPos);
                    util::MultiplyMatricesCM(editor->cameraPos, editor->camOffsetWithRotation, fullTransform);
                    editor->camPos = util::Get4x4FloatMatrixColumnCM(fullTransform, 3).xyz;
                    camOffsetStore = editor->camOffset.z;
                    editor->camOffset.z = 0.0f;
                }
            }


            ImGui::DragFloat("Camera Yaw", &editor->camYaw);
            ImGui::SameLine();
            if (ImGui::Button(ICON_FA_UNDO "##yaw")) {
                editor->camYaw = 0.0f;
            }

            ImGui::DragFloat("Camera Pitch", &editor->camPitch);
            ImGui::SameLine();
            if (ImGui::Button(ICON_FA_UNDO "##pitch")) {
                editor->camPitch = 0.0f;
            }

            ImGui::DragFloat3("Camera Offset", (float*)&editor->camOffset, 0.01f);
            ImGui::SameLine();
            if (ImGui::Button(ICON_FA_UNDO "##offset")) {
                editor->camOffset = math::float3(0.0f);
            }

            ImGui::DragFloat3("Camera Pos", (float*)&editor->camPos, 0.01f);
            ImGui::SameLine();
            if (ImGui::Button(ICON_FA_UNDO "##pos")) {
                editor->camPos = math::float3(0.0f);
            }
        } ImGui::End();
    }

    float cameraRotX[16], cameraRotY[16];
    util::Make4x4FloatRotationMatrixCMLH(cameraRotX, math::float3(1.0f, 0.0f, 0.0f), editor->camPitch * (3.141f / 180.0f));
    util::Make4x4FloatRotationMatrixCMLH(cameraRotY, math::float3(0.0f, 1.0f, 0.0f), editor->camYaw * (3.141f / 180.0f));


    util::Make4x4FloatTranslationMatrixCM(editor->cameraOffset, editor->camOffset);
    util::Make4x4FloatTranslationMatrixCM(editor->cameraPos, editor->camPos);
    util::MultiplyMatricesCM(cameraRotY, cameraRotX, editor->cameraRotation);

    util::MultiplyMatricesCM(editor->cameraRotation, editor->cameraOffset, editor->camOffsetWithRotation);
    util::MultiplyMatricesCM(editor->cameraPos, editor->camOffsetWithRotation, camera);

    float camInverse[16];
    util::Inverse4x4FloatMatrixCM(camera, camInverse);
    util::Copy4x4FloatMatrixCM(camInverse, camera);

    renderer->SetCameraTransform(renderWorld, camera);

    ImGuiWindowFlags windowFlags = 0;

    entity_system::Entity* entityList = GT_NEW_ARRAY(entity_system::Entity, 512, frameAllocator);
    size_t numEntities = 0;

    if (editor->views.enableView[Editor::Views::ENTITY_EXPLORER]) {
        if (ImGui::Begin(VIEW_LABELS[Editor::Views::ENTITY_EXPLORER], &editor->views.enableView[Editor::Views::ENTITY_EXPLORER], windowFlags)) {


            /* Add / delete of entities */

            if (ImGui::Button(ICON_FA_USER_PLUS "  Create New")) {
                entity_system::Entity ent = entitySystem->CreateEntity(world);
                if (!ImGui::GetIO().KeyCtrl) {
                    ClearList(&editor->entitySelection);
                }
                EntityNode* selectionNode = AllocateEntityNode(editor->entityNodePool, ENTITY_NODE_POOL_SIZE);
                selectionNode->ent = ent;
                AddToList(&editor->entitySelection, selectionNode);
                editor->lastSelected = ent;
            }

            entitySystem->GetAllEntities(world, entityList, &numEntities);

            if (editor->entitySelection.head != nullptr && entitySystem->IsEntityAlive(world, editor->entitySelection.head->ent)) {
                ImGui::SameLine();
                if (ImGui::Button(ICON_FA_USER_TIMES "  Delete")) {
                    EntityNode* it = editor->entitySelection.head;
                    while (it) {
                        entitySystem->DestroyEntity(world, it->ent);
                        renderer::StaticMesh meshComponent = renderer->GetStaticMesh(renderWorld, it->ent.id);
                        if (meshComponent.id != renderer::INVALID_ID) {
                            renderer->DestroyStaticMesh(renderWorld, meshComponent);
                        }


                        it = it->next;
                    }
                    ClearList(&editor->entitySelection);
                }
                ImGui::SameLine();
                if (ImGui::Button(ICON_FA_USERS "  Copy")) {
                    EntityNode* it = editor->entitySelection.head;
                    EntityNodeList copies;
                    while (it) {
                        auto newEntity = entitySystem->CopyEntity(world, it->ent);

                        renderer::StaticMesh meshComponent = renderer->GetStaticMesh(renderWorld, it->ent.id);
                        if (meshComponent.id != renderer::INVALID_ID) {
                            renderer->CopyStaticMesh(renderWorld, newEntity.id, meshComponent);
                        }

                        EntityNode* selectionNode = AllocateEntityNode(editor->entityNodePool, ENTITY_NODE_POOL_SIZE);
                        selectionNode->ent = newEntity;
                        AddToList(&copies, selectionNode);
                        editor->lastSelected = newEntity;
                        it = it->next;
                    }
                    ClearList(&editor->entitySelection);
                    it = copies.head;
                    while (it) {
                        auto next = it->next;
                        AddToList(&editor->entitySelection, it);
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

                int lastSelectedIndex = GetIndex(editor->lastSelected, entityList, numEntities);
                ImGui::PushStyleColor(ImGuiCol_Header, lastSelectedIndex == (int)i ? ImVec4(0.2f, 0.4f, 1.0f, 1.0f) : ImGui::GetStyle().Colors[ImGuiCol_Header]);
                bool select = ImGui::Selectable(name, IsEntityInList(&editor->entitySelection, entity));
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(MOUSE_LEFT)) {
                    editor->camPos = util::Get4x4FloatMatrixColumnCM(entitySystem->GetEntityTransform(world, entity), 3).xyz;
                }
                if (select) {
                    ImGui::PopStyleColor();

                    if (ImGui::GetIO().KeyShift || ImGui::GetIO().KeyCtrl) {  // multiselection
                        if (ImGui::GetIO().KeyShift) {
                            EntityNode* node = nullptr;

                            int clickedIndex = (int)i;
                            if (clickedIndex > lastSelectedIndex) {
                                for (int i = lastSelectedIndex; i <= clickedIndex; ++i) {
                                    if (!IsEntityInList(&editor->entitySelection, entityList[i], &node) && !ImGui::GetIO().KeyCtrl) {
                                        EntityNode* selectionNode = AllocateEntityNode(editor->entityNodePool, ENTITY_NODE_POOL_SIZE);
                                        selectionNode->ent = entityList[i];
                                        AddToList(&editor->entitySelection, selectionNode);
                                    }
                                    else {
                                        if (ImGui::GetIO().KeyCtrl) {
                                            RemoveFromList(&editor->entitySelection, node);
                                        }
                                    }
                                }
                            }
                            else {
                                if (clickedIndex < lastSelectedIndex) {
                                    for (int i = lastSelectedIndex; i >= clickedIndex; --i) {
                                        if (!IsEntityInList(&editor->entitySelection, entityList[i], &node) && !ImGui::GetIO().KeyCtrl) {
                                            EntityNode* selectionNode = AllocateEntityNode(editor->entityNodePool, ENTITY_NODE_POOL_SIZE);
                                            selectionNode->ent = entityList[i];
                                            AddToList(&editor->entitySelection, selectionNode);
                                        }
                                        else {
                                            if (ImGui::GetIO().KeyCtrl) {
                                                RemoveFromList(&editor->entitySelection, node);
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
                            if (!IsEntityInList(&editor->entitySelection, entity, &node)) {  // ADD
                                EntityNode* selectionNode = AllocateEntityNode(editor->entityNodePool, ENTITY_NODE_POOL_SIZE);
                                selectionNode->ent = entity;
                                AddToList(&editor->entitySelection, selectionNode);
                            }
                            else {  // REMOVE
                                RemoveFromList(&editor->entitySelection, node);
                                FreeEntityNode(node);
                            }
                        }
                    }
                    else {  // SET selection
                        ClearList(&editor->entitySelection);
                        EntityNode* selectionNode = AllocateEntityNode(editor->entityNodePool, ENTITY_NODE_POOL_SIZE);
                        selectionNode->ent = entity;
                        AddToList(&editor->entitySelection, selectionNode);
                    }

                    if (IsEntityInList(&editor->entitySelection, entity)) {
                        editor->lastSelected = entity;
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
                ClearList(&editor->entitySelection);
            }
            ImGui::EndChild();

        } ImGui::End();
    }

    entitySystem->GetAllEntities(world, entityList, &numEntities);
    for (size_t i = 0; i < numEntities; ++i) {
        //ImGuizmo::DrawCube(camera, projection, entitySystem->GetEntityTransform(world, entityList[i]));
    }

    if (editor->views.enableView[Editor::Views::ASSET_BROWSER]) {
        if (ImGui::Begin(VIEW_LABELS[Editor::Views::ASSET_BROWSER], &editor->views.enableView[Editor::Views::ASSET_BROWSER])) {

            static const size_t TAB_ASSET_VIEW = 0;
            static const size_t TAB_FILESYSTEM_VIEW = 1;

            static size_t activeTab = TAB_FILESYSTEM_VIEW;

            auto buttonColor = ImGui::GetStyle().Colors[ImGuiCol_Button];

            ImGui::PushStyleColor(ImGuiCol_Button, activeTab == TAB_ASSET_VIEW ? buttonColor : ImVec4(buttonColor.x, buttonColor.y, buttonColor.z, buttonColor.w * 0.5f));
            if (ImGui::Button("Asset View")) {
                activeTab = TAB_ASSET_VIEW;
            }
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Button, activeTab == TAB_FILESYSTEM_VIEW ? buttonColor : ImVec4(buttonColor.x, buttonColor.y, buttonColor.z, buttonColor.w * 0.5f));
            if (ImGui::Button("Filesystem View")) {
                activeTab = TAB_FILESYSTEM_VIEW;
            }
            ImGui::PopStyleColor();


            ImGui::Separator();
            ImGui::Spacing();
            ImGui::BeginChild("##view", ImGui::GetContentRegionAvail());
            ImGui::Spacing();


            if (activeTab == TAB_FILESYSTEM_VIEW) {
                ImGui::Text("Asset Source Directory");
                ImGui::InputText("", editor->currentDirectory, MAX_PATH);
                ImGui::SameLine();
                if (ImGui::Button(ICON_FA_FOLDER "  Browse...")) {
                    if (GetFolder(editor->currentDirectory, "Pick asset source directory")) {

                    }
                }

                ImGui::BeginChild("##listview", ImGui::GetContentRegionAvail(), true);
                char formatBuf[MAX_PATH];
                snprintf(formatBuf, MAX_PATH, "%s %s", ICON_FA_FOLDER_O, (const char*)editor->currentDirectory);
                if (ImGui::TreeNode(formatBuf)) {
                    ListDirectoryContents(editor->currentDirectory);
                    ImGui::TreePop();
                }
                ImGui::EndChild();
            }

            if (activeTab == TAB_ASSET_VIEW) {

                ImGui::Spacing();
                {   // Textures
                    ImGui::Text("Textures");
                    ImGui::Spacing();
                    ImGui::PushID("##importtex");
                    bool pushed = ImGui::Button("Import");
                    ImGui::PopID();
                    if (pushed) {
                        const size_t maxNumItems = 64;
                        char* buf = GT_NEW_ARRAY(char, Editor::FILENAME_BUF_SIZE * maxNumItems, frameAllocator);
                        FileInfo* files = GT_NEW_ARRAY(FileInfo, maxNumItems, frameAllocator);
                        size_t numItems = 0;
                        if (OpenFileDialog(buf, Editor::FILENAME_BUF_SIZE * maxNumItems, "All Image Files\0*.png;*.jpeg;*.jpg;*.tga;*.hdr\0PNG Image Files\0*.png\0JPG Image Files\0*.jpeg;*.jpg\0TGA Image Files\0*.tga\0HDR Image Files\0*.hdr\0", files, maxNumItems, &numItems)) {

                            for (size_t i = 0; i < numItems; ++i) {
                                GT_LOG_DEBUG("Editor", "Trying to import %s", files[i].path);
                                ImportTextureFromFile(editor, files[i].path, frameAllocator, renderer, renderWorld);
                            }
                        }
                    }

                    ImGui::Spacing();
                    static float displayScale = 1.0f;
                    ImGui::SliderFloat("##displayScale", &displayScale, 0.5f, 4.0f);

                    ImGui::BeginChild("##textures", ImVec2(ImGui::GetContentRegionAvailWidth(), 400), false, ImGuiWindowFlags_HorizontalScrollbar);
                    ImGui::Spacing();
                    size_t numDisplayColumns = (size_t)(ImGui::GetContentRegionAvailWidth() / (displayScale * 128.0f));
                    numDisplayColumns = numDisplayColumns > 1 ? numDisplayColumns : 1;
                    for (size_t i = 1; i < editor->textureAssetIndex; ++i) {
                        if (editor->textureAssets[i].asset.id == 0) { continue; }
                        auto texHandle = renderer->GetTextureHandle(renderWorld, editor->textureAssets[i].asset);
                        if (((i - 1) % numDisplayColumns) != 0) {
                            ImGui::SameLine();
                        }
                        else {
                            ImGui::Spacing();
                        }
                        {   // thumbnail group
                            ImGui::BeginGroup();

                            float width = editor->textureAssets[i].as_texture.desc.desc.width;
                            float height = editor->textureAssets[i].as_texture.desc.desc.height;
                            float ratio = width / height;

                            const ImVec2 thumbnailSize = ImVec2(128 * displayScale, 128 * displayScale);
                            if (width > thumbnailSize.x) {
                                width = thumbnailSize.x;
                                height = width / ratio;
                            }
                            if (height > thumbnailSize.y) {
                                height = thumbnailSize.y;
                                width = height * ratio;
                            }

                            auto drawList = ImGui::GetWindowDrawList();
                            auto cursorPos = ImGui::GetCursorScreenPos();
                            drawList->AddRectFilled(cursorPos, ImVec2(cursorPos.x + thumbnailSize.x, cursorPos.y + thumbnailSize.y), ImGui::ColorConvertFloat4ToU32(ImVec4(0.0f, 0.0f, 0.0f, 1.0f)));
                            auto offset = ImGui::GetCursorPos();
                            offset.x += thumbnailSize.x * 0.5f - width * 0.5f;
                            offset.y += thumbnailSize.y * 0.5f - height * 0.5f;
                            ImGui::SetCursorPos(offset);
                            ImGui::Image((ImTextureID)(uintptr_t)(texHandle.id), ImVec2(width, height));
                            ImGui::SetCursorScreenPos(cursorPos);
                            ImGui::Dummy(thumbnailSize);

                            if (ImGui::IsItemHovered()) {
                                ImGui::BeginTooltip();
                                ImGui::Text("%s", editor->textureAssets[i].name);
                                ImGui::EndTooltip();
                            }
                            Editor::Asset* asset = &editor->textureAssets[i];
                            ImGui::PushID((int)i);
                            AssetRefLabel(editor, asset, false, thumbnailSize.x);
                            ImGui::PopID();
                            ImGui::EndGroup();
                        }
                    }
                    ImGui::EndChild();
                }
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();
                {   // materials
                    ImGui::Text("Materials");

                    bool pushed = ImGui::Button("Create New");

                    static char nameEditBuf[Editor::FILENAME_BUF_SIZE];
                    Editor::MaterialAsset* editAsset = nullptr;
                    if (pushed) {
                        ImGui::OpenPopup("Material Editor");
                        snprintf(nameEditBuf, Editor::FILENAME_BUF_SIZE, "Material%llu", editor->materialAssetIndex);
                    }

                    if (ImGui::BeginPopup("Material Editor")) {

                        static renderer::MaterialDesc desc;
                        static core::Asset* texSlot = nullptr;

                        ImGui::InputText("##name", nameEditBuf, Editor::FILENAME_BUF_SIZE);

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
                            for (size_t i = 0; i < editor->textureAssetIndex; ++i) {
                                if (editor->textureAssets[i].asset.id == 0) { continue; }
                                auto texHandle = renderer->GetTextureHandle(renderWorld, editor->textureAssets[i].asset);
                                ImGui::Image((ImTextureID)(uintptr_t)(texHandle.id), ImVec2(128, 128));
                                if (ImGui::IsItemHovered()) {
                                    ImGui::BeginTooltip();
                                    ImGui::Text("%s", editor->textureAssets[i].name);
                                    ImGui::EndTooltip();

                                    if (ImGui::IsMouseClicked(MOUSE_LEFT)) {
                                        *texSlot = editor->textureAssets[i].asset;
                                        //ImGui::CloseCurrentPopup();
                                    }
                                }

                                if (i != 0 && (i % 4) != 0) { ImGui::SameLine(); }
                            }
                            //ImGui::EndChild();
                            ImGui::EndPopup();
                        }

                        bool isValid = desc.baseColorMap.id != 0 && desc.roughnessMap.id != 0 && desc.metalnessMap.id != 0 && desc.normalVecMap.id != 0 && desc.occlusionMap.id != 0;
                        bool done = ImGui::Button("Done", ImVec2(100, 25)) && isValid;
                        ImGui::SameLine();
                        bool cancel = ImGui::Button("Cancel", ImVec2(100, 25));
                        if (done) {
                            ImportMaterialAsset(editor, nameEditBuf, &desc, renderer, renderWorld);

                            desc = renderer::MaterialDesc();
                        }

                        if (done || cancel) {
                            ImGui::CloseCurrentPopup();
                        }
                        ImGui::EndPopup();
                    }

                    for (size_t i = 0; i < editor->materialAssetIndex; ++i) {
                        Editor::Asset* asset = &editor->materialAssets[i];
                        if (asset->asset.id == 0) { continue; }

                        ImGui::PushID((int)i);
                        AssetRefLabel(editor, asset, false);
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
                        char* buf = GT_NEW_ARRAY(char, Editor::FILENAME_BUF_SIZE * maxNumItems, frameAllocator);
                        FileInfo* files = GT_NEW_ARRAY(FileInfo, maxNumItems, frameAllocator);
                        size_t numItems = 0;
                        if (OpenFileDialog(buf, Editor::FILENAME_BUF_SIZE * maxNumItems, "FBX Files\0*.fbx\0", files, maxNumItems, &numItems)) {

                            for (size_t i = 0; i < numItems; ++i) {
                                GT_LOG_DEBUG("Editor", "Trying to import %s", files[i].path);

                                ImportMeshAssetFromFile(editor, files[i].path, frameAllocator, renderer, renderWorld);
                            }
                        }
                    }

                    ImGui::BeginChild("##meshes", ImVec2(ImGui::GetContentRegionAvailWidth(), 400), false, ImGuiWindowFlags_HorizontalScrollbar);
                    ImGui::Spacing();
                    for (size_t i = 0; i < editor->meshAssetIndex; ++i) {
                        if (editor->meshAssets[i].asset.id == 0) { continue; }
                        ImGui::PushID((int)i);
                        AssetRefLabel(editor, &editor->meshAssets[i], false);
                        ImGui::PopID();
                    }
                    ImGui::EndChild();
                }

            }

            ImGui::EndChild();
        } ImGui::End();
    }

    //ImGui::ShowTestWindow();
    if (editor->views.enableView[Editor::Views::RENDERER_SETTINGS]) {
        if (ImGui::Begin(VIEW_LABELS[Editor::Views::RENDERER_SETTINGS], &editor->views.enableView[Editor::Views::RENDERER_SETTINGS])) {
            ImGui::Text("Active environment map");
            ImGui::InputInt("", (int*)renderer->GetActiveCubemap(renderWorld));
        } ImGui::End();
    }

    if (editor->views.enableView[Editor::Views::PROPERTY_EDITOR]) {
        if (ImGui::Begin(VIEW_LABELS[Editor::Views::PROPERTY_EDITOR], &editor->views.enableView[Editor::Views::PROPERTY_EDITOR])) {
            static entity_system::Entity selectedEntity = { entity_system::INVALID_ID };
            if (!IsEntityInList(&editor->entitySelection, selectedEntity)) {
                selectedEntity = { entity_system::INVALID_ID };
            }
            EntityNode* it = editor->entitySelection.head;
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

            ImGui::Text("Editing %s", editor->isEditing ? ICON_FA_CHECK : ICON_FA_TIMES);

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

                    auto LookupMeshAsset = [](core::Asset ID, Editor::Asset* assets, size_t numAssets) -> Editor::Asset* {
                        for (size_t i = 0; i < numAssets; ++i) {
                            if (assets[i].asset == ID && assets[i].type == Editor::Asset::ASSET_TYPE_MESH) {
                                return &assets[i];
                            }
                        }
                        return nullptr;
                    };
                    auto LookupMaterialAsset = [](core::Asset ID, Editor::Asset* assets, size_t numAssets) -> Editor::Asset* {
                        for (size_t i = 0; i < numAssets; ++i) {
                            if (assets[i].asset == ID && assets[i].type == Editor::Asset::ASSET_TYPE_MATERIAL) {
                                return &assets[i];
                            }
                        }
                        return nullptr;
                    };

                    renderer::StaticMesh mesh = renderer->GetStaticMesh(renderWorld, selectedEntity.id);

                    ImGui::Text("(#%i)", mesh.id);

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

                    auto asset = LookupMeshAsset(meshAsset, editor->meshAssets, editor->meshAssetIndex);
                    ImGui::PushID(-1);
                    auto newMesh = AssetRefLabel(editor, asset, true);
                    ImGui::PopID();
                    if (newMesh != nullptr) {
                        anyChange = true;
                        meshAsset = newMesh->asset;
                    }
                    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(MOUSE_LEFT)) {
                        ImGui::OpenPopup("##meshPicker");
                    }
                    if (ImGui::BeginPopup("##meshPicker")) {
                        for (size_t i = 0; i < editor->meshAssetIndex; ++i) {
                            if (editor->meshAssets[i].asset.id == 0) { continue; }
                            ImGui::Text("%s", editor->meshAssets[i].name);
                            if (ImGui::IsItemHovered() && ImGui::IsItemClicked(MOUSE_LEFT)) {
                                meshAsset = editor->meshAssets[i].asset;
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
                        Editor::Asset* asset = LookupMaterialAsset(materials[0], editor->materialAssets, editor->materialAssetIndex);
                        ImGui::PushID(-1);
                        auto newMat = AssetRefLabel(editor, asset, true);
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
                        Editor::Asset* asset = LookupMaterialAsset(materials[i], editor->materialAssets, editor->materialAssetIndex);
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
                        auto newMat = AssetRefLabel(editor, asset, true);
                        ImGui::PopID();
                        if (newMat != nullptr) {
                            anyChange = true;
                            materials[i] = newMat->asset;
                        }
                    }
                    if (ImGui::BeginPopup("##materialPicker")) {
                        for (size_t i = 0; i < editor->materialAssetIndex; ++i) {
                            ImGui::Text("%s", editor->materialAssets[i].name);
                            if (ImGui::IsItemHovered() && ImGui::IsItemClicked(MOUSE_LEFT)) {
                                if (editMaterialIndex >= 0) {
                                    materials[editMaterialIndex] = editor->materialAssets[i].asset;
                                }
                                else {
                                    for (size_t j = 0; j < numSubmeshes; ++j) {
                                        materials[j] = editor->materialAssets[i].asset;
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
                            GT_LOG_DEBUG("Editor", "Destroying mesh component #%i", mesh.id);
                            renderer->DestroyStaticMesh(renderWorld, mesh);
                        }
                        auto newMesh = renderer->CreateStaticMesh(renderWorld, selectedEntity.id, meshAsset, materials, numSubmeshes);
                        GT_LOG_DEBUG("Editor", "Created mesh component #%i", newMesh.id);
                    }

                    ImGui::TreePop();
                }
            }
            ImGui::EndChild();

            math::float3 newPosition = util::Get4x4FloatMatrixColumnCM(groupTransform, 3).xyz;

            auto posDifference = newPosition - meanPosition;

            if (math::Length(posDifference) > 0.001f) {
                if (!editor->isEditing) {
                    editor->isEditing = true;

                }
            }
            else {
                if (editor->isEditing) {
                    editor->isEditing = false;
                }
            }


            it = editor->entitySelection.head;
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
    }

    *numEntitiesSelected = 0;
    auto it = editor->entitySelection.head;
    while (it != nullptr) {
        (*numEntitiesSelected)++;
        it = it->next;
    }

    if (*numEntitiesSelected > 0) {
        *entitySelection = (entity_system::Entity*)frameAllocator->Allocate(sizeof(entity_system::Entity) * (*numEntitiesSelected), alignof(entity_system::Entity));
        it = editor->entitySelection.head;
        int i = 0;
        while (it != nullptr) {
            (*entitySelection)[i++].id = it->ent.id;
            it = it->next;
        }
    }

    return;
}