#include "Actor.hpp"
#include "hecl/Blender/Connection.hpp"

namespace DataSpec::DNAMP1 {

zeus::CAABox Actor::getVISIAABB(hecl::blender::Token& btok) const {
  hecl::blender::Connection& conn = btok.getBlenderConnection();
  zeus::CAABox aabbOut;

  if (model.isValid()) {
    hecl::ProjectPath path = UniqueIDBridge::TranslatePakIdToPath(model);
    conn.openBlend(path);
    hecl::blender::DataStream ds = conn.beginData();
    auto aabb = ds.getMeshAABB();
    aabbOut = zeus::CAABox(aabb.first, aabb.second);
  } else if (animationParameters.animationCharacterSet.isValid()) {
    hecl::ProjectPath path = UniqueIDBridge::TranslatePakIdToPath(animationParameters.animationCharacterSet);
    conn.openBlend(path.getWithExtension(".blend", true));
    hecl::blender::DataStream ds = conn.beginData();
    auto aabb = ds.getMeshAABB();
    aabbOut = zeus::CAABox(aabb.first, aabb.second);
  }

  if (aabbOut.min.x() > aabbOut.max.x())
    return {};

  zeus::CTransform xf = ConvertEditorEulerToTransform4f(scale, orientation, location);
  return aabbOut.getTransformedAABox(xf);
}

} // namespace DataSpec::DNAMP1
