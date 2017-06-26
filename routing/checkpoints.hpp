#pragma once

#include "geometry/mercator.hpp"
#include "geometry/point2d.hpp"

#include "base/assert.hpp"

#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace routing
{
class Checkpoints final
{
public:
  Checkpoints() = default;

  Checkpoints(m2::PointD const & start, m2::PointD const & finish) : m_points({start, finish}) {}

  Checkpoints(size_t arrivedIdx, std::vector<m2::PointD> && points)
    : m_points(std::move(points)), m_arrivedIdx(arrivedIdx)
  {
    CheckValid();
  }

  m2::PointD const & GetStart() const
  {
    CheckValid();
    return m_points.front();
  }

  m2::PointD const & GetFinish() const
  {
    CheckValid();
    return m_points.back();
  }

  void SetStart(m2::PointD const & start)
  {
    CheckValid();
    m_points.front() = start;
  }

  m2::PointD const & GetPoint(size_t pointIdx) const
  {
    CHECK_LESS(pointIdx, m_points.size(), ());
    return m_points[pointIdx];
  }

  std::vector<m2::PointD> const & GetPoints() const
  {
    CheckValid();
    return m_points;
  }

  size_t GetNumSubroutes() const
  {
    CheckValid();
    return m_points.size() - 1;
  }

  size_t GetArrivedIdx() const { return m_arrivedIdx; }

  void ArriveNextPoint()
  {
    ++m_arrivedIdx;
    CheckValid();
  }

  void CheckValid() const
  {
    CHECK_GREATER_OR_EQUAL(m_points.size(), 2,
                           ("Checkpoints should at least contain start and finish"));
    CHECK_LESS(m_arrivedIdx, m_points.size(), ());
  }

private:
  // m_points contains start, finish and intermediate points.
  std::vector<m2::PointD> m_points;
  // Arrived idx is the index of the checkpoint by which the user passed by.
  // By default, user has arrived at 0, it is start point.
  size_t m_arrivedIdx = 0;
};

inline std::string DebugPrint(Checkpoints const & checkpoints)
{
  std::ostringstream out;
  out.precision(8);
  out << "Checkpoints(";
  for (auto const & point : checkpoints.GetPoints())
  {
    auto const latlon = MercatorBounds::ToLatLon(point);
    out << latlon.lat << " " << latlon.lon << ", ";
  }
  out << "arrived: " << checkpoints.GetArrivedIdx() << ")";
  return out.str();
}
}  // namespace routing