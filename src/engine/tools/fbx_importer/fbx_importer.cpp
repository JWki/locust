
#include "fbx_importer.h"
#include "OpenFBX/ofbx.h"
#include <memory>


FBXScene FBXLoadSceneFromMemory(void* data, size_t dataSize)
{
    auto scene = ofbx::load((ofbx::u8*)data, (int)dataSize);
    return { scene };
}

FBXMesh FBXGetMeshWithIndex(FBXScene scene, int index)
{
    auto ofbxScene = (ofbx::IScene*)scene._ptr;
    return { (void*)ofbxScene->getMesh(index) };
}

size_t FBXGetMeshCount(FBXScene scene)
{
    auto ofbxScene = (ofbx::IScene*)scene._ptr;
    return ofbxScene->getMeshCount();
}


bool FBXGetMeshInfo(FBXMesh mesh, FBXMeshInfo* outInfo)
{
    auto ofbxMesh = (ofbx::Mesh*)mesh._ptr;
    const ofbx::Geometry* geom = ofbxMesh->getGeometry();
    
    outInfo->numVertices = geom->getVertexCount();
    outInfo->hasNormals = geom->getNormals() != nullptr;
    outInfo->hasTexcoords = geom->getUVs() != nullptr;

    return true;
}

bool FBXGetNormals(FBXMesh mesh, float* buffer, size_t bufferCapacity)
{
    auto ofbxMesh = (ofbx::Mesh*)mesh._ptr;
    const ofbx::Geometry* geom = ofbxMesh->getGeometry();
    auto normals = geom->getNormals();
    if (normals == nullptr) { return false; }
    if (bufferCapacity < geom->getVertexCount() * 3) { return false; }
    for (int i = 0; i < geom->getVertexCount(); ++i) {
        buffer[i * 3 + 0] = (float)normals[i].x;
        buffer[i * 3 + 1] = (float)normals[i].y;
        buffer[i * 3 + 2] = (float)normals[i].z;
    }
    return true;
}

bool FBXGetTexcoords(FBXMesh mesh, float* buffer, size_t bufferCapacity)
{
    auto ofbxMesh = (ofbx::Mesh*)mesh._ptr;
    const ofbx::Geometry* geom = ofbxMesh->getGeometry();
    auto uvs = geom->getUVs();
    if (uvs == nullptr) { return false; }
    if (bufferCapacity < geom->getVertexCount() * 2) { return false; }
    for (int i = 0; i < geom->getVertexCount(); ++i) {
        buffer[i * 2 + 0] = (float)uvs[i].x;
        buffer[i * 2 + 1] = 1.0f - (float)uvs[i].y;
    }
    return true;
}

bool FBXGetVertexPositions(FBXMesh mesh, float* buffer, size_t bufferCapacity)
{
    auto ofbxMesh = (ofbx::Mesh*)mesh._ptr;
    const ofbx::Geometry* geom = ofbxMesh->getGeometry();
    auto vertices = geom->getVertices();
    if (vertices == nullptr) { return false; }
    if (bufferCapacity < geom->getVertexCount() * 3) { return false; }
    for (int i = 0; i < geom->getVertexCount(); ++i) {
        buffer[i * 3 + 0] = (float)vertices[i].x;
        buffer[i * 3 + 1] = (float)vertices[i].y;
        buffer[i * 3 + 2] = (float)vertices[i].z;
    }
    return true;
}
