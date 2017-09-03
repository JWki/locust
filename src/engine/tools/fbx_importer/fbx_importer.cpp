
#include "fbx_importer.h"
#include "OpenFBX/ofbx.h"
#include <memory>
#include <foundation/logging/logging.h>



class SimpleFilterPolicy
{
public:
    bool Filter(fnd::logging::LogCriteria criteria)
    {
        auto h = fnd::logging::LogChannel("RenderProfile").hash;
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


class IDEConsoleFormatter
{
public:
    void Format(char* buf, size_t bufSize, fnd::logging::LogCriteria criteria, const char* format, va_list args)
    {
        size_t offset = snprintf(buf, bufSize, "%s(%llu): [%s]    ", criteria.scInfo.file, criteria.scInfo.line, criteria.channel.str);
        vsnprintf(buf + offset, bufSize - offset, format, args);
    }
};

#ifdef _MSC_VER
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif

class IDEConsoleWriter
{
public:
    void Write(const char* msg)
    {
#ifdef _MSC_VER
        OutputDebugStringA(msg);
        OutputDebugStringA("\n");
#endif
    }
};

class ConsoleWriter
{
public:
    void Write(const char* msg)
    {
        printf("%s\n", msg);
    }
};

#include <foundation/sockets/sockets.h>

class NetworkFilterPolicy
{
public:
    bool Filter(fnd::logging::LogCriteria criteria)
    {
        return criteria.channel.hash != fnd::logging::LogChannel("TCP Logger").hash
            && criteria.channel.hash != fnd::logging::LogChannel("RenderProfile").hash;
    }
};

class NetworkFormatPolicy
{
public:
    void Format(char* buf, size_t bufSize, fnd::logging::LogCriteria criteria, const char* format, va_list args)
    {
        size_t offset = snprintf(buf, bufSize, "[%s]    ", criteria.channel.str);
        vsnprintf(buf + offset, bufSize - offset, format, args);
    }
};


typedef fnd::logging::Logger<SimpleFilterPolicy, SimpleFormatPolicy, ConsoleWriter> SimpleLogger;
typedef fnd::logging::Logger<SimpleFilterPolicy, IDEConsoleFormatter, IDEConsoleWriter> IDEConsoleLogger;

SimpleLogger simpleLogger;


void CalculateTangents(fnd::math::float3* vertexBuffer, fnd::math::float2* uvBuffer, size_t numVertices, fnd::math::float3* tangentBuffer)
{
    using namespace fnd;

    // calculate tangents
    for (int i = 0; i < numVertices; i += 3) {
        math::float3 pos1 = vertexBuffer[i];
        math::float3 pos2 = vertexBuffer[i + 1];
        math::float3 pos3 = vertexBuffer[i + 2];

        math::float2 uv1 = uvBuffer[i];
        math::float2 uv2 = uvBuffer[i + 1];
        math::float2 uv3 = uvBuffer[i + 2];

   

        math::float3 edge1 = pos2 - pos1;
        math::float3 edge2 = pos3 - pos1;
        math::float2 deltaUV1 = uv2 - uv1;
        math::float2 deltaUV2 = uv3 - uv1;

        float f = 1.0f / (deltaUV1.x * deltaUV2.y - deltaUV2.x * deltaUV1.y);

        math::float3 tangent1;
        math::float3 bitangent1;
        tangent1.x = f * (deltaUV2.y * edge1.x - deltaUV1.y * edge2.x);
        tangent1.y = f * (deltaUV2.y * edge1.y - deltaUV1.y * edge2.y);
        tangent1.z = f * (deltaUV2.y * edge1.z - deltaUV1.y * edge2.z);
        tangent1 = math::Normalize(tangent1);

        bitangent1.x = f * (-deltaUV2.x * edge1.x + deltaUV1.x * edge2.x);
        bitangent1.y = f * (-deltaUV2.x * edge1.y + deltaUV1.x * edge2.y);
        bitangent1.z = f * (-deltaUV2.x * edge1.z + deltaUV1.x * edge2.z);
        bitangent1 = math::Normalize(bitangent1);

        for (int j = 0; j < 3; ++j) {
            tangentBuffer[i + j] = tangent1;
        }
    }
}



bool FBXImportAsset(fnd::memory::MemoryArenaBase* arena, char* fbxData, size_t fbxDataSize, MeshAsset* outAsset)
{
    using namespace fnd;

    GT_LOG_INFO("FBX Importer", "Importing FBX file...");

    ofbx::IScene* scene = ofbx::load((const ofbx::u8*)fbxData, (int)fbxDataSize);
    if (!scene) { return false; }

    uint32_t numVertices = 0;
    auto numMeshes = scene->getMeshCount();
    for (int i = 0; i < numMeshes; ++i) {
        const ofbx::Mesh* mesh = scene->getMesh(i);
        const ofbx::Geometry* geometry = mesh->getGeometry();

        numVertices += geometry->getVertexCount();
    }

    outAsset->numVertices = outAsset->numIndices = numVertices;

    outAsset->vertexPositions = GT_NEW_ARRAY(math::float3, numVertices, arena);
    outAsset->vertexNormals = GT_NEW_ARRAY(math::float3, numVertices, arena);
    outAsset->vertexTangents = GT_NEW_ARRAY(math::float3, numVertices, arena);
    outAsset->vertexUVs = GT_NEW_ARRAY(math::float2, numVertices, arena);

    outAsset->indexFormat = numVertices > UINT16_MAX ? MeshAsset::IndexFormat::UINT32 : MeshAsset::IndexFormat::UINT16;

    if (outAsset->indexFormat == MeshAsset::IndexFormat::UINT16) {
        outAsset->indices.as_uint16 = GT_NEW_ARRAY(uint16_t, numVertices, arena);
    }
    else {
        outAsset->indices.as_uint32 = GT_NEW_ARRAY(uint32_t, numVertices, arena);
    }

    size_t vertexOffset = 0;
    for (int i = 0; i < numMeshes; ++i) {
        const ofbx::Mesh* mesh = scene->getMesh(i);
        const ofbx::Geometry* geometry = mesh->getGeometry();

        ofbx::Matrix sourceGeometricTransform = mesh->getGeometricMatrix();
        ofbx::Matrix sourceGlobalTransform = mesh->getGlobalTransform();

        float geometricTransform[16];
        float globalTransform[16];

        for (int i = 0; i < 16; ++i) {
            geometricTransform[i] = (float)sourceGeometricTransform.m[i];
            globalTransform[i] = (float)sourceGlobalTransform.m[i];
        }

        size_t numSubmeshVertices = geometry->getVertexCount();

        math::float3* positionBuffer = outAsset->vertexPositions + vertexOffset;
        math::float3* normalBuffer = outAsset->vertexNormals + vertexOffset;
        math::float3* tangentBuffer = outAsset->vertexTangents + vertexOffset;
        math::float2* uvBuffer = outAsset->vertexUVs + vertexOffset;

        const ofbx::Vec3* sourcePositionBuffer = geometry->getVertices();
        const ofbx::Vec3* sourceNormalBuffer = geometry->getNormals();
        const ofbx::Vec3* sourceTangentBuffer = geometry->getTangents();
        const ofbx::Vec2* sourceUVBuffer = geometry->getUVs();

        float transform[16];
        util::MultiplyMatricesCM(globalTransform, geometricTransform, transform);

        float transposeTransform[16];
        float inverseTransposeTransform[16];
        util::Make4x4FloatMatrixTranspose(transform, transposeTransform);
        util::Inverse4x4FloatMatrixCM(transposeTransform, inverseTransposeTransform);

        if (sourcePositionBuffer) {
            for (size_t i = 0; i < numSubmeshVertices; ++i) {
                positionBuffer[i] = math::float3((float)sourcePositionBuffer[i].x, (float)sourcePositionBuffer[i].y, (float)sourcePositionBuffer[i].z);
                positionBuffer[i] = util::TransformPositionCM(positionBuffer[i], transform);
                positionBuffer[i].z *= -1.0f;
                positionBuffer[i] *= 0.01f;     // account for units
            }
        }
        else {
            // @TODO: handle missing positions
            GT_LOG_WARNING("FBX Importer", "Submesh #%i does not contain position data", i);
        }
        if (sourceNormalBuffer) {
            for (size_t i = 0; i < numSubmeshVertices; ++i) {
                normalBuffer[i] = math::float3((float)sourceNormalBuffer[i].x, (float)sourceNormalBuffer[i].y, (float)sourceNormalBuffer[i].z);
                normalBuffer[i] = util::TransformDirectionCM(normalBuffer[i], inverseTransposeTransform);
                normalBuffer[i].z *= -1.0f;
                //normalBuffer[i] *= 0.01f;
            }
        } 
        else {
            // @TODO: handle missing normals
            GT_LOG_WARNING("FBX Importer", "Submesh #%i does not contain surface normal data", i);
        }
        if (sourceUVBuffer) {
            for (size_t i = 0; i < numSubmeshVertices; ++i) {
                uvBuffer[i] = math::float2((float)sourceUVBuffer[i].x, 1.0f - (float)sourceUVBuffer[i].y);
            }
        }
        else {
            // @TODO: handle missing UVs
            GT_LOG_WARNING("FBX Importer", "Submesh #%i does not contain a UV set", i);
        }
        if (sourceTangentBuffer) {
            for (size_t i = 0; i < numSubmeshVertices; ++i) {
                tangentBuffer[i] = math::float3((float)sourceTangentBuffer[i].x, (float)sourceTangentBuffer[i].y, (float)sourceTangentBuffer[i].z);
                tangentBuffer[i] = util::TransformDirectionCM(tangentBuffer[i], inverseTransposeTransform);
                tangentBuffer[i].z *= -1.0f;
            }
        } 
        else {
            if (sourceUVBuffer) {
                GT_LOG_INFO("FBX Importer", "Calculating tangents for submesh #%i", i);
                CalculateTangents(positionBuffer, uvBuffer, numSubmeshVertices, tangentBuffer);
            }
            else {
                // @TODO: handle missing tangents and missing uvs
                GT_LOG_WARNING("FBX Importer", "Submesh #%i does not have tangents", i);
            }
        }
        vertexOffset += numSubmeshVertices;
    }

    for (uint32_t i = 0; i < numVertices; ++i) {
        if (outAsset->indexFormat == MeshAsset::IndexFormat::UINT16) {
            outAsset->indices.as_uint16[i] = (uint16_t)i;
        }
        else {
            outAsset->indices.as_uint32[i] = (uint32_t)i;
        }
    }

    for (uint32_t i = 0; i < numVertices; i += 3) {
        if (outAsset->indexFormat == MeshAsset::IndexFormat::UINT16) {
            uint16_t swap = outAsset->indices.as_uint16[i + 1];
            outAsset->indices.as_uint16[i + 1] = outAsset->indices.as_uint16[i];
            outAsset->indices.as_uint16[i] = swap;
        }
        else {
            uint32_t swap = outAsset->indices.as_uint32[i + 1];
            outAsset->indices.as_uint32[i + 1] = outAsset->indices.as_uint32[i];
            outAsset->indices.as_uint32[i] = swap;
        }
    }

    return true;
}

