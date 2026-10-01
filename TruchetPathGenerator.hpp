#pragma once
#include <algorithm>
#include <array>
#include <iostream>
#include <optional>
#include <random>
#include <ranges>
#include <span>
#include <unordered_map>
#include <vector>

#include "TopologyUtils.hpp"
#include "jagged_span.hpp"

using EdgeId = int;
using TileId = int;

struct EdgeOwnership {
  std::array<TileId, 2> tiles = {-1, -1};
  std::array<int, 2> terminals;
};

template <typename T>
concept GridTopology = requires(const T& topology_, TileId tile, EdgeId edge) {
  { topology_.GetTotalEdges() } -> std::same_as<int>;
  { topology_.GetTotalTiles() } -> std::same_as<int>;
  { topology_.GetEdgeCountForTile(tile) } -> std::same_as<int>;
  {
    topology_.GetEdgesForTile(tile)
  } -> std::convertible_to<std::span<const EdgeId>>;
  { topology_.GetTilesForEdge(edge) } -> std::same_as<EdgeOwnership>;
  { topology_.GetLayoutPolicy() };
  { topology_.GetTotalFlatEdges() } -> std::same_as<int>;
};

inline constexpr std::true_type AllowOverlaps{};
inline constexpr std::false_type DisallowOverlaps{};

template <GridTopology Topology, bool kAllowOverlaps = true>
class TruchetPathGenerator {
 public:
  using LayoutPolicyType = decltype(std::declval<Topology>().GetLayoutPolicy());
  using TilesView = jagged_span<int, LayoutPolicyType>;

  explicit TruchetPathGenerator(const Topology& topology, std::mt19937& rng,
                                std::bool_constant<kAllowOverlaps> = {})
      : topology_(topology), rng_(rng) {}

  TruchetPathGenerator(const TruchetPathGenerator&) = delete;
  TruchetPathGenerator& operator=(const TruchetPathGenerator&) = delete;

  void Generate(TilesView tiles_view, bool one_path = true) {
    int total_edges = topology_.GetTotalEdges();

    edge_path_ids_.assign(total_edges, -1);
    PrecomputeMutability();

    PrecomputeFixedRouting(tiles_view);
    GenerateTiles(tiles_view);
    if (one_path) ConnectPaths(tiles_view);
  }

 private:
  std::mt19937& rng_;
  const Topology& topology_;

  std::vector<int> edge_path_ids_;
  std::vector<bool> is_mutable_;

  void PrecomputeMutability() {
    int total_tiles = topology_.GetTotalTiles();
    is_mutable_.assign(total_tiles, true);

    for (TileId t : std::views::iota(0, total_tiles)) {
      auto edges = topology_.GetEdgesForTile(t);

      if (std::ranges::any_of(edges, [&](EdgeId n) {
            return topology_.GetTilesForEdge(n).tiles[1] == -1;
          })) {
        is_mutable_[t] = false;
      }
    }
  }

  void PrecomputeFixedRouting(TilesView tiles_view) {
    int total_edges = topology_.GetTotalEdges();

    for (EdgeId start_edge : std::views::iota(0, total_edges)) {
      auto ownership = topology_.GetTilesForEdge(start_edge);

      bool m0 = ownership.tiles[0] != -1 && is_mutable_[ownership.tiles[0]];
      bool m1 = ownership.tiles[1] != -1 && is_mutable_[ownership.tiles[1]];

      if (m0 != m1) {
        EdgeId curr = start_edge;
        TileId curr_ghost = m0 ? ownership.tiles[1] : ownership.tiles[0];

        bool connect_mode = true;

        do {
          const auto edges = topology_.GetEdgesForTile(curr_ghost);

          auto it = std::ranges::find(edges, curr);
          int idx = static_cast<int>(std::distance(edges.begin(), it));

          int next_idx = (idx - 1 + edges.size()) % edges.size();
          EdgeId next_edge = edges[next_idx];

          if (connect_mode) {
            tiles_view[curr_ghost, idx] = next_idx;
            tiles_view[curr_ghost, next_idx] = idx;
            auto global_edges = topology_.GetEdgesForTile(curr_ghost);
            UnionSets(global_edges[idx], global_edges[next_idx]);
          }

          curr = next_edge;

          auto next_own = topology_.GetTilesForEdge(curr);
          TileId t0 = next_own.tiles[0];
          TileId t1 = next_own.tiles[1];

          bool is_next_mutable =
              (t0 != -1 && is_mutable_[t0]) || (t1 != -1 && is_mutable_[t1]);

          if (is_next_mutable) {
            connect_mode = !connect_mode;
          } else {
            TileId other_ghost = (t0 == curr_ghost) ? t1 : t0;
            if (other_ghost != -1) {
              curr_ghost = other_ghost;
            }
          }

        } while (curr != start_edge);

        return;
      }
    }
  }

  int FindSet(int v) {
    if (edge_path_ids_[v] < 0) return v;
    return edge_path_ids_[v] = FindSet(edge_path_ids_[v]);
  }

  bool UnionSets(int a, int b) {
    a = FindSet(a);
    b = FindSet(b);
    if (a != b) {
      if (edge_path_ids_[a] > edge_path_ids_[b]) {
        std::swap(a, b);
      }
      edge_path_ids_[a] += edge_path_ids_[b];
      edge_path_ids_[b] = a;
      return true;
    }
    return false;
  }

  void GenerateTile(std::span<int> terminals, std::span<int> tile_memory) {
    if (terminals.empty()) return;

    if constexpr (kAllowOverlaps) {
      std::ranges::shuffle(terminals, rng_);
      for (int i = 0; i < terminals.size(); i += 2) {
        tile_memory[terminals[i]] = terminals[i + 1];
        tile_memory[terminals[i + 1]] = terminals[i];
      }
    } else {
      if (terminals.size() == 2) {
        tile_memory[terminals[0]] = terminals[1];
        tile_memory[terminals[1]] = terminals[0];
        return;
      }

      int p1 = terminals[0];
      int num_choices = terminals.size() / 2;
      std::uniform_int_distribution<int> dist(0, num_choices - 1);
      int chosen_idx = 1 + 2 * dist(rng_);

      int p2 = terminals[chosen_idx];
      tile_memory[p1] = p2;
      tile_memory[p2] = p1;

      auto inner_terminals = terminals.subspan(1, chosen_idx - 1);
      auto outer_terminals = terminals.subspan(chosen_idx + 1);

      GenerateTile(inner_terminals, tile_memory);
      GenerateTile(outer_terminals, tile_memory);
    }
  }

  void GenerateTiles(TilesView tiles_view) {
    int total_tiles = topology_.GetTotalTiles();
    std::vector<int> terminals;

    for (TileId tile : std::views::iota(0, total_tiles)) {
      if (!is_mutable_[tile]) continue;

      int edge_count = topology_.GetEdgeCountForTile(tile);
      auto global_edges = topology_.GetEdgesForTile(tile);

      terminals.resize(edge_count);
      std::ranges::iota(terminals, 0);

      auto tile_memory = tiles_view.subspan(tile);

      GenerateTile(terminals, tile_memory);

      for (int u : std::views::iota(0, edge_count)) {
        int v = tiles_view[tile, u];
        if (u < v) {
          UnionSets(global_edges[u], global_edges[v]);
        }
      }
    }
  }

  void ConnectPaths(TilesView tiles_view) {
    int total_tiles = topology_.GetTotalTiles();
    std::vector<int> adjacent_starts;

    for (TileId tile : std::views::iota(0, total_tiles) | std::views::reverse) {
      if (!is_mutable_[tile]) continue;

      int edge_count = topology_.GetEdgeCountForTile(tile);
      auto global_edges = topology_.GetEdgesForTile(tile);

      adjacent_starts.resize(edge_count);
      std::ranges::iota(adjacent_starts, 0);
      std::ranges::shuffle(adjacent_starts, rng_);

      for (int i : std::views::iota(0, edge_count)) {
        int curr_local = adjacent_starts[i];

        int prev_local = -1;
        if constexpr (kAllowOverlaps) {
          prev_local = adjacent_starts[(i + 1) % edge_count];
        } else {
          prev_local = (curr_local + 1) % edge_count;
        }

        int curr_global = global_edges[curr_local];
        int prev_global = global_edges[prev_local];

        if (FindSet(curr_global) != FindSet(prev_global)) {
          int curr_partner = tiles_view[tile, curr_local];
          int prev_partner = tiles_view[tile, prev_local];

          tiles_view[tile, curr_local] = prev_local;
          tiles_view[tile, prev_local] = curr_local;

          tiles_view[tile, curr_partner] = prev_partner;
          tiles_view[tile, prev_partner] = curr_partner;

          UnionSets(curr_global, prev_global);
        }
      }
    }
  }
};