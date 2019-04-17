#include <bvh.hpp>

std::vector<BVHTriangleRef> buildTriangleRefList(std::vector<TriangleRef> const& refs,
						std::vector<Vertex> const& vertex_data) {
	std::vector<BVHTriangleRef> bvh_refs;
	bvh_refs.reserve(refs.size());

	for (unsigned i = 0; i < refs.size(); ++i) 
		bvh_refs.emplace_back(refs[i], vertex_data, i);

	return bvh_refs;
}

void sortBVHRefList(std::vector<BVHTriangleRef>& refList, BVHAxis axis)
{
  std::sort(refList.begin(), refList.end(), 
    [&](BVHTriangleRef ref0, BVHTriangleRef ref1) -> bool {
      glm::vec3 c0 = (ref0.bounds.max + ref0.bounds.min) / 2.0f;
      glm::vec3 c1 = (ref1.bounds.max + ref1.bounds.min) / 2.0f;
      return (c0[axis] < c1[axis]);
    });
}

AABB refListBounds(std::vector<BVHTriangleRef> const& refList)
{
  AABB bounds;
  bounds.max = glm::vec3(-100000000000000);
  bounds.min = glm::vec3(+100000000000000);
  for (auto& ref : refList) {
    bounds.max = glm::max(ref.bounds.max, bounds.max);
    bounds.min = glm::min(ref.bounds.min, bounds.min);
  }

  return bounds;
}

BVHBuildNode* buildBVHNode(std::vector<BVHTriangleRef>& refList)
{
  //auto node = std::make_unique<BVHBuildNode>();
  auto node = new BVHBuildNode;
  AABB bounds = refListBounds(refList);

	if (refList.size() <= 10) {
    node->isLeaf = true;
    node->refList = refList;
    node->leftBounds = bounds;

    return node;
	}

  BVHAxis sortAxis;
  if (bounds.max.x - bounds.min.x < bounds.max.y - bounds.min.y) {
    if (bounds.max.y - bounds.min.y < bounds.max.z - bounds.min.z) {
      sortAxis = Z_AXIS;
    } else {
      sortAxis = Y_AXIS;
    }
  } else if (bounds.max.x - bounds.min.x < bounds.max.z - bounds.min.z) {
    sortAxis = Z_AXIS;
  } else {
    sortAxis = X_AXIS;
  }
  
	sortBVHRefList(refList, sortAxis);

  auto mid = refList.begin() + (refList.size() / 2);
  std::vector<BVHTriangleRef> leftRefs(refList.begin(), mid+1);
  std::vector<BVHTriangleRef> rightRefs(mid, refList.end());

  node->leftBounds = refListBounds(leftRefs);
  node->rightBounds = refListBounds(rightRefs);

  node->isLeaf = false;

  //node->left = std::unique_ptr<BVHBuildNode>(buildBVHNode(leftRefs));
  //node->right = std::unique_ptr<BVHBuildNode>(buildBVHNode(rightRefs));
  node->left = buildBVHNode(leftRefs);
  node->right = buildBVHNode(rightRefs);

  return node;
}

uint32_t buildBVH(BVHBuildNode* buildNode, BVH& bvh)
{
  BVHNode node;
  node.isLeafBegin = buildNode->isLeaf ? 1 : -1;

  if (node.isLeafBegin > 0) {
    node.isLeafBegin = bvh.refList.size();
    node.rightOffsetEnd = bvh.refList.size() + buildNode->refList.size();
    bvh.nodeList.push_back(node);
    bvh.refList.insert(bvh.refList.end(), buildNode->refList.begin(), buildNode->refList.end());

    return bvh.nodeList.size() - 1;
  }

  node.leftBounds = buildNode->leftBounds;
  node.rightBounds = buildNode->rightBounds;

  bvh.nodeList.push_back(node);
  uint32_t nodeIndex = bvh.nodeList.size() - 1;

  buildBVH(buildNode->left, bvh);
  bvh.nodeList[nodeIndex].rightOffsetEnd = buildBVH(buildNode->right, bvh);

  return nodeIndex;
}

std::optional<Mesh> loadMesh(std::string const& path) {
  std::vector<Vertex> vertex_data;
  std::vector<TriangleRef> triangles;

  Assimp::Importer importer;
  const aiScene *scene = importer.ReadFile(path, aiProcess_Triangulate |
                                           aiProcess_FlipUVs |
                                           aiProcess_GenNormals);

  if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
    std::cerr << "Failed to open mesh: " + path + "; " + importer.GetErrorString() + "\n";
    return {};
  }

  aiNode *node = scene->mRootNode->mChildren[0];

  if (node->mNumMeshes != 1) {
    std::cerr << "Only a single mesh is suported\n";
    return {};
  }

  aiMesh *mesh = scene->mMeshes[node->mMeshes[0]];

  for (unsigned int i = 0; i < mesh->mNumVertices; i++) {
    glm::vec3 p, n;
    glm::vec2 t = {};

    p.x = mesh->mVertices[i].x;
    p.y = mesh->mVertices[i].y;
    p.z = mesh->mVertices[i].z;

    n.x = mesh->mNormals[i].x;
    n.y = mesh->mNormals[i].y;
    n.z = mesh->mNormals[i].z;

    if (mesh->mTextureCoords[0]) {
        t.x = mesh->mTextureCoords[0][i].x;
        t.y = mesh->mTextureCoords[0][i].z;
    }

    vertex_data.emplace_back(p, n, t);
  }

  for (unsigned int i = 0; i < mesh->mNumFaces; i++) {
    aiFace face = mesh->mFaces[i];

    for (unsigned int j = 0; j < 3; j++) 
      triangles.emplace_back(face.mIndices[0],
                          face.mIndices[1],
                          face.mIndices[2]);
  }

	return {{vertex_data, triangles}};
}
