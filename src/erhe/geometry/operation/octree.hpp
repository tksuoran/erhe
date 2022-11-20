#ifndef UNIBN_OCTREE_H_
#define UNIBN_OCTREE_H_

// Copyright (c) 2015 Jens Behley, University of Bonn
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to
// deal in the Software without restriction, including without limitation the
// rights  to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
// IN THE SOFTWARE.

#include <stdint.h>
#include <cassert>
#include <cmath>
#include <cstring>  // memset.
#include <limits>
#include <vector>

// needed for gtest access to protected/private members ...
namespace
{
class OctreeTest;
}

namespace unibn
{

/**
 * Some traits to access coordinates regardless of the specific implementation of point
 * inspired by boost.geometry, which needs to be implemented by new points.
 *
 */
namespace traits
{

template <typename PointT, int D>
struct access
{
};

template <class PointT>
struct access<PointT, 0>
{
  static float get(const PointT& p)
  {
    return p.x;
  }
};

template <class PointT>
struct access<PointT, 1>
{
  static float get(const PointT& p)
  {
    return p.y;
  }
};

template <class PointT>
struct access<PointT, 2>
{
  static float get(const PointT& p)
  {
    return p.z;
  }
};
}

/** convenience function for access of point coordinates **/
template <int D, typename PointT>
inline float get(const PointT& p)
{
  return traits::access<PointT, D>::get(p);
}

/**
 * Some generic distances: Manhattan, (squared) Euclidean, and Maximum distance.
 *
 * A Distance has to implement the methods
 * 1. compute of two points p and q to compute and return the distance between two points, and
 * 2. norm of x,y,z coordinates to compute and return the norm of a point p = (x,y,z)
 * 3. sqr and sqrt of value to compute the correct radius if a comparison is performed using squared norms (see
 *L2Distance)...
 */
template <typename PointT>
struct L1Distance
{
  static inline float compute(const PointT& p, const PointT& q)
  {
    float diff1 = get<0>(p) - get<0>(q);
    float diff2 = get<1>(p) - get<1>(q);
    float diff3 = get<2>(p) - get<2>(q);

    return std::abs(diff1) + std::abs(diff2) + std::abs(diff3);
  }

  static inline float norm(float x, float y, float z)
  {
    return std::abs(x) + std::abs(y) + std::abs(z);
  }

  static inline float sqr(float r)
  {
    return r;
  }

  static inline float sqrt(float r)
  {
    return r;
  }
};

template <typename PointT>
struct L2Distance
{
  static inline float compute(const PointT& p, const PointT& q)
  {
    float diff1 = get<0>(p) - get<0>(q);
    float diff2 = get<1>(p) - get<1>(q);
    float diff3 = get<2>(p) - get<2>(q);

    return std::pow(diff1, 2) + std::pow(diff2, 2) + std::pow(diff3, 2);
  }

  static inline float norm(float x, float y, float z)
  {
    return std::pow(x, 2) + std::pow(y, 2) + std::pow(z, 2);
  }

  static inline float sqr(float r)
  {
    return r * r;
  }

  static inline float sqrt(float r)
  {
    return std::sqrt(r);
  }
};

template <typename PointT>
struct MaxDistance
{
  static inline float compute(const PointT& p, const PointT& q)
  {
    float diff1 = std::abs(get<0>(p) - get<0>(q));
    float diff2 = std::abs(get<1>(p) - get<1>(q));
    float diff3 = std::abs(get<2>(p) - get<2>(q));

    float maximum = diff1;
    if (diff2 > maximum) maximum = diff2;
    if (diff3 > maximum) maximum = diff3;

    return maximum;
  }

  static inline float norm(float x, float y, float z)
  {
    float maximum = x;
    if (y > maximum) maximum = y;
    if (z > maximum) maximum = z;
    return maximum;
  }

  static inline float sqr(float r)
  {
    return r;
  }

  static inline float sqrt(float r)
  {
    return r;
  }
};

struct OctreeParams
{
 public:
  OctreeParams(uint32_t bucketSize = 32, bool copyPoints = false, float minExtent = 0.0f)
      : bucketSize(bucketSize), copyPoints(copyPoints), minExtent(minExtent)
  {
  }
  uint32_t bucketSize;
  bool copyPoints;
  float minExtent;
};

/** \brief Index-based Octree implementation offering different queries and insertion/removal of points.
 *
 * The index-based Octree uses a successor relation and a startIndex in each Octant to improve runtime
 * performance for radius queries. The efficient storage of the points by relinking list elements
 * bases on the insight that children of an Octant contain disjoint subsets of points inside the Octant and
 * that we can reorganize the points such that we get an continuous single connect list that we can use to
 * store in each octant the start of this list.
 *
 * Special about the implementation is that it allows to search for neighbors with arbitrary p-norms, which
 * distinguishes it from most other Octree implementations.
 *
 * We decided to implement the Octree using a template for points and containers. The container must have an
 * operator[], which allows to access the points, and a size() member function, which allows to get the size of the
 * container. For the points, we used an access trait to access the coordinates inspired by boost.geometry.
 * The implementation already provides a general access trait, which expects to have public member variables x,y,z.
 *
 * f you use the implementation or ideas from the corresponding paper in your academic work, it would be nice if you
 * cite the corresponding paper:
 *
 *    J. Behley, V. Steinhage, A.B. Cremers. Efficient Radius Neighbor Search in Three-dimensional Point Clouds,
 *    Proc. of the IEEE International Conference on Robotics and Automation (ICRA), 2015.
 *
 * In future, we might add also other neighbor queries and implement the removal and adding of points.
 *
 * \version 0.1-icra
 *
 * \author behley
 */

template <typename PointT, typename ContainerT = std::vector<PointT> >
class Octree
{
 public:
  Octree();
  ~Octree();

  /** \brief initialize octree with all points **/
  void initialize(const ContainerT& pts, const OctreeParams& params = OctreeParams());

  /** \brief initialize octree only from pts that are inside indexes. **/
  void initialize(const ContainerT& pts, const std::vector<uint32_t>& indexes,
                  const OctreeParams& params = OctreeParams());

  /** \brief remove all data inside the octree. **/
  void clear();

  /** \brief radius neighbor queries where radius determines the maximal radius of reported indices of points in
   * resultIndices **/
  template <typename Distance>
  void radiusNeighbors(const PointT& query, float radius, std::vector<uint32_t>& resultIndices) const;

  /** \brief radius neighbor queries with explicit (squared) distance computation. **/
  template <typename Distance>
  void radiusNeighbors(const PointT& query, float radius, std::vector<uint32_t>& resultIndices,
                       std::vector<float>& distances) const;

  /** \brief nearest neighbor queries. Using minDistance >= 0, we explicitly disallow self-matches.
   * @return index of nearest neighbor n with Distance::compute(query, n) > minDistance and otherwise -1.
   **/
  template <typename Distance>
  int32_t findNeighbor(const PointT& query, float minDistance = -1) const;

 protected:
  class Octant
  {
   public:
    Octant();
    ~Octant();

    bool isLeaf;

    // bounding box of the octant needed for overlap and contains tests...
    float x, y, z;  // center
    float extent;   // half of side-length

    uint32_t start, end;  // start and end in succ_
    uint32_t size;        // number of points

    Octant* child[8];
  };

  // not copyable, not assignable ...
  Octree(Octree&);
  Octree& operator=(const Octree& oct);

  /**
   * \brief creation of an octant using the elements starting at startIdx.
   *
   * The method reorders the index such that all points are correctly linked to successors belonging
   * to the same octant.
   *
   * \param x,y,z           center coordinates of octant
   * \param extent          extent of octant
   * \param startIdx        first index of points inside octant
   * \param endIdx          last index of points inside octant
   * \param size            number of points in octant
   *
   * \return  octant with children nodes.
   */
  Octant* createOctant(float x, float y, float z, float extent, uint32_t startIdx, uint32_t endIdx, uint32_t size);

  /** @return true, if search finished, otherwise false. **/
  template <typename Distance>
  bool findNeighbor(const Octant* octant, const PointT& query, float minDistance, float& maxDistance,
                    int32_t& resultIndex) const;

  template <typename Distance>
  void radiusNeighbors(const Octant* octant, const PointT& query, float radius, float sqrRadius,
                       std::vector<uint32_t>& resultIndices) const;

  template <typename Distance>
  void radiusNeighbors(const Octant* octant, const PointT& query, float radius, float sqrRadius,
                       std::vector<uint32_t>& resultIndices, std::vector<float>& distances) const;

  /** \brief test if search ball S(q,r) overlaps with octant
   *
   * @param query   query point
   * @param radius  "squared" radius
   * @param o       pointer to octant
   *
   * @return true, if search ball overlaps with octant, false otherwise.
   */
  template <typename Distance>
  static bool overlaps(const PointT& query, float radius, float sqRadius, const Octant* o);

  /** \brief test if search ball S(q,r) contains octant
   *
   * @param query    query point
   * @param sqRadius "squared" radius
   * @param octant   pointer to octant
   *
   * @return true, if search ball overlaps with octant, false otherwise.
   */
  template <typename Distance>
  static bool contains(const PointT& query, float sqRadius, const Octant* octant);

  /** \brief test if search ball S(q,r) is completely inside octant.
   *
   * @param query   query point
   * @param radius  radius r
   * @param octant  point to octant.
   *
   * @return true, if search ball is completely inside the octant, false otherwise.
   */
  template <typename Distance>
  static bool inside(const PointT& query, float radius, const Octant* octant);

  OctreeParams params_;
  Octant* root_;
  const ContainerT* data_;

  std::vector<uint32_t> successors_;  // single connected list of next point indices...

  friend class ::OctreeTest;
};

template <typename PointT, typename ContainerT>
Octree<PointT, ContainerT>::Octant::Octant()
    : isLeaf(true), x(0.0f), y(0.0f), z(0.0f), extent(0.0f), start(0), end(0), size(0)
{
  memset(&child, 0, 8 * sizeof(Octant*));
}

template <typename PointT, typename ContainerT>
Octree<PointT, ContainerT>::Octant::~Octant()
{
  for (uint32_t i = 0; i < 8; ++i) delete child[i];
}

template <typename PointT, typename ContainerT>
Octree<PointT, ContainerT>::Octree()
    : root_(0), data_(0)
{
}

template <typename PointT, typename ContainerT>
Octree<PointT, ContainerT>::~Octree()
{
  delete root_;
  if (params_.copyPoints) delete data_;
}

template <typename PointT, typename ContainerT>
void Octree<PointT, ContainerT>::initialize(const ContainerT& pts, const OctreeParams& params)
{
  clear();
  params_ = params;

  if (params_.copyPoints)
    data_ = new ContainerT(pts);
  else
    data_ = &pts;

  const uint32_t N = pts.size();
  successors_ = std::vector<uint32_t>(N);

  // determine axis-aligned bounding box.
  float min[3], max[3];
  min[0] = get<0>(pts[0]);
  min[1] = get<1>(pts[0]);
  min[2] = get<2>(pts[0]);
  max[0] = min[0];
  max[1] = min[1];
  max[2] = min[2];

  for (uint32_t i = 0; i < N; ++i)
  {
    // initially each element links simply to the following element.
    successors_[i] = i + 1;

    const PointT& p = pts[i];

    if (get<0>(p) < min[0]) min[0] = get<0>(p);
    if (get<1>(p) < min[1]) min[1] = get<1>(p);
    if (get<2>(p) < min[2]) min[2] = get<2>(p);
    if (get<0>(p) > max[0]) max[0] = get<0>(p);
    if (get<1>(p) > max[1]) max[1] = get<1>(p);
    if (get<2>(p) > max[2]) max[2] = get<2>(p);
  }

  float ctr[3] = {min[0], min[1], min[2]};

  float maxextent = 0.5f * (max[0] - min[0]);
  ctr[0] += maxextent;
  for (uint32_t i = 1; i < 3; ++i)
  {
    float extent = 0.5f * (max[i] - min[i]);
    ctr[i] += extent;
    if (extent > maxextent) maxextent = extent;
  }

  root_ = createOctant(ctr[0], ctr[1], ctr[2], maxextent, 0, N - 1, N);
}

template <typename PointT, typename ContainerT>
void Octree<PointT, ContainerT>::initialize(const ContainerT& pts, const std::vector<uint32_t>& indexes,
                                            const OctreeParams& params)
{
  clear();
  params_ = params;

  if (params_.copyPoints)
    data_ = new ContainerT(pts);
  else
    data_ = &pts;

  const uint32_t N = pts.size();
  successors_ = std::vector<uint32_t>(N);

  if (indexes.size() == 0) return;

  // determine axis-aligned bounding box.
  uint32_t lastIdx = indexes[0];
  float min[3], max[3];
  min[0] = get<0>(pts[lastIdx]);
  min[1] = get<1>(pts[lastIdx]);
  min[2] = get<2>(pts[lastIdx]);
  max[0] = min[0];
  max[1] = min[1];
  max[2] = min[2];

  for (uint32_t i = 1; i < indexes.size(); ++i)
  {
    uint32_t idx = indexes[i];
    // initially each element links simply to the following element.
    successors_[lastIdx] = idx;

    const PointT& p = pts[idx];

    if (get<0>(p) < min[0]) min[0] = get<0>(p);
    if (get<1>(p) < min[1]) min[1] = get<1>(p);
    if (get<2>(p) < min[2]) min[2] = get<2>(p);
    if (get<0>(p) > max[0]) max[0] = get<0>(p);
    if (get<1>(p) > max[1]) max[1] = get<1>(p);
    if (get<2>(p) > max[2]) max[2] = get<2>(p);

    lastIdx = idx;
  }

  float ctr[3] = {min[0], min[1], min[2]};

  float maxextent = 0.5f * (max[0] - min[0]);
  ctr[0] += maxextent;
  for (uint32_t i = 1; i < 3; ++i)
  {
    float extent = 0.5f * (max[i] - min[i]);
    ctr[i] += extent;
    if (extent > maxextent) maxextent = extent;
  }

  root_ = createOctant(ctr[0], ctr[1], ctr[2], maxextent, indexes[0], lastIdx, indexes.size());
}

template <typename PointT, typename ContainerT>
void Octree<PointT, ContainerT>::clear()
{
  delete root_;
  if (params_.copyPoints) delete data_;
  root_ = 0;
  data_ = 0;
  successors_.clear();
}

template <typename PointT, typename ContainerT>
typename Octree<PointT, ContainerT>::Octant* Octree<PointT, ContainerT>::createOctant(float x, float y, float z,
                                                                                      float extent, uint32_t startIdx,
                                                                                      uint32_t endIdx, uint32_t size)
{
  // For a leaf we don't have to change anything; points are already correctly linked or correctly reordered.
  Octant* octant = new Octant;

  octant->isLeaf = true;

  octant->x = x;
  octant->y = y;
  octant->z = z;
  octant->extent = extent;

  octant->start = startIdx;
  octant->end = endIdx;
  octant->size = size;

  static const float factor[] = {-0.5f, 0.5f};

  // subdivide subset of points and re-link points according to Morton codes
  if (size > params_.bucketSize && extent > 2 * params_.minExtent)
  {
    octant->isLeaf = false;

    const ContainerT& points = *data_;
    std::vector<uint32_t> childStarts(8, 0);
    std::vector<uint32_t> childEnds(8, 0);
    std::vector<uint32_t> childSizes(8, 0);

    // re-link disjoint child subsets...
    uint32_t idx = startIdx;

    for (uint32_t i = 0; i < size; ++i)
    {
      const PointT& p = points[idx];

      // determine Morton code for each point...
      uint32_t mortonCode = 0;
      if (get<0>(p) > x) mortonCode |= 1;
      if (get<1>(p) > y) mortonCode |= 2;
      if (get<2>(p) > z) mortonCode |= 4;

      // set child starts and update successors...
      if (childSizes[mortonCode] == 0)
        childStarts[mortonCode] = idx;
      else
        successors_[childEnds[mortonCode]] = idx;
      childSizes[mortonCode] += 1;

      childEnds[mortonCode] = idx;
      idx = successors_[idx];
    }

    // now, we can create the child nodes...
    float childExtent = 0.5f * extent;
    bool firsttime = true;
    uint32_t lastChildIdx = 0;
    for (uint32_t i = 0; i < 8; ++i)
    {
      if (childSizes[i] == 0) continue;

      float childX = x + factor[(i & 1) > 0] * extent;
      float childY = y + factor[(i & 2) > 0] * extent;
      float childZ = z + factor[(i & 4) > 0] * extent;

      octant->child[i] = createOctant(childX, childY, childZ, childExtent, childStarts[i], childEnds[i], childSizes[i]);

      if (firsttime)
        octant->start = octant->child[i]->start;
      else
        successors_[octant->child[lastChildIdx]->end] =
            octant->child[i]->start;  // we have to ensure that also the child ends link to the next child start.

      lastChildIdx = i;
      octant->end = octant->child[i]->end;
      firsttime = false;
    }
  }

  return octant;
}

template <typename PointT, typename ContainerT>
template <typename Distance>
void Octree<PointT, ContainerT>::radiusNeighbors(const Octant* octant, const PointT& query, float radius,
                                                 float sqrRadius, std::vector<uint32_t>& resultIndices) const
{
  const ContainerT& points = *data_;

  // if search ball S(q,r) contains octant, simply add point indexes.
  if (contains<Distance>(query, sqrRadius, octant))
  {
    uint32_t idx = octant->start;
    for (uint32_t i = 0; i < octant->size; ++i)
    {
      resultIndices.push_back(idx);
      idx = successors_[idx];
    }

    return;  // early pruning.
  }

  if (octant->isLeaf)
  {
    uint32_t idx = octant->start;
    for (uint32_t i = 0; i < octant->size; ++i)
    {
      const PointT& p = points[idx];
      float dist = Distance::compute(query, p);
      if (dist < sqrRadius) resultIndices.push_back(idx);
      idx = successors_[idx];
    }

    return;
  }

  // check whether child nodes are in range.
  for (uint32_t c = 0; c < 8; ++c)
  {
    if (octant->child[c] == 0) continue;
    if (!overlaps<Distance>(query, radius, sqrRadius, octant->child[c])) continue;
    radiusNeighbors<Distance>(octant->child[c], query, radius, sqrRadius, resultIndices);
  }
}

template <typename PointT, typename ContainerT>
template <typename Distance>
void Octree<PointT, ContainerT>::radiusNeighbors(const Octant* octant, const PointT& query, float radius,
                                                 float sqrRadius, std::vector<uint32_t>& resultIndices,
                                                 std::vector<float>& distances) const
{
  const ContainerT& points = *data_;

  // if search ball S(q,r) contains octant, simply add point indexes and compute squared distances.
  if (contains<Distance>(query, sqrRadius, octant))
  {
    uint32_t idx = octant->start;
    for (uint32_t i = 0; i < octant->size; ++i)
    {
      resultIndices.push_back(idx);
      distances.push_back(Distance::compute(query, points[idx]));
      idx = successors_[idx];
    }

    return;  // early pruning.
  }

  if (octant->isLeaf)
  {
    uint32_t idx = octant->start;
    for (uint32_t i = 0; i < octant->size; ++i)
    {
      const PointT& p = points[idx];
      float dist = Distance::compute(query, p);
      if (dist < sqrRadius)
      {
        resultIndices.push_back(idx);
        distances.push_back(dist);
      }
      idx = successors_[idx];
    }

    return;
  }

  // check whether child nodes are in range.
  for (uint32_t c = 0; c < 8; ++c)
  {
    if (octant->child[c] == 0) continue;
    if (!overlaps<Distance>(query, radius, sqrRadius, octant->child[c])) continue;
    radiusNeighbors<Distance>(octant->child[c], query, radius, sqrRadius, resultIndices, distances);
  }
}

template <typename PointT, typename ContainerT>
template <typename Distance>
void Octree<PointT, ContainerT>::radiusNeighbors(const PointT& query, float radius,
                                                 std::vector<uint32_t>& resultIndices) const
{
  resultIndices.clear();
  if (root_ == 0) return;

  float sqrRadius = Distance::sqr(radius);  // "squared" radius
  radiusNeighbors<Distance>(root_, query, radius, sqrRadius, resultIndices);
}

template <typename PointT, typename ContainerT>
template <typename Distance>
void Octree<PointT, ContainerT>::radiusNeighbors(const PointT& query, float radius,
                                                 std::vector<uint32_t>& resultIndices,
                                                 std::vector<float>& distances) const
{
  resultIndices.clear();
  distances.clear();
  if (root_ == 0) return;

  float sqrRadius = Distance::sqr(radius);  // "squared" radius
  radiusNeighbors<Distance>(root_, query, radius, sqrRadius, resultIndices, distances);
}

template <typename PointT, typename ContainerT>
template <typename Distance>
bool Octree<PointT, ContainerT>::overlaps(const PointT& query, float radius, float sqRadius, const Octant* o)
{
  // we exploit the symmetry to reduce the test to testing if its inside the Minkowski sum around the positive quadrant.
  float x = get<0>(query) - o->x;
  float y = get<1>(query) - o->y;
  float z = get<2>(query) - o->z;

  x = std::abs(x);
  y = std::abs(y);
  z = std::abs(z);

  float maxdist = radius + o->extent;

  // Completely outside, since q' is outside the relevant area.
  if (x > maxdist || y > maxdist || z > maxdist) return false;

  int32_t num_less_extent = (x < o->extent) + (y < o->extent) + (z < o->extent);

  // Checking different cases:

  // a. inside the surface region of the octant.
  if (num_less_extent > 1) return true;

  // b. checking the corner region && edge region.
  x = std::max(x - o->extent, 0.0f);
  y = std::max(y - o->extent, 0.0f);
  z = std::max(z - o->extent, 0.0f);

  return (Distance::norm(x, y, z) < sqRadius);
}

template <typename PointT, typename ContainerT>
template <typename Distance>
bool Octree<PointT, ContainerT>::contains(const PointT& query, float sqRadius, const Octant* o)
{
  // we exploit the symmetry to reduce the test to test
  // whether the farthest corner is inside the search ball.
  float x = get<0>(query) - o->x;
  float y = get<1>(query) - o->y;
  float z = get<2>(query) - o->z;

  x = std::abs(x);
  y = std::abs(y);
  z = std::abs(z);
  // reminder: (x, y, z) - (-e, -e, -e) = (x, y, z) + (e, e, e)
  x += o->extent;
  y += o->extent;
  z += o->extent;

  return (Distance::norm(x, y, z) < sqRadius);
}

template <typename PointT, typename ContainerT>
template <typename Distance>
int32_t Octree<PointT, ContainerT>::findNeighbor(const PointT& query, float minDistance) const
{
  float maxDistance = std::numeric_limits<float>::infinity();
  int32_t resultIndex = -1;
  if(root_ == 0) return resultIndex;
  
  findNeighbor<Distance>(root_, query, minDistance, maxDistance, resultIndex);

  return resultIndex;
}

template <typename PointT, typename ContainerT>
template <typename Distance>
bool Octree<PointT, ContainerT>::findNeighbor(const Octant* octant, const PointT& query, float minDistance,
                                              float& maxDistance, int32_t& resultIndex) const
{
  const ContainerT& points = *data_;
  // 1. first descend to leaf and check in leafs points.
  if (octant->isLeaf)
  {
    uint32_t idx = octant->start;
    float sqrMaxDistance = Distance::sqr(maxDistance);
    float sqrMinDistance = (minDistance < 0) ? minDistance : Distance::sqr(minDistance);

    for (uint32_t i = 0; i < octant->size; ++i)
    {
      const PointT& p = points[idx];
      float dist = Distance::compute(query, p);
      if (dist > sqrMinDistance && dist < sqrMaxDistance)
      {
        resultIndex = idx;
        sqrMaxDistance = dist;
      }
      idx = successors_[idx];
    }

    maxDistance = Distance::sqrt(sqrMaxDistance);
    return inside<Distance>(query, maxDistance, octant);
  }

  // determine Morton code for each point...
  uint32_t mortonCode = 0;
  if (get<0>(query) > octant->x) mortonCode |= 1;
  if (get<1>(query) > octant->y) mortonCode |= 2;
  if (get<2>(query) > octant->z) mortonCode |= 4;

  if (octant->child[mortonCode] != 0)
  {
    if (findNeighbor<Distance>(octant->child[mortonCode], query, minDistance, maxDistance, resultIndex)) return true;
  }

  // 2. if current best point completely inside, just return.
  float sqrMaxDistance = Distance::sqr(maxDistance);

  // 3. check adjacent octants for overlap and check these if necessary.
  for (uint32_t c = 0; c < 8; ++c)
  {
    if (c == mortonCode) continue;
    if (octant->child[c] == 0) continue;
    if (!overlaps<Distance>(query, maxDistance, sqrMaxDistance, octant->child[c])) continue;
    if (findNeighbor<Distance>(octant->child[c], query, minDistance, maxDistance, resultIndex))
      return true;  // early pruning
  }

  // all children have been checked...check if point is inside the current octant...
  return inside<Distance>(query, maxDistance, octant);
}

template <typename PointT, typename ContainerT>
template <typename Distance>
bool Octree<PointT, ContainerT>::inside(const PointT& query, float radius, const Octant* octant)
{
  // we exploit the symmetry to reduce the test to test
  // whether the farthest corner is inside the search ball.
  float x = get<0>(query) - octant->x;
  float y = get<1>(query) - octant->y;
  float z = get<2>(query) - octant->z;

  x = std::abs(x) + radius;
  y = std::abs(y) + radius;
  z = std::abs(z) + radius;

  if (x > octant->extent) return false;
  if (y > octant->extent) return false;
  if (z > octant->extent) return false;

  return true;
}
}

#endif /* OCTREE_HPP_ */
