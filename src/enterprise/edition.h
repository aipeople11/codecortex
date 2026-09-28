// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Saurabh Verma
#pragma once

#include <cstdlib>
#include <string>
#include <string_view>

namespace rw::enterprise
{

struct Profile
{
    std::string edition = "community";
    std::string organization;
};

inline Profile loadProfile()
{
    Profile p;
    if( const char* edition = std::getenv( "CODECORTEX_EDITION" ); edition && *edition )
    {
        const std::string_view v( edition );
        if( v == "enterprise" || v == "ENTERPRISE" ) p.edition = "enterprise";
    }
    if( const char* org = std::getenv( "CODECORTEX_ORG" ); org && *org ) p.organization = org;
    return p;
}

} // namespace rw::enterprise
