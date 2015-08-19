#ifndef __RETRO_CBASICS_HPP__
#define __RETRO_CBASICS_HPP__

#include <stdint.h>
#include <stdlib.h>

#include "RetroTypes.hpp"

#include <Athena/IStreamReader.hpp>
#include <Athena/IStreamWriter.hpp>


namespace Retro
{

using CInputStream = Athena::io::IStreamReader;
using COutputStream = Athena::io::IStreamWriter;

class CBasics
{
public:
    static void Init();
    static const char* Stringize(const char* fmt, ...);
};

}

#endif // __RETRO_CBASICS_HPP__
