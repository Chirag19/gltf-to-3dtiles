#include "MergeMesh.h"
#include "MyMesh.h"
#include <algorithm>
using namespace tinygltf;
using namespace std;

MergeMesh::MergeMesh(tinygltf::Model* model, tinygltf::Model* newModel, std::vector<MyMesh*> myMeshes, std::vector<int> nodesToMerge, std::string bufferName)
{
    m_bufferName = bufferName;
	m_pModel = model;
    m_pNewModel = newModel;
	m_myMeshes = myMeshes;
    m_nodesToMerge = nodesToMerge;
    m_currentMeshIdx = 0;
}

MergeMesh::~MergeMesh()
{

}

bool MergeMesh::meshComparenFunction(int meshIdx1, int meshIdx2)
{
    MyMesh* mesh1 = m_myMeshes[meshIdx1];
    MyMesh* mesh2 = m_myMeshes[meshIdx2];
    return mesh1->vn < mesh2->vn;
}


struct mesh_compare_fn
{
    inline bool operator() (const MyMesh* myMesh1, const MyMesh* myMesh2)
    {
        return myMesh1->vn < myMesh2->vn;
    }
};

void MergeMesh::DoMerge()
{
	for (int i = 0; i < m_nodesToMerge.size(); ++i)
	{
		Node* node = &(m_pModel->nodes[m_nodesToMerge[i]]);
		std::vector<MeshInfo> meshInfos;
        GetNodeMeshInfos(m_pModel, node, meshInfos);
        
        for (int j = 0; j < meshInfos.size(); ++j)
        {
            Mesh* mesh = &(m_pModel->meshes[meshInfos[j].meshIdx]);

            if (m_meshMatrixMap.count(m_myMeshes[meshInfos[j].meshIdx]) > 0)
            {
                printf("error detected\n");
            }
            else if (meshInfos[j].matrix != NULL)
            {
                m_meshMatrixMap.insert(make_pair(m_myMeshes[meshInfos[j].meshIdx], *meshInfos[j].matrix));
            }
            int materialIdx = mesh->primitives[0].material;
            Material material = m_pModel->materials[materialIdx];
            if (m_materialMeshMap.count(material) > 0)
            {
                m_materialMeshMap.at(material).push_back(m_myMeshes[meshInfos[j].meshIdx]);
            }
            else
            {
                std::vector<MyMesh*> myMeshesToMerge;
                myMeshesToMerge.push_back(m_myMeshes[meshInfos[j].meshIdx]);
                m_materialMeshMap.insert(make_pair(material, myMeshesToMerge));
            }
        }
	}

    // Add nodes according to material.

    {
        // FIXME: Support more than 3 bufferviews.
        BufferView arraybufferView;
        arraybufferView.target = TINYGLTF_TARGET_ARRAY_BUFFER;
        arraybufferView.byteLength = 0;
        arraybufferView.buffer = 0;
        arraybufferView.byteStride = 12;
        arraybufferView.byteOffset = 0;

        BufferView batchIdArrayBufferView;
        batchIdArrayBufferView.target = TINYGLTF_TARGET_ARRAY_BUFFER;
        batchIdArrayBufferView.byteLength = 0;
        batchIdArrayBufferView.buffer = 0;
        batchIdArrayBufferView.byteStride = 4;
        batchIdArrayBufferView.byteOffset = 0;

        BufferView elementArraybufferView;
        elementArraybufferView.target = TINYGLTF_TARGET_ELEMENT_ARRAY_BUFFER;
        elementArraybufferView.byteLength = 0;
        elementArraybufferView.buffer = 0;

        m_pNewModel->bufferViews.push_back(arraybufferView);
        m_pNewModel->bufferViews.push_back(batchIdArrayBufferView);
        m_pNewModel->bufferViews.push_back(elementArraybufferView);
    }

    std::unordered_map<tinygltf::Material, std::vector<MyMesh*>, material_hash_fn, material_equal_fn>::iterator it;

    Node root;
    root.name = "scene_root";
    m_pNewModel->nodes.push_back(root);
    Scene scene;
    scene.name = "scene";
    scene.nodes.push_back(0);
    m_pNewModel->scenes.push_back(scene);
    m_pNewModel->asset.version = "2.0";
    for (it = m_materialMeshMap.begin(); it != m_materialMeshMap.end(); ++it)
    {
        m_pNewModel->materials.push_back(it->first);

        std::vector<MyMesh*> myMeshes = it->second;

        std::sort(myMeshes.begin(), myMeshes.end(), mesh_compare_fn());

        addMergedMeshesToNewModel(m_pNewModel->materials.size() - 1, myMeshes);
    }

    {
        // FIXME: Support more than 3 bufferviews.
        m_pNewModel->bufferViews[1].byteOffset = m_currentAttributeBuffer.size();
        m_pNewModel->bufferViews[2].byteOffset = m_currentAttributeBuffer.size() + m_currentBatchIdBuffer.size();
    }

    Buffer buffer;
    buffer.uri = m_bufferName;
    buffer.data.resize(m_currentAttributeBuffer.size() + m_currentIndexBuffer.size() + m_currentBatchIdBuffer.size());
    memcpy(buffer.data.data(), m_currentAttributeBuffer.data(), m_currentAttributeBuffer.size());
    memcpy(buffer.data.data() + m_currentAttributeBuffer.size(), m_currentBatchIdBuffer.data(), m_currentBatchIdBuffer.size());
    memcpy(buffer.data.data() + m_currentAttributeBuffer.size() + m_currentBatchIdBuffer.size(),
        m_currentIndexBuffer.data(), m_currentIndexBuffer.size());
    m_pNewModel->buffers.push_back(buffer);
}

void MergeMesh::addMergedMeshesToNewModel(int materialIdx, std::vector<MyMesh*> myMeshes)
{
    m_totalVertex = 0;
    m_totalFace = 0;
    std::vector<MyMesh*> meshesToMerge;
    for (int i = 0; i < myMeshes.size(); ++i)
    {
        MyMesh* myMesh = myMeshes[i];
        if (m_totalVertex + myMesh->vn > 65536 || (m_totalVertex + myMesh->vn < 65536 && i == myMeshes.size() - 1))
        {
            if (m_totalVertex + myMesh->vn < 65536 && i == myMeshes.size() - 1)
            {
                meshesToMerge.push_back(myMesh);
                m_totalVertex += myMesh->vn;
                m_totalFace += myMesh->fn;
            }
            // add mesh node
            Node node;
            Mesh newMesh;
            char meshName[1024];
            sprintf(meshName, "%d", m_currentMeshIdx);
            node.name = string(meshName);
            newMesh.name = string(meshName);
            
            Primitive newPrimitive;
            newPrimitive.mode = 4; // currently only support triangle mesh.
            newPrimitive.material = materialIdx;
            m_currentMeshes = meshesToMerge;
            addPrimitive(&newPrimitive);
            newMesh.primitives.push_back(newPrimitive);

            m_pNewModel->meshes.push_back(newMesh);
            node.mesh = m_pNewModel->meshes.size() - 1;
            m_pNewModel->nodes.push_back(node);
            m_pNewModel->nodes[0].children.push_back(m_currentMeshIdx + 1);

            m_currentMeshIdx++;
            meshesToMerge.clear();
            m_totalVertex = 0;
            m_totalFace = 0;
        }

        meshesToMerge.push_back(myMesh);
        m_totalVertex += myMesh->vn;
        m_totalFace += myMesh->fn;
    }
}

void MergeMesh::addPrimitive(Primitive* primitive)
{
    int positionAccessorIdx = addAccessor(POSITION);
    int normalAccessorIdx = addAccessor(NORMAL);
    //int uvAccessorIdx = addAccessor(UV);
    int batchIdAccessorIdx = addAccessor(BATCH_ID);
    int indicesAccessorIdx = addAccessor(INDEX);

    primitive->attributes.insert(make_pair("POSITION", positionAccessorIdx));
    primitive->attributes.insert(make_pair("NORMAL", normalAccessorIdx));
    primitive->attributes.insert(make_pair("_BATCHID", batchIdAccessorIdx));

    primitive->indices = indicesAccessorIdx;
}

int MergeMesh::addAccessor(AccessorType type)
{
    Accessor newAccessor;
    switch (type)
    {
    case BATCH_ID:
        newAccessor.type = TINYGLTF_TYPE_SCALAR;
        newAccessor.componentType = TINYGLTF_COMPONENT_TYPE_FLOAT;
        newAccessor.count = m_totalVertex;
        break;
    case POSITION:
        newAccessor.type = TINYGLTF_TYPE_VEC3;
        newAccessor.componentType = TINYGLTF_COMPONENT_TYPE_FLOAT;
        newAccessor.count = m_totalVertex;
        break;
    case NORMAL:
        newAccessor.type = TINYGLTF_TYPE_VEC3;
        newAccessor.componentType = TINYGLTF_COMPONENT_TYPE_FLOAT;
        newAccessor.count = m_totalVertex;
        break;
    case UV:
        newAccessor.type = TINYGLTF_TYPE_VEC2;
        newAccessor.componentType = TINYGLTF_COMPONENT_TYPE_FLOAT;
        newAccessor.count = m_totalVertex;
        break;
    case INDEX:
        newAccessor.type = TINYGLTF_TYPE_SCALAR;
        if (m_totalVertex > 65536)
        {
            newAccessor.componentType = TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT;
        }
        else
        {
            newAccessor.componentType = TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT;
        }
        newAccessor.count = m_totalFace * 3;
        break;
    default:
        assert(-1);
        break;
    }

    newAccessor.bufferView = addBufferView(type, newAccessor.byteOffset);
    if (type == POSITION)
    {
        newAccessor.minValues.push_back(m_positionMin[0]);
        newAccessor.minValues.push_back(m_positionMin[1]);
        newAccessor.minValues.push_back(m_positionMin[2]);
        newAccessor.maxValues.push_back(m_positionMax[0]);
        newAccessor.maxValues.push_back(m_positionMax[1]);
        newAccessor.maxValues.push_back(m_positionMax[2]);
    }
    m_pNewModel->accessors.push_back(newAccessor);
    return m_pNewModel->accessors.size() - 1;
}

int MergeMesh::addBufferView(AccessorType type, size_t& byteOffset)
{
    // FIXME: We have not consider the uv coordinates yet.
    // And we only have one .bin file currently.
    int byteLength = addBuffer(type);
    switch (type)
    {
    case POSITION:
    case NORMAL:
        byteOffset = m_pNewModel->bufferViews[0].byteLength;
        m_pNewModel->bufferViews[0].byteLength += byteLength;
        return 0;
    case UV:
        // TODO: implement UV
        break;
    case INDEX:
        byteOffset = m_pNewModel->bufferViews[2].byteLength;
        m_pNewModel->bufferViews[2].byteLength += byteLength;
        return 2;
        break;
    case BATCH_ID:
        byteOffset = m_pNewModel->bufferViews[1].byteLength;
        m_pNewModel->bufferViews[1].byteLength += byteLength;
        return 1;
        break;
    default:
        break;
    }
}

int MergeMesh::addBuffer(AccessorType type)
{
    int byteLength = 0;
    int index = 0;
    unsigned char* temp = NULL;
    switch (type)
    {
    case BATCH_ID:
        for (int i = 0; i < m_currentMeshes.size(); ++i)
        {
            MyMesh* myMesh = m_currentMeshes[i];
            for (vector<MyVertex>::iterator it = myMesh->vert.begin(); it != myMesh->vert.end(); ++it)
            {
                if (it->IsD())
                {
                    continue;
                }
                m_currentBatchIdBuffer.push_back(it->C().X());
                m_currentBatchIdBuffer.push_back(it->C().Y());
                m_currentBatchIdBuffer.push_back(it->C().Z());
                m_currentBatchIdBuffer.push_back(it->C().W());
            }
        }
        byteLength = m_totalVertex * sizeof(unsigned int);
        break;
    case POSITION:
        m_positionMin[0] = m_positionMin[1] = m_positionMin[2] = INFINITY;
        m_positionMax[0] = m_positionMax[1] = m_positionMax[2] = -INFINITY;
        for (int i = 0; i < m_currentMeshes.size(); ++i)
        {
            MyMesh* myMesh = m_currentMeshes[i];
            Matrix44f matrix;
            bool hasMatrix = false;
            if (m_meshMatrixMap.count(myMesh) > 0)
            {
                hasMatrix = true;
                matrix = m_meshMatrixMap.at(myMesh);
            }
            for (vector<MyVertex>::iterator it = myMesh->vert.begin(); it != myMesh->vert.end(); ++it)
            {
                if (it->IsD())
                {
                    continue;
                }

                Point4f point;
                point.X() = it->P()[0];
                point.Y() = it->P()[1];
                point.Z() = it->P()[2];
                point.W() = 1.0;

                if (hasMatrix)
                {
                    point = matrix * point;
                }

                for (int i = 0; i < 3; ++i)
                {
                    if (m_positionMin[i] > point[i])
                    {
                        m_positionMin[i] = point[i];
                    }
                    if (m_positionMax[i] < point[i])
                    {
                        m_positionMax[i] = point[i];
                    }

                    temp = (unsigned char*)&(point[i]);
                    m_currentAttributeBuffer.push_back(temp[0]);
                    m_currentAttributeBuffer.push_back(temp[1]);
                    m_currentAttributeBuffer.push_back(temp[2]);
                    m_currentAttributeBuffer.push_back(temp[3]);
                }

                m_vertexUshortMap.insert(make_pair(&(*it), index));
                index++;
            }
        }
        byteLength = m_totalVertex * 3 * sizeof(float);
        break;
    case NORMAL:    
        for (int i = 0; i < m_currentMeshes.size(); ++i)
        {
            MyMesh* myMesh = m_currentMeshes[i];
            bool hasMatrix = false;
            Matrix44f matrix;
            if (m_meshMatrixMap.count(myMesh) > 0)
            {
                hasMatrix = true;
                matrix = m_meshMatrixMap.at(myMesh);
            }
            for (vector<MyVertex>::iterator it = myMesh->vert.begin(); it != myMesh->vert.end(); ++it)
            {
                if (it->IsD())
                {
                    continue;
                }

                Point3f point;
                point.X() = it->N()[0];
                point.Y() = it->N()[1];
                point.Z() = it->N()[2];
                if (hasMatrix)
                {
                    Matrix33f normalMatrix = Matrix33f(matrix, 3);
                    normalMatrix = Inverse(normalMatrix);
                    point = normalMatrix * point;
                }
                for (int i = 0; i < 3; ++i)
                {
                    temp = (unsigned char*)&(point[i]);
                    m_currentAttributeBuffer.push_back(temp[0]);
                    m_currentAttributeBuffer.push_back(temp[1]);
                    m_currentAttributeBuffer.push_back(temp[2]);
                    m_currentAttributeBuffer.push_back(temp[3]);
                }
            }
        }
        byteLength = m_totalVertex * 3 * sizeof(float);
        break;
    case UV:
        // FIXME: Implement UV
        break;
    case INDEX:
        for (int i = 0; i < m_currentMeshes.size(); ++i)
        {
            MyMesh* myMesh = m_currentMeshes[i];
            for (vector<MyFace>::iterator it = myMesh->face.begin(); it != myMesh->face.end(); ++it)
            {
                if (it->IsD())
                {
                    continue;
                }
                for (int i = 0; i < 3; ++i)
                {
                    if (m_totalVertex > 65536)
                    {
                        temp = (unsigned char*)&(m_vertexUshortMap.at(it->V(i)));
                        m_currentIndexBuffer.push_back(temp[0]);
                        m_currentIndexBuffer.push_back(temp[1]);
                        m_currentIndexBuffer.push_back(temp[2]);
                        m_currentIndexBuffer.push_back(temp[3]);
                    }
                    else
                    {
                        temp = (unsigned char*)&(m_vertexUshortMap.at(it->V(i)));
                        m_currentIndexBuffer.push_back(temp[0]);
                        m_currentIndexBuffer.push_back(temp[1]);
                    }
                }
            }
        }
        byteLength = m_totalFace * 3 * (m_totalVertex > 65536? sizeof(uint32_t) : sizeof(uint16_t));
        break;
    default:
        break;
    }
    return byteLength;
}