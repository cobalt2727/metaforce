#ifndef __PSHAG_CPARTICLESWOOSHDATAFACTORY_HPP__
#define __PSHAG_CPARTICLESWOOSHDATAFACTORY_HPP__

#include "RetroTypes.hpp"
#include "IObj.hpp"
#include "CToken.hpp"
#include "IOStreams.hpp"

namespace pshag
{
class CSwooshDescription;
class CSimplePool;
class CParticleSwooshDataFactory
{
    static CSwooshDescription* CreateGeneratorDescription(CInputStream& in, CSimplePool* resPool);
    static bool CreateWPSM(CSwooshDescription* desc, CInputStream& in, CSimplePool* resPool);
public:
    static CSwooshDescription* GetGeneratorDesc(CInputStream& in, CSimplePool* resPool);
};

std::unique_ptr<IObj> FParticleSwooshDataFactory(const SObjectTag& tag, CInputStream& in, const CVParamTransfer& vparms);
}

#endif // __PSHAG_CPARTICLESWOOSHDATAFACTORY_HPP__
