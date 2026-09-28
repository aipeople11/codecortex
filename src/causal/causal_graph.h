// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Saurabh Verma
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "btree.hpp"

namespace rw::causal
{

using NodeId = std::uint32_t;
inline constexpr NodeId kNoNode = static_cast<NodeId>( -1 );

enum class NodeType : std::uint8_t
{
    File,
    Symbol,
    Test,
    Commit,
    RuntimeError,
    Trace,
    AgentRun,
    ToolCall,
    Hypothesis,
    Decision,
    Patch,
    Memory,
    Model,
    Search,
    Proof
};

enum class EdgeType : std::uint8_t
{
    Calls,
    Imports,
    References,
    ChangedBy,
    TestedBy,
    FailedAt,
    InspectedBy,
    AttemptedBy,
    RejectedBecause,
    FixedBy,
    VerifiedBy,
    RememberedAs,
    RunsModel,
    Invoked,
    Read,
    Searched,
    Edited,
    Executed,
    ObservedBefore
};

std::string_view nodeTypeName( NodeType type ) noexcept;
std::string_view edgeTypeName( EdgeType type ) noexcept;

struct Node
{
    NodeId id = kNoNode;
    NodeType type = NodeType::Symbol;
    std::string key;       // globally stable within one graph, e.g. symbol:src/auth.cpp:refresh
    std::string label;     // human-facing display label
    std::string evidence;  // compact provenance/evidence text; never treated as executable input
};

struct Edge
{
    NodeId from = kNoNode;
    NodeId to = kNoNode;
    EdgeType type = EdgeType::References;
    std::string evidence;
};

class Graph
{
  public:
    NodeId upsertNode( NodeType type, std::string key, std::string label = {}, std::string evidence = {} );
    bool addEdge( NodeId from, NodeId to, EdgeType type, std::string evidence = {} );
    void finalize();

    [[nodiscard]] const Node* find( std::string_view key ) const noexcept;
    [[nodiscard]] std::vector<const Edge*> outgoing( NodeId id ) const;
    [[nodiscard]] std::vector<const Edge*> incoming( NodeId id ) const;

    [[nodiscard]] std::span<const Node> nodes() const noexcept { return nodes_; }
    [[nodiscard]] std::span<const Edge> edges() const noexcept { return edges_; }
    [[nodiscard]] std::string toJson() const;

  private:
    std::vector<Node> nodes_;
    std::vector<Edge> edges_;
    gtl::btree_map<std::string, NodeId> byKey_;
    bool finalized_ = false;
};

} // namespace rw::causal
