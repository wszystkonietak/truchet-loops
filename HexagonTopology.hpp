#pragma once
#include <span>
#include <vector>

#include "TopologyUtils.hpp"
#include "jagged_span.hpp"

class HexTopology {
 public:
  struct LayoutPolicy {
    constexpr std::pair<size_t, size_t> operator()(size_t tile_id) const {
      return {tile_id * 6, 6};
    }
  };

  struct VertexLayoutPolicy {
    constexpr std::pair<size_t, size_t> operator()(size_t vertex_id) const {
      return {vertex_id * 3, 3};
    }
  };

  explicit HexTopology(int width, int height)
      : width_(width),
        height_(height),
        total_edges_(GetHorizontalRowStart(height) + 2 * width),
        total_vertices_(GetVertexRowStart(height) + 2 * width + 1),
        flat_tile_edges_(width * height * 6, -1),
        tile_edges_view_(flat_tile_edges_.data(), LayoutPolicy{}),
        flat_tile_vertices_(width * height * 6, -1),
        tile_vertices_view_(flat_tile_vertices_.data(), LayoutPolicy{}),
        flat_vertex_ownership_(total_vertices_ * 3, -1),
        vertex_ownership_view_(flat_vertex_ownership_.data(),
                               VertexLayoutPolicy{}) {
    PrecomputeTileLookups();

    edge_ownership_ =
        ComputeEdgeOwnership(tile_edges_view_, total_edges_, GetTotalTiles());
    ComputeVertexOwnership(tile_vertices_view_, GetTotalTiles(),
                           vertex_ownership_view_);
  }

  constexpr LayoutPolicy GetLayoutPolicy() const { return LayoutPolicy{}; }

  int GetTotalEdges() const { return total_edges_; }
  int GetTotalTiles() const { return width_ * height_; }
  int GetEdgeCountForTile(TileId tile) const { return 6; }

  std::span<const EdgeId> GetEdgesForTile(TileId tile) const {
    return tile_edges_view_.subspan(tile);
  }

  EdgeOwnership GetTilesForEdge(EdgeId edge) const {
    return edge_ownership_[edge];
  }

  int GetTotalVertices() const { return total_vertices_; }
  int GetVertexCountForTile(TileId tile) const { return 6; }

  std::span<const VertexId> GetVerticesForTile(TileId tile) const {
    return tile_vertices_view_.subspan(tile);
  }

  std::span<const TileId> GetTilesForVertex(VertexId vertex) const {
    if (vertex < 0 || vertex >= total_vertices_) {
      return {};
    }
    return vertex_ownership_view_.subspan(vertex);
  }

  int GetTotalFlatEdges() const { return width_ * height_ * 6; }

 private:
  int width_;
  int height_;
  int total_edges_;
  int total_vertices_;

  std::vector<EdgeId> flat_tile_edges_;
  jagged_span<EdgeId, LayoutPolicy> tile_edges_view_;
  std::vector<EdgeOwnership> edge_ownership_;

  std::vector<VertexId> flat_tile_vertices_;
  jagged_span<VertexId, LayoutPolicy> tile_vertices_view_;

  std::vector<TileId> flat_vertex_ownership_;
  jagged_span<TileId, VertexLayoutPolicy> vertex_ownership_view_;

  int GetHorizontalRowStart(int y) const {
    if (y == 0) return 0;
    int count = 2 * width_ + (width_ + 1);
    if (y > 1) count += (y - 1) * ((2 * width_ + 1) + (width_ + 1));
    return count;
  }

  int GetVerticalRowStart(int y) const {
    int start = GetHorizontalRowStart(y);
    int horiz_size = (y == 0 || y == height_) ? (2 * width_) : (2 * width_ + 1);
    return start + horiz_size;
  }

  int GetVertexRowStart(int vy) const {
    if (vy == 0) return 0;
    int count = 2 * width_ + 1;
    if (vy > 1) count += (vy - 1) * (2 * width_ + 2);
    return count;
  }

  int GetVertexId(int vx, int vy) const {
    int start = GetVertexRowStart(vy);
    if (vy == height_ && height_ % 2 == 0) return start + vx - 1;
    return start + vx;
  }

  void PrecomputeTileLookups() {
    int total_tiles = GetTotalTiles();

    for (int tile : std::views::iota(0, total_tiles)) {
      int x = tile % width_;
      int y = tile / width_;

      int h_top = GetHorizontalRowStart(y);
      int v_row = GetVerticalRowStart(y);
      int h_bot = GetHorizontalRowStart(y + 1);

      int nw_e, ne_e, e_e, se_e, sw_e, w_e;
      w_e = v_row + x;
      e_e = v_row + x + 1;

      if (y % 2 == 0) {
        nw_e = h_top + 2 * x;
        ne_e = h_top + 2 * x + 1;
        sw_e = h_bot + 2 * x;
        se_e = h_bot + 2 * x + 1;
      } else {
        nw_e = h_top + 2 * x + 1;
        ne_e = h_top + 2 * x + 2;
        if (y == height_ - 1) {
          sw_e = h_bot + 2 * x;
          se_e = h_bot + 2 * x + 1;
        } else {
          sw_e = h_bot + 2 * x + 1;
          se_e = h_bot + 2 * x + 2;
        }
      }
      auto edges = tile_edges_view_.subspan(tile);

      edges[0] = nw_e;
      edges[1] = ne_e;
      edges[2] = e_e;
      edges[3] = se_e;
      edges[4] = sw_e;
      edges[5] = w_e;

      int nw_x, n_x, ne_x, sw_x, s_x, se_x;
      int top_y = y;
      int bot_y = y + 1;

      if (y % 2 == 0) {
        nw_x = 2 * x;
        n_x = 2 * x + 1;
        ne_x = 2 * x + 2;
        sw_x = 2 * x;
        s_x = 2 * x + 1;
        se_x = 2 * x + 2;
      } else {
        nw_x = 2 * x + 1;
        n_x = 2 * x + 2;
        ne_x = 2 * x + 3;
        sw_x = 2 * x + 1;
        s_x = 2 * x + 2;
        se_x = 2 * x + 3;
      }
      auto verts = tile_vertices_view_.subspan(tile);
      verts[0] = GetVertexId(nw_x, top_y);
      verts[1] = GetVertexId(n_x, top_y);
      verts[2] = GetVertexId(ne_x, top_y);
      verts[3] = GetVertexId(se_x, bot_y);
      verts[4] = GetVertexId(s_x, bot_y);
      verts[5] = GetVertexId(sw_x, bot_y);
    }
  }
};