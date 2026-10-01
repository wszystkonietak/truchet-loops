#pragma once
#include <algorithm>
#include <array>
#include <iostream>
#include <optional>
#include <ranges>
#include <span>
#include <unordered_map>
#include <vector>

#include "HexagonTopology.hpp"
#include "TopologyUtils.hpp"
#include "jagged_span.hpp"

class DodecagonTopology {
 public:
  struct LayoutPolicy {
    int dodecagon_count;
    int square_count;

    constexpr std::pair<size_t, size_t> operator()(size_t tile_id) const {
      if (tile_id < static_cast<size_t>(dodecagon_count)) {
        return {tile_id * 12, 12};
      }

      size_t square_offset = dodecagon_count;
      if (tile_id < square_offset + square_count) {
        size_t base_idx = dodecagon_count * 12;
        return {base_idx + (tile_id - square_offset) * 4, 4};
      }

      size_t hex_offset = square_offset + square_count;
      size_t base_idx = (dodecagon_count * 12) + (square_count * 4);
      return {base_idx + (tile_id - hex_offset) * 6, 6};
    }
  };

  explicit DodecagonTopology(int width, int height)
      : width_(width),
        height_(height),
        base_(width, height),
        total_dodecagons_(base_.GetTotalTiles()),
        total_squares_(base_.GetTotalEdges()),
        total_hexagons_(base_.GetTotalVertices()),
        flat_tile_edges_((total_dodecagons_ * 12) + (total_squares_ * 4) +
                             (total_hexagons_ * 6),
                         -1),
        tile_edges_view_(flat_tile_edges_.data(),
                         LayoutPolicy{total_dodecagons_, total_squares_}) {
    PrecomputeTiles();
  }

  constexpr LayoutPolicy GetLayoutPolicy() const {
    return LayoutPolicy{total_dodecagons_, total_squares_};
  }

  int GetTotalEdges() const { return static_cast<int>(edge_ownership_.size()); }
  int GetTotalTiles() const {
    return total_dodecagons_ + total_squares_ + total_hexagons_;
  }

  int GetEdgeCountForTile(TileId tile) const {
    if (tile < total_dodecagons_) return 12;
    if (tile < total_dodecagons_ + total_squares_) return 4;
    return 6;
  }

  std::span<const EdgeId> GetEdgesForTile(TileId tile) const {
    return tile_edges_view_.subspan(tile);
  }

  EdgeOwnership GetTilesForEdge(EdgeId edge) const {
    return edge_ownership_[edge];
  }
  int GetTotalFlatEdges() const {
    return (total_dodecagons_ * 12) + (total_squares_ * 4) +
           (total_hexagons_ * 6);
  }

 private:
  int width_;
  int height_;
  HexTopology base_;

  int total_dodecagons_;
  int total_squares_;
  int total_hexagons_;

  std::vector<EdgeId> flat_tile_edges_;
  jagged_span<EdgeId, LayoutPolicy> tile_edges_view_;
  std::vector<EdgeOwnership> edge_ownership_;

  int offset_ds_;
  int offset_dh_;
  int offset_sh_;
  int next_boundary_id_;

  int HexagonSquareId(VertexId vid, EdgeId be_id) const {
    auto ownership = base_.GetTilesForEdge(be_id);
    TileId t = ownership.tiles[0];

    int local_idx = ownership.terminals[0];
    auto verts = base_.GetVerticesForTile(t);

    VertexId v1 = verts[local_idx];
    VertexId v2 = verts[(local_idx + 1) % 6];

    if (v1 == vid) return offset_sh_ + be_id * 2;
    if (v2 == vid) return offset_sh_ + be_id * 2 + 1;
    return -1;
  }

  int HexagonDodecagonId(VertexId vid, TileId tid) const {
    auto tiles = base_.GetTilesForVertex(vid);
    auto it = std::ranges::find(tiles, tid);
    if (it != tiles.end()) {
      return offset_dh_ + vid * 3 + std::distance(tiles.begin(), it);
    }
    return -1;
  }

  int DodecagonSquareId(TileId tid, EdgeId be_id) const {
    auto ownership = base_.GetTilesForEdge(be_id);
    if (ownership.tiles[0] == tid) return offset_ds_ + be_id * 2;
    if (ownership.tiles[1] == tid) return offset_ds_ + be_id * 2 + 1;
    return -1;
  }

  void PrecomputeTiles() {
    offset_ds_ = 0;
    offset_dh_ = offset_ds_ + (total_squares_ * 2);
    offset_sh_ = offset_dh_ + (total_hexagons_ * 3);
    next_boundary_id_ = offset_sh_ + (total_squares_ * 2);

    int square_offset = total_dodecagons_;
    int hexagon_offset = total_dodecagons_ + total_squares_;

    std::vector<bool> visited_square(total_squares_, false);

    for (TileId t : std::views::iota(0, total_dodecagons_)) {
      auto base_edges = base_.GetEdgesForTile(t);
      auto verts = base_.GetVerticesForTile(t);

      auto dod_edges = tile_edges_view_.subspan(t);

      for (int i : std::views::iota(0, 6)) {
        EdgeId be_prev = base_edges[(i + 5) % 6];
        VertexId v1 = verts[i];
        EdgeId be_curr = base_edges[i];

        EdgeId sq_prev = HexagonSquareId(v1, be_prev);
        EdgeId dod_edge = HexagonDodecagonId(v1, t);
        EdgeId sq_curr = HexagonSquareId(v1, be_curr);

        // dodecagon
        dod_edges[i * 2] = dod_edge;
        dod_edges[i * 2 + 1] = DodecagonSquareId(t, be_curr);

        // hexagon
        auto hex = tile_edges_view_.subspan(hexagon_offset + v1);

        if (hex[0] == -1 && hex[5] == -1) {
          hex[5] = sq_curr;
          hex[0] = dod_edge;
          hex[1] = sq_prev;
        } else if (hex[1] == sq_curr) {
          hex[2] = dod_edge;
          hex[3] = sq_prev;
        } else if (hex[5] == sq_prev) {
          hex[4] = dod_edge;
          hex[3] = sq_curr;
        }

        // square
        if (!visited_square[be_curr]) {
          visited_square[be_curr] = true;

          TileId square_id = square_offset + be_curr;
          auto sq_edges = tile_edges_view_.subspan(square_id);

          sq_edges[0] = HexagonSquareId(verts[(i + 1) % 6], be_curr);
          sq_edges[1] = DodecagonSquareId(t, be_curr);
          sq_edges[2] = HexagonSquareId(v1, be_curr);

          auto ownership = base_.GetTilesForEdge(be_curr);
          if (ownership.tiles[1] != -1) {
            sq_edges[3] = DodecagonSquareId(ownership.tiles[1], be_curr);
          } else {
            sq_edges[3] = next_boundary_id_++;
          }
        }
      }
    }

    for (int hex_id : std::views::iota(0, total_hexagons_)) {
      auto hex = tile_edges_view_.subspan(hexagon_offset + hex_id);
      for (auto& terminal : hex) {
        if (terminal == -1) terminal = next_boundary_id_++;
      }
    }

    edge_ownership_ = ComputeEdgeOwnership(tile_edges_view_, next_boundary_id_,
                                           GetTotalTiles());
  }
};