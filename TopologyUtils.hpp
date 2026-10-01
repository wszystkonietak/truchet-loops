#pragma once
#include <numeric>
#include <optional>
#include <vector>

#include "TruchetPathGenerator.hpp"

template <typename T>
struct ShaderMappingConfig;

struct RouteData {
  int target = -1;
  bool flip = false;
  int loop_id = -1;
};
using TileId = int;
using VertexId = int;
using EdgeId = int;
using TileEdge = std::pair<TileId, int>;

template <GridTopology Topology>
constexpr auto EnumerateTileEdges(const Topology& topology,
                                  int base_tile_count) {
  return std::views::iota(0, base_tile_count) |
         std::views::transform([&topology](TileId tile_id) {
           return topology.GetEdgesForTile(tile_id) | std::views::enumerate |
                  std::views::transform([tile_id](auto pair) {
                    auto [local_id, edge] = pair;
                    return std::make_tuple(tile_id, static_cast<int>(local_id),
                                           edge);
                  });
         }) |
         std::views::join;
}

template <typename View>
constexpr auto EnumerateTileEdges(const View& view, int tile_count) {
  return std::views::iota(0, tile_count) |
         std::views::transform([&view](TileId tile_id) {
           return view.subspan(tile_id) | std::views::enumerate |
                  std::views::transform([tile_id](auto pair) {
                    auto [local_id, val] = pair;
                    return std::make_tuple(tile_id, static_cast<int>(local_id),
                                           val);
                  });
         }) |
         std::views::join;
}

template <typename TileEdgesView>
std::vector<EdgeOwnership> ComputeEdgeOwnership(const TileEdgesView tile_edges,
                                                int total_edges,
                                                int total_tiles) {
  std::vector<EdgeOwnership> edge_ownership(total_edges);

  for (auto [tile_id, local_id, id] :
       EnumerateTileEdges(tile_edges, total_tiles)) {
    int side = (edge_ownership[id].tiles[0] == -1) ? 0 : 1;
    edge_ownership[id].tiles[side] = tile_id;
    edge_ownership[id].terminals[side] = local_id;
  }

  return edge_ownership;
}

template <typename TileVerticesViewIn, typename VertexTileViewOut>
void ComputeVertexOwnership(TileVerticesViewIn tile_vertices_view,
                            int total_tiles,
                            VertexTileViewOut vertex_ownership_view) {
  for (TileId t = 0; t < total_tiles; ++t) {
    auto verts = tile_vertices_view.subspan(t);

    for (VertexId v : verts) {
      auto ownership = vertex_ownership_view.subspan(v);

      auto empty_slot = std::ranges::find(ownership, -1);
      if (empty_slot != ownership.end()) {
        *empty_slot = t;
      }
    }
  }
}

template <GridTopology Topology, typename TilesView>
auto ExtractLoops(const Topology& topology, const TilesView tiles) {
  int total_edges = topology.GetTotalEdges();

  std::vector<EdgeId> flat_edges;
  std::vector<int> offsets;
  flat_edges.reserve(total_edges);
  offsets.push_back(0);

  std::vector<bool> visited(total_edges, false);

  for (EdgeId start_edge : std::views::iota(0, total_edges)) {
    if (visited[start_edge]) continue;

    auto start_own = topology.GetTilesForEdge(start_edge);
    TileId curr_tile = start_own.tiles[0];
    int p_in = start_own.terminals[0];

    if (curr_tile == -1 || tiles[curr_tile, p_in] == -1) continue;
    EdgeId curr = start_edge;

    while (!visited[curr]) {
      visited[curr] = true;
      flat_edges.push_back(curr);

      int p_out = tiles[curr_tile, p_in];

      EdgeId next_edge = topology.GetEdgesForTile(curr_tile)[p_out];

      if (visited[next_edge]) break;

      auto next_own = topology.GetTilesForEdge(next_edge);
      TileId next_tile = -1;
      int next_p_in = -1;

      if (next_own.tiles[0] != -1 && next_own.tiles[0] != curr_tile) {
        next_tile = next_own.tiles[0];
        next_p_in = next_own.terminals[0];
      } else if (next_own.tiles[1] != -1 && next_own.tiles[1] != curr_tile) {
        next_tile = next_own.tiles[1];
        next_p_in = next_own.terminals[1];
      }

      curr = next_edge;

      curr_tile = next_tile;
      p_in = next_p_in;
    }
    offsets.push_back(flat_edges.size());
  }

  return std::make_pair(std::move(offsets), std::move(flat_edges));
}

template <GridTopology Topology>
std::vector<int> PrecomputeTileOffsets(const Topology& topology,
                                       int base_tile_count) {
  int total_tiles = topology.GetTotalTiles();
  std::vector<int> offsets(total_tiles, 0);
  std::vector<bool> visited(total_tiles, false);

  for (TileId t : std::views::iota(0, base_tile_count)) {
    int N = topology.GetEdgeCountForTile(t);
    offsets[t] =
        (ShaderMappingConfig<Topology>::base_tile_rotation % N + N) % N;
    visited[t] = true;
  }

  for (auto [base_id, base_edge_id, edge] :
       EnumerateTileEdges(topology, base_tile_count)) {
    auto own = topology.GetTilesForEdge(edge);

    TileId neighbor = -1;
    int neighbor_edge = -1;

    if (own.tiles[0] == base_id) {
      neighbor = own.tiles[1];
      neighbor_edge = own.terminals[1];
    } else {
      neighbor = own.tiles[0];
      neighbor_edge = own.terminals[0];
    }

    if (neighbor != -1 && !visited[neighbor]) {
      visited[neighbor] = true;
      int N = topology.GetEdgeCountForTile(neighbor);
      int offset_diff = ShaderMappingConfig<
                            Topology>::clockwise_neighbor_edges[base_edge_id] -
                        neighbor_edge;
      offsets[neighbor] = (offset_diff % N + N) % N;
    }
  }
  return offsets;
}

inline int GetVSide(int p_in, int p_out, int N) {
  int diff = (p_out - p_in + N) % N;
  if (diff == N / 2 && N % 2 == 0) return (p_in < p_out) ? 1 : 0;
  return (diff < N / 2) ? 1 : 0;
}

inline bool LoopLocalDirection(int p_in, int p_out, int N, bool is_flipped) {
  return (GetVSide(p_in, p_out, N) == 1) != is_flipped;
}

template <GridTopology Topology, typename ShaderDataView,
          typename TerminalStateView>
void ResolveLoopConflicts(const Topology& topology,
                          ShaderDataView shader_data_view, int num_loops,
                          TerminalStateView terminal_state_view) {
  int total_tiles = topology.GetTotalTiles();

  std::vector<std::vector<std::pair<int, bool>>> adj(num_loops);

  // setup adjacent loops
  for (auto [t, u, curr_terminal] :
       EnumerateTileEdges(terminal_state_view, total_tiles)) {
    int N = terminal_state_view.subspan(t).size();
    int prev_u = (u - 1 + N) % N;
    auto [curr_loop, curr_target, curr_flip] = curr_terminal;
    auto [prev_loop, prev_target, prev_flip] =
        terminal_state_view.subspan(t)[prev_u];

    if (curr_loop == -1 || prev_loop == -1) continue;

    if (curr_loop != prev_loop) {
      bool color_prev_CW =
          LoopLocalDirection(prev_u, prev_target, N, prev_flip);
      bool color_curr_CCW = LoopLocalDirection(curr_target, u, N, curr_flip);
      bool conflict = (color_prev_CW != color_curr_CCW);

      adj[curr_loop].emplace_back(prev_loop, conflict);
      adj[prev_loop].emplace_back(curr_loop, conflict);
    }
  }

  std::vector<int> loop_flip_state(num_loops, -1);
  std::vector<int> stack;
  stack.reserve(num_loops);
  // resolve loops
  for (int i : std::views::iota(0, num_loops)) {
    if (loop_flip_state[i] != -1) continue;

    loop_flip_state[i] = 0;
    stack.push_back(i);

    while (!stack.empty()) {
      int curr = stack.back();
      stack.pop_back();

      for (auto const& [neighbor, needs_flip] : adj[curr]) {
        if (loop_flip_state[neighbor] == -1) {
          loop_flip_state[neighbor] = loop_flip_state[curr] ^ needs_flip;
          stack.push_back(neighbor);
        }
      }
    }
  }

  TileId boundary_tile =
      *std::ranges::find_if(std::views::iota(0, total_tiles), [&](TileId t) {
        int N = topology.GetEdgeCountForTile(t);
        return shader_data_view[t, N - 1].target == -1 &&
               shader_data_view[t, 0].target != -1;
      });
  int N = topology.GetEdgeCountForTile(boundary_tile);
  // make boundary black
  bool background_needs_flip = false;
  for (int u = 0; u < N; ++u) {
    int prev_u = (u - 1 + N) % N;
    auto [curr_loop, curr_target, curr_flip] =
        terminal_state_view[boundary_tile, u];
    auto [prev_loop, prev_target, prev_flip] =
        terminal_state_view[boundary_tile, prev_u];

    if (curr_loop != -1 && prev_loop == -1) {
      bool planned_flip = curr_flip ^ (loop_flip_state[curr_loop] == 1);
      background_needs_flip =
          LoopLocalDirection(curr_target, u, N, planned_flip);
      break;
    }
  }
  if (background_needs_flip) {
    std::ranges::for_each(loop_flip_state, [](int& state) {
      if (state != -1) state ^= 1;
    });
  }

  std::vector<int> color_dsu(num_loops);
  std::iota(color_dsu.begin(), color_dsu.end(), 0);

  auto find_set = [&](int i, auto& self) -> int {
    if (color_dsu[i] == i) return i;
    return color_dsu[i] = self(color_dsu[i], self);
  };

  auto union_sets = [&](int i, int j) {
    int root_i = find_set(i, find_set);
    int root_j = find_set(j, find_set);
    if (root_i != root_j) color_dsu[root_i] = root_j;
  };

  // flip inner loops
  for (auto [t, u, curr_terminal] :
       EnumerateTileEdges(terminal_state_view, total_tiles)) {
    int N = terminal_state_view.subspan(t).size();
    int prev_u = (u - 1 + N) % N;
    auto [curr_loop, curr_target, curr_flip] = curr_terminal;
    auto [prev_loop, prev_target, prev_flip] =
        terminal_state_view.subspan(t)[prev_u];

    if (curr_loop != -1 && prev_loop != -1) {
      bool planned_flip = curr_flip ^ (loop_flip_state[curr_loop] == 1);
      bool is_colored_region =
          LoopLocalDirection(curr_target, u, N, planned_flip);

      if (is_colored_region) {
        union_sets(curr_loop, prev_loop);
      }
    }
  }

  // apply flips
  for (TileId t : std::views::iota(0, total_tiles)) {
    for (auto pair_chunk : shader_data_view.subspan(t) | std::views::chunk(2)) {
      auto& p1 = pair_chunk[0];
      auto& p2 = pair_chunk[1];

      if (p1.target != -1) {
        auto [loop_id, target, flip] = terminal_state_view[t, p1.target];
        if (loop_id != -1) {
          if (loop_flip_state[loop_id] == 1) {
            p1.flip = !p1.flip;
            p2.flip = !p2.flip;
          }
          int grouped_id = find_set(loop_id, find_set);
          p1.loop_id = grouped_id;
          p2.loop_id = grouped_id;
        }
      }
    }
  }
}

template <GridTopology Topology, typename TilesView, typename ShaderDataView>
void MapTilesForShader(const Topology& topology, const TilesView tiles_view,
                       int base_tile_count,
                       const ShaderDataView shader_data_view) {
  std::vector<int> offsets = PrecomputeTileOffsets(topology, base_tile_count);
  std::vector<int> pair_counts(topology.GetTotalTiles(), 0);

  auto [loop_offsets, loop_edges] = ExtractLoops(topology, tiles_view);
  int num_loops = std::max(0, static_cast<int>(loop_offsets.size()) - 1);

  std::vector<std::tuple<int, int, bool>> flat_terminal_state(
      topology.GetTotalFlatEdges(), {-1, -1, false});
  jagged_span terminal_state_view(flat_terminal_state.data(),
                                  topology.GetLayoutPolicy());

  for (int loop_id : std::views::iota(0, num_loops)) {
    int start = loop_offsets[loop_id];
    int loop_length = loop_offsets[loop_id + 1] - start;

    if (loop_length == 0) continue;

    auto current_loop = std::span(loop_edges).subspan(start, loop_length);
    bool is_flipped = false;

    for (int i : std::views::iota(0, loop_length)) {
      EdgeId curr_edge = current_loop[i];
      EdgeId next_edge = current_loop[(i + 1) % loop_length];

      auto own = topology.GetTilesForEdge(curr_edge);
      auto [tile_a, tile_b] = own.tiles;
      auto [terminal_a_raw, terminal_b_raw] = own.terminals;

      auto get_vis = [&](TileId t, int p_raw) {
        int count = topology.GetEdgeCountForTile(t);
        return std::make_pair((p_raw + offsets[t]) % count,
                              (tiles_view[t, p_raw] + offsets[t]) % count);
      };

      auto [terminal_a_vis, target_a_vis] = get_vis(tile_a, terminal_a_raw);
      auto [terminal_b_vis, target_b_vis] = get_vis(tile_b, terminal_b_raw);

      int edges_a = topology.GetEdgeCountForTile(tile_a);
      int edges_b = topology.GetEdgeCountForTile(tile_b);

      if (GetVSide(target_a_vis, terminal_a_vis, edges_a) !=
          GetVSide(terminal_b_vis, target_b_vis, edges_b)) {
        is_flipped = !is_flipped;
      }

      bool is_side_a =
          (tile_a != -1 && tiles_view[tile_a, terminal_a_raw] != -1 &&
           topology.GetEdgesForTile(
               tile_a)[tiles_view[tile_a, terminal_a_raw]] == next_edge);

      TileId active_tile = is_side_a ? tile_a : tile_b;
      int terminal_in_raw = is_side_a ? terminal_a_raw : terminal_b_raw;
      int active_edges = topology.GetEdgeCountForTile(active_tile);

      int terminal_in_vis =
          (terminal_in_raw + offsets[active_tile]) % active_edges;
      int terminal_out_vis =
          (tiles_view[active_tile, terminal_in_raw] + offsets[active_tile]) %
          active_edges;

      int p_idx = pair_counts[active_tile];

      shader_data_view[active_tile, p_idx] = {terminal_in_vis, is_flipped,
                                              loop_id};
      shader_data_view[active_tile, p_idx + 1] = {terminal_out_vis, is_flipped,
                                                  loop_id};

      terminal_state_view[active_tile, terminal_in_vis] = {
          loop_id, terminal_out_vis, is_flipped};
      terminal_state_view[active_tile, terminal_out_vis] = {
          loop_id, terminal_in_vis, is_flipped};
      pair_counts[active_tile] += 2;
    }
  }

  ResolveLoopConflicts(topology, shader_data_view, num_loops,
                       terminal_state_view);
}