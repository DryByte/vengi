/**
 * @file
 */

#include "Mesh.h"
#include "CubicSurfaceExtractor.h"
#include "core/Common.h"
#include "core/Trace.h"
#include "core/Assert.h"
#include "core/collection/DynamicArray.h"
#include "util/BufferUtil.h"
#include <glm/vector_relational.hpp>
#include <glm/common.hpp>

namespace voxel {

Mesh::Mesh(int vertices, int indices, bool mayGetResized) : _mayGetResized(mayGetResized) {
	if (vertices > 0) {
		_vecVertices.reserve(vertices);
	}
	if (indices > 0) {
		_vecIndices.reserve(indices);
	}
}

Mesh::Mesh(Mesh&& other) noexcept {
	_vecIndices = core::move(other._vecIndices);
	_vecVertices = core::move(other._vecVertices);
	_compressedIndices = other._compressedIndices;
	other._compressedIndices = nullptr;
	_compressedIndexSize = other._compressedIndexSize;
	other._compressedIndexSize = 0u;
	_offset = other._offset;
	_mayGetResized = other._mayGetResized;
}

Mesh::Mesh(const Mesh& other) {
	_vecIndices = other._vecIndices;
	_vecVertices = other._vecVertices;
	_compressedIndexSize = other._compressedIndexSize;
	if (other._compressedIndices != nullptr) {
		_compressedIndices = (uint8_t*)core_malloc(_vecIndices.size() * _compressedIndexSize);
		core_memcpy(_compressedIndices, other._compressedIndices, _vecIndices.size() * _compressedIndexSize);
	} else {
		_compressedIndices = nullptr;
	}
	_offset = other._offset;
	_mayGetResized = other._mayGetResized;
}

Mesh& Mesh::operator=(const Mesh& other) {
	if (&other == this) {
		return *this;
	}
	_vecIndices = other._vecIndices;
	_vecVertices = other._vecVertices;
	_compressedIndexSize = other._compressedIndexSize;
	core_free(_compressedIndices);
	if (other._compressedIndices != nullptr) {
		_compressedIndices = (uint8_t*)core_malloc(_vecIndices.size() * _compressedIndexSize);
		core_memcpy(_compressedIndices, other._compressedIndices, _vecIndices.size() * _compressedIndexSize);
	} else {
		_compressedIndices = nullptr;
	}
	_offset = other._offset;
	_mayGetResized = other._mayGetResized;
	return *this;
}

Mesh& Mesh::operator=(Mesh&& other) noexcept {
	_vecIndices = core::move(other._vecIndices);
	_vecVertices = core::move(other._vecVertices);
	core_free(_compressedIndices);
	_compressedIndices = other._compressedIndices;
	other._compressedIndices = nullptr;
	_compressedIndexSize = other._compressedIndexSize;
	other._compressedIndexSize = 4u;
	_offset = other._offset;
	_mayGetResized = other._mayGetResized;
	return *this;
}

Mesh::~Mesh() {
	core_free(_compressedIndices);
}

const NormalArray& Mesh::getNormalVector() const {
	return _normals;
}

const IndexArray& Mesh::getIndexVector() const {
	return _vecIndices;
}

const VertexArray& Mesh::getVertexVector() const {
	return _vecVertices;
}

IndexArray& Mesh::getIndexVector() {
	return _vecIndices;
}

VertexArray& Mesh::getVertexVector() {
	return _vecVertices;
}

NormalArray& Mesh::getNormalVector() {
	return _normals;
}

size_t Mesh::getNoOfVertices() const {
	return _vecVertices.size();
}

const VoxelVertex& Mesh::getVertex(IndexType index) const {
	return _vecVertices[index];
}

const VoxelVertex* Mesh::getRawVertexData() const {
	return _vecVertices.data();
}

size_t Mesh::getNoOfIndices() const {
	return _vecIndices.size();
}

IndexType Mesh::getIndex(IndexType index) const {
	return _vecIndices[index];
}

const IndexType* Mesh::getRawIndexData() const {
	return _vecIndices.data();
}

const glm::ivec3& Mesh::getOffset() const {
	return _offset;
}

void Mesh::setOffset(const glm::ivec3& offset) {
	_offset = offset;
}

void Mesh::clear() {
	_vecVertices.clear();
	_vecIndices.clear();
	_offset = glm::ivec3(0);
}

bool Mesh::isEmpty() const {
	return getNoOfVertices() == 0 || getNoOfIndices() == 0;
}

void Mesh::addTriangle(IndexType index0, IndexType index1, IndexType index2) {
	//Make sure the specified indices correspond to valid vertices.
	core_assert_msg(index0 < _vecVertices.size(), "Index points at an invalid vertex (%i/%i).", (int)index0, (int)_vecVertices.size());
	core_assert_msg(index1 < _vecVertices.size(), "Index points at an invalid vertex (%i/%i).", (int)index1, (int)_vecVertices.size());
	core_assert_msg(index2 < _vecVertices.size(), "Index points at an invalid vertex (%i/%i).", (int)index2, (int)_vecVertices.size());
	if (!_mayGetResized) {
		core_assert_msg(_vecIndices.size() + 3 < _vecIndices.capacity(), "addTriangle() call exceeds the capacity of the indices vector and will trigger a realloc (%i vs %i)", (int)_vecIndices.size(), (int)_vecIndices.capacity());
	}

	_vecIndices.push_back(index0);
	_vecIndices.push_back(index1);
	_vecIndices.push_back(index2);
}

IndexType Mesh::addVertex(const VoxelVertex& vertex) {
	// We should not add more vertices than our chosen index type will let us index.
	core_assert_msg(_vecVertices.size() < (std::numeric_limits<IndexType>::max)(), "Mesh has more vertices that the chosen index type allows.");
	if (!_mayGetResized) {
		core_assert_msg(_vecVertices.size() + 1 < _vecVertices.capacity(), "addVertex() call exceeds the capacity of the vertices vector and will trigger a realloc (%i vs %i)", (int)_vecVertices.size(), (int)_vecVertices.capacity());
	}

	_vecVertices.push_back(vertex);
	return (IndexType)_vecVertices.size() - 1;
}

void Mesh::setNormal(IndexType index, const glm::vec3 &normal) {
	// We should not add more vertices than our chosen index type will let us index.
	core_assert_msg(_normals.size() < (std::numeric_limits<IndexType>::max)(), "Mesh has more normals that the chosen index type allows.");
	_normals.resize(_vecVertices.capacity());
	_normals[index] = normal;
}

void Mesh::removeUnusedVertices() {
	const size_t vertices = _vecVertices.size();
	const size_t indices = _vecIndices.size();
	core::DynamicArray<bool> isVertexUsed(vertices);
	isVertexUsed.fill(false);

	for (size_t triCt = 0u; triCt < indices; ++triCt) {
		IndexType v = _vecIndices[triCt];
		isVertexUsed[v] = true;
	}

	size_t noOfUsedVertices = 0;
	IndexArray newPos(vertices);
	for (size_t vertCt = 0u; vertCt < vertices; ++vertCt) {
		if (!isVertexUsed[vertCt]) {
			continue;
		}
		_vecVertices[noOfUsedVertices] = _vecVertices[vertCt];
		// here the assumption that a normal is added for each vertex if
		// at least one normal was added holds
		if (!_normals.empty()) {
			_normals[noOfUsedVertices] = _normals[vertCt];
		}
		newPos[vertCt] = noOfUsedVertices;
		++noOfUsedVertices;
	}

	_vecVertices.resize(noOfUsedVertices);

	for (size_t triCt = 0u; triCt < indices; ++triCt) {
		_vecIndices[triCt] = newPos[_vecIndices[triCt]];
	}
	_vecIndices.resize(indices);
}

void Mesh::compressIndices() {
	if (_vecIndices.empty()) {
		core_free(_compressedIndices);
		_compressedIndices = nullptr;
		_compressedIndexSize = 0;
		return;
	}
	const size_t maxSize = _vecIndices.size() * sizeof(voxel::IndexType);
	core_free(_compressedIndices);
	_compressedIndices = (uint8_t*)core_malloc(maxSize);
	util::indexCompress(&_vecIndices.front(), maxSize, _compressedIndexSize, _compressedIndices, maxSize);
}

bool Mesh::operator<(const Mesh& rhs) const {
	return glm::all(glm::lessThan(getOffset(), rhs.getOffset()));
}

bool Mesh::sort(const glm::vec3 &objectSpaceEye) {
#if 0
	for (int idx = 0; idx < (int)_vecIndices.size(); idx += 3) {
		const voxel::IndexType i0 = _vecIndices[idx + 0];
		const voxel::IndexType i1 = _vecIndices[idx + 1];
		const voxel::IndexType i2 = _vecIndices[idx + 2];
		const voxel::VoxelVertex &v0 = _vecVertices[i0];
		const voxel::VoxelVertex &v1 = _vecVertices[i1];
		const voxel::VoxelVertex &v2 = _vecVertices[i2];
		const glm::vec3 center = (v0.position + v1.position + v2.position) / 3.0f;
	}
#endif
	return false;
}

}
