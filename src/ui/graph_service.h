// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Saurabh Verma
#pragma once

#include <string>
#include <string_view>

namespace rw::ui
{
struct GraphServiceConfig
{
    std::string executable;
    std::string root;
};

// Thin composition facade over CodeCortex's existing graph/test/git commands.
// It does not build or own a second graph/index.
class GraphService
{
public:
    explicit GraphService( GraphServiceConfig cfg ) : cfg_( std::move( cfg ) ) {}

    std::string symbol( std::string_view target ) const;
    std::string architecture() const;
    std::string community( std::string_view id ) const;
    std::string path( std::string_view from, std::string_view to ) const;
    std::string dependencies() const;

private:
    GraphServiceConfig cfg_;
};
} // namespace rw::ui
