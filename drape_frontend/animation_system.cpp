#include "animation_system.h"
#include "animation/interpolations.hpp"

#include "base/logging.hpp"

#include "boost/variant/variant.hpp"
#include "boost/variant/get.hpp"

namespace df
{

namespace
{

double CalcAnimSpeedDuration(double pxDiff, double pxSpeed)
{
  double const kEps = 1e-5;

  if (my::AlmostEqualAbs(pxDiff, 0.0, kEps))
    return 0.0;

  return fabs(pxDiff) / pxSpeed;
}

}

bool Animation::CouldBeMixedWith(TObject object, TObjectProperties const & properties)
{
  if (!m_couldBeMixed)
    return false;
  ASSERT(HasObject(object), ());

  TObjectProperties const & selfProperties = GetProperties(object);
  TObjectProperties intersection;
  set_intersection(selfProperties.begin(), selfProperties.end(),
                   properties.begin(), properties.end(),
                   inserter(intersection, intersection.end()));
  return intersection.empty();
}

bool Animation::CouldBeMixedWith(Animation const & animation)
{
  if (!m_couldBeMixed || animation.m_couldBeMixed)
    return false;

  for (auto const & object : animation.GetObjects())
  {
    if (!HasObject(object))
      continue;
    if (!CouldBeMixedWith(object, animation.GetProperties(object)))
      return false;
  }
  return true;
}

Interpolator::Interpolator(double duration, double delay)
  : m_elapsedTime(0.0)
  , m_duration(duration)
  , m_delay(delay)
{
  ASSERT(m_duration >= 0.0, ());
}

Interpolator::~Interpolator()
{
}

bool Interpolator::IsFinished() const
{
  return m_elapsedTime > (m_duration + m_delay);
}

void Interpolator::Advance(double elapsedSeconds)
{
  m_elapsedTime += elapsedSeconds;
}

void Interpolator::SetMaxDuration(double maxDuration)
{
  m_duration = min(m_duration, maxDuration);
}

double Interpolator::GetT() const
{
  if (IsFinished())
    return 1.0;

  return max(m_elapsedTime - m_delay, 0.0) / m_duration;
}

double Interpolator::GetElapsedTime() const
{
  return m_elapsedTime;
}

double Interpolator::GetDuration() const
{
  return m_duration;
}

//static
double PositionInterpolator::GetMoveDuration(m2::PointD const & startPosition, m2::PointD const & endPosition, ScreenBase const & convertor)
{
  double const kMinMoveDuration = 0.2;
  double const kMinSpeedScalar = 0.2;
  double const kMaxSpeedScalar = 7.0;
  double const kEps = 1e-5;

  m2::RectD const & dispPxRect = convertor.PixelRect();
  double const pixelLength = convertor.GtoP(endPosition).Length(convertor.GtoP(startPosition));
  if (pixelLength < kEps)
    return 0.0;

  double const minSize = min(dispPxRect.SizeX(), dispPxRect.SizeY());
  if (pixelLength < kMinSpeedScalar * minSize)
    return kMinMoveDuration;

  double const pixelSpeed = kMaxSpeedScalar * minSize;
  return CalcAnimSpeedDuration(pixelLength, pixelSpeed);
}

PositionInterpolator::PositionInterpolator(m2::PointD const & startPosition, m2::PointD const & endPosition, ScreenBase const & convertor)
  : PositionInterpolator(0.0 /* delay */, startPosition, endPosition, convertor)
{
}

PositionInterpolator::PositionInterpolator(double delay, m2::PointD const & startPosition, m2::PointD const & endPosition, ScreenBase const & convertor)
  : Interpolator(PositionInterpolator::GetMoveDuration(startPosition, endPosition, convertor), delay)
  , m_startPosition(startPosition)
  , m_endPosition(endPosition)
  , m_position(startPosition)
{

}

void PositionInterpolator::Advance(double elapsedSeconds)
{
  TBase::Advance(elapsedSeconds);
  m_position = InterpolatePoint(m_startPosition, m_endPosition, GetT());
}

// static
double AngleInterpolator::GetRotateDuration(double startAngle, double endAngle)
{
  return 0.5 * fabs(endAngle - startAngle) / math::pi4;
}

AngleInterpolator::AngleInterpolator(double startAngle, double endAngle)
  : AngleInterpolator(0.0 /* delay */, startAngle, endAngle)
{
}

AngleInterpolator::AngleInterpolator(double delay, double startAngle, double endAngle)
  : Interpolator(AngleInterpolator::GetRotateDuration(startAngle, endAngle), delay)
  , m_startAngle(startAngle)
  , m_endAngle(endAngle)
  , m_angle(startAngle)
{
}

void AngleInterpolator::Advance(double elapsedSeconds)
{
  TBase::Advance(elapsedSeconds);
  m_angle = InterpolateDouble(m_startAngle, m_endAngle, GetT());
}

// static
double ScaleInterpolator::GetScaleDuration(double startScale, double endScale)
{
  // Resize 2.0 times should be done for 0.3 seconds.
  double constexpr kPixelSpeed = 2.0 / 0.3;

  if (startScale > endScale)
    swap(startScale, endScale);

  return CalcAnimSpeedDuration(endScale / startScale, kPixelSpeed);
}

ScaleInterpolator::ScaleInterpolator(double startScale, double endScale)
  : ScaleInterpolator(0.0 /* delay */, startScale, endScale)
{
}

ScaleInterpolator::ScaleInterpolator(double delay, double startScale, double endScale)
  : Interpolator(ScaleInterpolator::GetScaleDuration(startScale, endScale), delay)
  , m_startScale(startScale)
  , m_endScale(endScale)
  , m_scale(startScale)
{
}

void ScaleInterpolator::Advance(double elapsedSeconds)
{
  TBase::Advance(elapsedSeconds);
  m_scale = InterpolateDouble(m_startScale, m_endScale, GetT());
}

FollowAnimation::FollowAnimation(m2::PointD const & startPos, m2::PointD const & endPos,
                                 double startAngle, double endAngle,
                                 double startScale, double endScale, ScreenBase const & convertor)
  : Animation(true /* couldBeInterrupted */, false /* couldBeMixed */)
{
  m_objects.insert(Animation::MapPlane);
  SetMove(startPos, endPos, convertor);
  SetRotate(startAngle, endAngle);
  SetScale(startScale, endScale);
}

FollowAnimation::FollowAnimation()
  : Animation(true /* couldBeInterrupted */, false /* couldBeMixed */)
{
  m_objects.insert(Animation::MapPlane);
}

void FollowAnimation::SetMove(m2::PointD const & startPos, m2::PointD const & endPos,
                              ScreenBase const & convertor)
{
  if (startPos != endPos)
  {
    m_positionInterpolator = make_unique_dp<PositionInterpolator>(startPos, endPos, convertor);
    m_properties.insert(Animation::Position);
  }
}

void FollowAnimation::SetRotate(double startAngle, double endAngle)
{
  if (startAngle != endAngle)
  {
    m_angleInterpolator = make_unique_dp<AngleInterpolator>(startAngle, endAngle);
    m_properties.insert(Animation::Angle);
  }
}

void FollowAnimation::SetScale(double startScale, double endScale)
{
  if (startScale != endScale)
  {
    m_scaleInterpolator = make_unique_dp<ScaleInterpolator>(startScale, endScale);
    m_properties.insert(Animation::Scale);
  }
}

Animation::TObjectProperties const & FollowAnimation::GetProperties(TObject object) const
{
  ASSERT(object == Animation::MapPlane, ());
  return m_properties;
}

bool FollowAnimation::HasProperty(TObject object, TProperty property) const
{
  return HasObject(object) && m_properties.find(property) != m_properties.end();
}

void FollowAnimation::Advance(double elapsedSeconds)
{
  if (m_angleInterpolator != nullptr)
    m_angleInterpolator->Advance(elapsedSeconds);
  if (m_scaleInterpolator != nullptr)
    m_scaleInterpolator->Advance(elapsedSeconds);
  if (m_positionInterpolator != nullptr)
    m_positionInterpolator->Advance(elapsedSeconds);
}

void FollowAnimation::SetMaxDuration(double maxDuration)
{
  if (m_angleInterpolator != nullptr)
    m_angleInterpolator->SetMaxDuration(maxDuration);
  if (m_scaleInterpolator != nullptr)
    m_scaleInterpolator->SetMaxDuration(maxDuration);
  if (m_positionInterpolator != nullptr)
    m_positionInterpolator->SetMaxDuration(maxDuration);
}

double FollowAnimation::GetDuration() const
{
  double duration = 0;
  if (m_angleInterpolator != nullptr)
    duration = m_angleInterpolator->GetDuration();
  if (m_scaleInterpolator != nullptr)
    duration = max(duration, m_scaleInterpolator->GetDuration());
  if (m_positionInterpolator != nullptr)
    duration = max(duration, m_positionInterpolator->GetDuration());
  return duration;
}

bool FollowAnimation::IsFinished() const
{
  return ((m_angleInterpolator == nullptr || m_angleInterpolator->IsFinished())
      && (m_scaleInterpolator == nullptr || m_scaleInterpolator->IsFinished())
      && (m_positionInterpolator == nullptr || m_positionInterpolator->IsFinished()));
}

Animation::TPropValue FollowAnimation::GetProperty(TObject object, TProperty property) const
{
  ASSERT(object == Animation::MapPlane, ());

  switch (property)
  {
  case Animation::Position:
    ASSERT(m_positionInterpolator != nullptr, ());
    if (m_positionInterpolator != nullptr)
      return m_positionInterpolator->GetPosition();
    break;
  case Animation::Scale:
    ASSERT(m_scaleInterpolator != nullptr, ());
    if (m_scaleInterpolator != nullptr)
      return m_scaleInterpolator->GetScale();
    break;
  case Animation::Angle:
    ASSERT(m_angleInterpolator != nullptr, ());
    if (m_angleInterpolator != nullptr)
      return m_angleInterpolator->GetAngle();
    break;
  default:
    ASSERT(!"Wrong property", ());
  }

  return 0.0;
}

Animation::TObjectProperties const & ParallelAnimation::GetProperties(TObject object) const
{
  ASSERT(HasObject(object), ());
  return m_properties.find(object)->second;
}

bool ParallelAnimation::HasProperty(TObject object, TProperty property) const
{
  if (!HasObject(object))
    return false;
  TObjectProperties properties = GetProperties(object);
  return properties.find(property) != properties.end();
}

void ParallelAnimation::AddAnimation(drape_ptr<Animation> && animation)
{
  TAnimObjects const & objects = animation->GetObjects();
  m_objects.insert(objects.begin(), objects.end());
  for (auto const & object : objects)
  {
    TObjectProperties const & properties = animation->GetProperties(object);
    m_properties[object].insert(properties.begin(), properties.end());
  }
  m_animations.push_back(move(animation));
}

void ParallelAnimation::OnStart()
{
  for (auto & anim : m_animations)
    anim->OnStart();
}

void ParallelAnimation::OnFinish()
{

}

void ParallelAnimation::Advance(double elapsedSeconds)
{
  auto iter = m_animations.begin();
  while (iter != m_animations.end())
  {
    (*iter)->Advance(elapsedSeconds);
    if ((*iter)->IsFinished())
    {
      (*iter)->OnFinish();
      iter = m_animations.erase(iter);
    }
    else
      ++iter;
  }
}

Animation::TAnimObjects const & SequenceAnimation::GetObjects() const
{
  ASSERT(!m_animations.empty(), ());
  return m_animations.front()->GetObjects();
}

bool SequenceAnimation::HasObject(TObject object) const
{
  ASSERT(!m_animations.empty(), ());
  return m_animations.front()->HasObject(object);
}

Animation::TObjectProperties const & SequenceAnimation::GetProperties(TObject object) const
{
  ASSERT(!m_animations.empty(), ());
  return m_animations.front()->GetProperties(object);
}

bool SequenceAnimation::HasProperty(TObject object, TProperty property) const
{
  ASSERT(!m_animations.empty(), ());
  return m_animations.front()->HasProperty(object, property);
}

void SequenceAnimation::AddAnimation(drape_ptr<Animation> && animation)
{
  m_animations.push_back(move(animation));
}

void SequenceAnimation::OnStart()
{
  if (m_animations.empty())
    return;
  m_animations.front()->OnStart();
}

void SequenceAnimation::OnFinish()
{

}

void SequenceAnimation::Advance(double elapsedSeconds)
{
  if (m_animations.empty())
    return;
  m_animations.front()->Advance(elapsedSeconds);
  if (m_animations.front()->IsFinished())
  {
    m_animations.front()->OnFinish();
    m_animations.pop_front();
  }
}

AnimationSystem::AnimationSystem()
{

}

m2::AnyRectD AnimationSystem::GetRect(ScreenBase const & currentScreen)
{
  const Animation::TObject obj = Animation::MapPlane;
  double scale = boost::get<double>(
        GetProperty(obj, Animation::Scale, currentScreen.GetScale()));
  double angle = boost::get<double>(
        GetProperty(obj, Animation::Angle, currentScreen.GetAngle()));
  m2::PointD pos = boost::get<m2::PointD>(
        GetProperty(obj, Animation::Position, currentScreen.GlobalRect().GlobalZero()));
  m2::RectD rect = currentScreen.PixelRect();
  rect.Offset(-rect.Center());
  rect.Scale(scale);
  return m2::AnyRectD(pos, angle, rect);
}

bool AnimationSystem::AnimationExists(Animation::TObject object) const
{
  if (m_animationChain.empty())
    return false;
  for (auto const & anim : m_animationChain.front())
  {
    if (anim->HasObject(object))
      return true;
  }
  for (auto it = m_propertyCache.begin(); it != m_propertyCache.end(); ++it)
  {
    if (it->first.first == object)
      return true;
  }
  return false;
}

AnimationSystem & AnimationSystem::Instance()
{
  static AnimationSystem animSystem;
  return animSystem;
}

void AnimationSystem::AddAnimation(drape_ptr<Animation> && animation, bool force)
{
  for (auto & lst : m_animationChain)
  {
    bool couldBeMixed = true;
    for (auto it = lst.begin(); it != lst.end();)
    {
      auto & anim = *it;
      if (!anim->CouldBeMixedWith(*animation))
      {
        if (!force || !anim->CouldBeInterrupted())
        {
          couldBeMixed = false;
          break;
        }
        // TODO: do not interrupt anything until it's not clear that we can mix
        anim->Interrupt();
        SaveAnimationResult(*anim);
        it = lst.erase(it);
      }
      else
      {
        ++it;
      }
    }
    if (couldBeMixed)
    {
      animation->OnStart();
      lst.emplace_back(move(animation));
      return;
    }
  }

  PushAnimation(move(animation));
}

void AnimationSystem::PushAnimation(drape_ptr<Animation> && animation)
{
  animation->OnStart();
  TAnimationList list;
  list.emplace_back(move(animation));
  m_animationChain.emplace_back(move(list));
}

void AnimationSystem::Advance(double elapsedSeconds)
{
  if (m_animationChain.empty())
    return;

  TAnimationList & frontList = m_animationChain.front();
  for (auto it = frontList.begin(); it != frontList.end();)
  {
    auto & anim = *it;
    anim->Advance(elapsedSeconds);
    if (anim->IsFinished())
    {
      anim->OnFinish();
      SaveAnimationResult(*anim);
      it = frontList.erase(it);
    }
    else
    {
      ++it;
    }
  }
}

Animation::TPropValue AnimationSystem::GetProperty(Animation::TObject object, Animation::TProperty property, Animation::TPropValue current) const
{
  if (!m_animationChain.empty())
  {
    for (auto const & anim : m_animationChain.front())
    {
      if (anim->HasProperty(object, property))
        return anim->GetProperty(object, property);
    }
  }
  auto it = m_propertyCache.find(make_pair(object, property));
  if (it != m_propertyCache.end())
  {
    Animation::TPropValue value(it->second);
    m_propertyCache.erase(it);
    return value;
  }
  return current;
}

void AnimationSystem::SaveAnimationResult(Animation const & animation)
{
  for (auto const & object : animation.GetObjects())
  {
    for (auto const & property : animation.GetProperties(object))
    {
      m_propertyCache[make_pair(object, property)] = animation.GetProperty(object, property);
    }
  }
}

} // namespace df