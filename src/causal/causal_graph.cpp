// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Saurabh Verma
#include "causal_graph.h"

#include "infra/jsonesc.h"

#include <algorithm>
#include <tuple>

namespace rw::causal
{
namespace
{
std::string jsonString( std::string_view value )
{
    std::string out;
    out.push_back( '"' );
    rw::jsonesc::escapeInto( value, out, true, true, true );
    out.push_back( '"' );
    return out;
}
}

std::string_view nodeTypeName( NodeType type ) noexcept
{
    switch( type )
    {
        case NodeType::File: return "file";
        case NodeType::Symbol: return "symbol";
        case NodeType::Test: return "test";
        case NodeType::Commit: return "commit";
        case NodeType::RuntimeError: return "runtime_error";
        case NodeType::Trace: return "trace";
        case NodeType::AgentRun: return "agent_run";
        case NodeType::ToolCall: return "tool_call";
        case NodeType::Hypothesis: return "hypothesis";
        case NodeType::Decision: return "decision";
        case NodeType::Patch: return "patch";
        case NodeType::Memory: return "memory";
        case NodeType::Model: return "model";
        case NodeType::Search: return "search";
        case NodeType::Proof: return "proof";
    }
    return "unknown";
}

std::string_view edgeTypeName( EdgeType type ) noexcept
{
    switch( type )
    {
        case EdgeType::Calls: return "CALLS";
        case EdgeType::Imports: return "IMPORTS";
        case EdgeType::References: return "REFERENCES";
        case EdgeType::ChangedBy: return "CHANGED_BY";
        case EdgeType::TestedBy: return "TESTED_BY";
        case EdgeType::FailedAt: return "FAILED_AT";
        case EdgeType::InspectedBy: return "INSPECTED_BY";
        case EdgeType::AttemptedBy: return "ATTEMPTED_BY";
        case EdgeType::RejectedBecause: return "REJECTED_BECAUSE";
        case EdgeType::FixedBy: return "FIXED_BY";
        case EdgeType::VerifiedBy: return "VERIFIED_BY";
        case EdgeType::RememberedAs: return "REMEMBERED_AS";
        case EdgeType::RunsModel: return "RUNS_MODEL";
        case EdgeType::Invoked: return "INVOKED";
        case EdgeType::Read: return "READ";
        case EdgeType::Searched: return "SEARCHED";
        case EdgeType::Edited: return "EDITED";
        case EdgeType::Executed: return "EXECUTED";
        case EdgeType::ObservedBefore: return "OBSERVED_BEFORE";
    }
    return "UNKNOWN";
}

NodeId Graph::upsertNode( NodeType type, std::string key, std::string label, std::string evidence )
{
    if( auto it = byKey_.find( key ); it != byKey_.end() )
    {
        Node& node = nodes_[it->second];
        if( node.label.empty() && !label.empty() ) node.label = std::move( label );
        if( node.evidence.empty() && !evidence.empty() ) node.evidence = std::move( evidence );
        return node.id;
    }
    const NodeId id = static_cast<NodeId>( nodes_.size() );
    byKey_.emplace( key, id );
    nodes_.push_back( { id, type, std::move( key ), std::move( label ), std::move( evidence ) } );
    finalized_ = false;
    return id;
}

bool Graph::addEdge( NodeId from, NodeId to, EdgeType type, std::string evidence )
{
    if( from >= nodes_.size() || to >= nodes_.size() ) return false;
    edges_.push_back( { from, to, type, std::move( evidence ) } );
    finalized_ = false;
    return true;
}

void Graph::finalize()
{
    std::sort( edges_.begin(), edges_.end(), []( const Edge& a, const Edge& b )
    {
        return std::tie( a.from, a.to, a.type, a.evidence ) < std::tie( b.from, b.to, b.type, b.evidence );
    } );
    edges_.erase( std::unique( edges_.begin(), edges_.end(), []( const Edge& a, const Edge& b )
    {
        return a.from == b.from && a.to == b.to && a.type == b.type && a.evidence == b.evidence;
    } ), edges_.end() );
    finalized_ = true;
}

const Node* Graph::find( std::string_view key ) const noexcept
{
    auto it = byKey_.find( std::string( key ) );
    return it == byKey_.end() ? nullptr : &nodes_[it->second];
}

std::vector<const Edge*> Graph::outgoing( NodeId id ) const
{
    std::vector<const Edge*> out;
    for( const auto& edge : edges_ ) if( edge.from == id ) out.push_back( &edge );
    return out;
}

std::vector<const Edge*> Graph::incoming( NodeId id ) const
{
    std::vector<const Edge*> out;
    for( const auto& edge : edges_ ) if( edge.to == id ) out.push_back( &edge );
    return out;
}

std::string Graph::toJson() const
{
    std::string out = "{\"schema\":1,\"nodes\":[";
    for( std::size_t i = 0; i < nodes_.size(); ++i )
    {
        if( i ) out.push_back( ',' );
        const Node& n = nodes_[i];
        out += "{\"id\":" + std::to_string( n.id ) + ",\"type\":" + jsonString( nodeTypeName( n.type ) )
            + ",\"key\":" + jsonString( n.key );
        if( !n.label.empty() ) out += ",\"label\":" + jsonString( n.label );
        if( !n.evidence.empty() ) out += ",\"evidence\":" + jsonString( n.evidence );
        out += '}';
    }
    out += "],\"edges\":[";
    for( std::size_t i = 0; i < edges_.size(); ++i )
    {
        if( i ) out.push_back( ',' );
        const Edge& e = edges_[i];
        out += "{\"from\":" + std::to_string( e.from ) + ",\"to\":" + std::to_string( e.to )
            + ",\"type\":" + jsonString( edgeTypeName( e.type ) );
        if( !e.evidence.empty() ) out += ",\"evidence\":" + jsonString( e.evidence );
        out += '}';
    }
    out += "]}";
    return out;
}

} // namespace rw::causal
