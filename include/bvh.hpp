#include <iostream>
#include <fstream>
#include <vector>
#include <memory>
#include <tuple>
#include <optional>
#include <sstream>

#include <glm/glm.hpp>

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

constexpr float EPSILON = 0.00001f;

struct Vertex {
	glm::vec3 pos, normal;
	glm::vec2 texcoord;

	Vertex() = default;
	Vertex(glm::vec3 pos,
		   glm::vec3 normal,
		   glm::vec2 texcoord) :
		pos(pos),
		normal(normal),
		texcoord(texcoord) {}
};

struct AABB {
	alignas(16) glm::vec3 min;
	alignas(16) glm::vec3 max;
	
	AABB() = default;
	AABB(glm::vec3 max, glm::vec3 min) :
		max(max), min(min) {}
};

struct TriangleRef {
	unsigned v0, v1, v2;

	TriangleRef() = default;
	TriangleRef(unsigned v0,
		    unsigned v1,
		    unsigned v2) :
		v0(v0),
		v1(v1),
		v2(v2) {}
};

struct Mesh {
	std::vector<Vertex> vertex_data;
	std::vector<TriangleRef> triangles;

	Mesh(std::vector<Vertex> vertex_data,
		 std::vector<TriangleRef> triangles) :
		vertex_data(vertex_data),
		triangles(triangles) {}
	Mesh() {}
};

struct BVHTriangleRef {
    alignas(16) glm::vec3 v0, e1, e2;
	AABB bounds;
	alignas(16) unsigned index;

	BVHTriangleRef(TriangleRef const& tri,
				   std::vector<Vertex> const& vertex_data,
				   unsigned index) :
		index(index) {
		auto& v0 = vertex_data[tri.v0];
		auto& v1 = vertex_data[tri.v1];
		auto& v2 = vertex_data[tri.v2];

		this->v0 = v0.pos;
		e1 = v1.pos - v0.pos;
		e2 = v2.pos - v0.pos;

		bounds = {glm::max(v0.pos, glm::max(v1.pos, v2.pos)),
				  glm::min(v0.pos, glm::min(v1.pos, v2.pos))};

		bounds.max += glm::vec3(EPSILON);
		bounds.min -= glm::vec3(EPSILON);
	}
};

struct BVHBuildNode {
    AABB leftBounds;
    AABB rightBounds;
    bool isLeaf;

    BVHBuildNode* left;
    BVHBuildNode* right;

    std::vector<BVHTriangleRef> refList;

	BVHBuildNode() = default;
};

struct BVHNode {
    AABB leftBounds;
    AABB rightBounds;
    int32_t isLeafBegin;
    int32_t rightOffsetEnd;
	char pad[8];
};

struct BVH {
    std::vector<BVHNode> nodeList;
    std::vector<BVHTriangleRef> refList;
};

std::optional<Mesh> loadMesh(std::string const& path);
std::vector<BVHTriangleRef> buildTriangleRefList(std::vector<TriangleRef> const& refs,
    std::vector<Vertex> const& vertex_data);
BVHBuildNode* buildBVHNode(std::vector<BVHTriangleRef>& refList);
uint32_t buildBVH(BVHBuildNode* buildNode, BVH& bvh);
AABB refListBounds(std::vector<BVHTriangleRef> const& refList);

enum BVHAxis {
  X_AXIS = 0,
  Y_AXIS = 1,
  Z_AXIS = 2
};

void sortBVHRefList(std::vector<BVHTriangleRef>& refList, BVHAxis axis);
