#pragma once

#include "structlayout.h"

#include <cstdint>

struct FixtureLayout
{
    std::uint32_t value = 0;
#if defined( CODECORTEX_LAYOUT_FIXTURE_WIDE )
    std::uint32_t extra = 0;
#endif
};

CODECORTEX_LAYOUT_REGISTER_TYPES( CODECORTEX_LAYOUT_TYPE_ENTRY( FixtureLayout ) );
