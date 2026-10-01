#pragma once
#include <span>
#include <vector>

#include "TopologyUtils.hpp"
#include "jagged_span.hpp"

class OctagonTopology {
 public:
  struct LayoutPolicy {
    int octagon_count;

    constexpr std::pair<size_t, size_t> operator()(size_t tile_id) const {
      if (tile_id < static_cast<size_t>(octagon_count)) {
        return {tile_id * 8, 8};
      } else {
        size_t base_offset = octagon_count * 8;
        return {base_offset + (tile_id - octagon_count) * 4, 4};
      }
    }
  };

  explicit OctagonTopology(int width, int height)
      : width_(width),
        height_(height),
        octagon_count_(width * height),
        square_count_((width - 1) * (height - 1)),
        flat_tile_edges_(octagon_count_ * 8 + square_count_ * 4, -1),
        tile_edges_view_(flat_tile_edges_.data(),
                         LayoutPolicy{octagon_count_}) {
    PrecomputeTileLookups();
    edge_ownership_ =
        ComputeEdgeOwnership(tile_edges_view_, edge_count_, GetTotalTiles());
  }

  constexpr LayoutPolicy GetLayoutPolicy() const {
    return LayoutPolicy{octagon_count_};
  }

  int GetTotalEdges() const { return static_cast<int>(edge_count_); }
  int GetTotalTiles() const { return octagon_count_ + square_count_; }

  int GetEdgeCountForTile(TileId tile) const {
    return (tile < octagon_count_) ? 8 : 4;
  }

  std::span<const EdgeId> GetEdgesForTile(TileId tile) const {
    return tile_edges_view_.subspan(tile);
  }

  EdgeOwnership GetTilesForEdge(EdgeId edge) const {
    return edge_ownership_[edge];
  }

  int GetTotalFlatEdges() const {
    return (octagon_count_ * 8) + (square_count_ * 4);
  }

 private:
  int width_;
  int height_;
  int octagon_count_;
  int square_count_;
  int edge_count_;

  std::vector<EdgeId> flat_tile_edges_;
  jagged_span<EdgeId, LayoutPolicy> tile_edges_view_;
  std::vector<EdgeOwnership> edge_ownership_;

  void PrecomputeTileLookups() {
    int row_edges_count = (1 + height_) * width_;
    int col_edges_count = height_ * (1 + width_);
    int square_edges_offset = row_edges_count + col_edges_count;
    int square_edges_count = octagon_count_ * 4;
    edge_count_ = row_edges_count + col_edges_count + square_edges_count;

    for (int octagon_id : std::views::iota(0, octagon_count_)) {
      int y = octagon_id / width_;
      int x = octagon_id % width_;

      int n_id = octagon_id;
      int s_id = (y + 1) * width_ + x;
      int w_id = row_edges_count + x * height_ + y;
      int e_id = row_edges_count + (x + 1) * height_ + y;

      int d_offset = square_edges_offset + 4 * octagon_id;
      int nw_id = d_offset + 0;
      int ne_id = d_offset + 1;
      int se_id = d_offset + 2;
      int sw_id = d_offset + 3;

      auto edges = tile_edges_view_.subspan(octagon_id);

      edges[0] = n_id;
      edges[1] = ne_id;
      edges[2] = e_id;
      edges[3] = se_id;
      edges[4] = s_id;
      edges[5] = sw_id;
      edges[6] = w_id;
      edges[7] = nw_id;
    }

    if (width_ == 1 || height_ == 1) return;

    for (int i : std::views::iota(0, square_count_)) {
      int y = i / (width_ - 1);
      int x = i % (width_ - 1);

      int nw_id = square_edges_offset + 4 * (y * width_ + x) + 2;
      int ne_id = square_edges_offset + 4 * (y * width_ + (x + 1)) + 3;
      int se_id = square_edges_offset + 4 * ((y + 1) * width_ + (x + 1)) + 0;
      int sw_id = square_edges_offset + 4 * ((y + 1) * width_ + x) + 1;

      int square_id = octagon_count_ + i;
      auto edges = tile_edges_view_.subspan(square_id);
      edges[0] = nw_id;
      edges[1] = ne_id;
      edges[2] = se_id;
      edges[3] = sw_id;
    }
  }
};