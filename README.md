# Truchet Tiling Path Generator

A real-time procedural Truchet path generator and renderer across finite 2D periodic tilings: Hexagonal (6³), Octagonal–Square (4.8²), and Dodecagonal–Hexagonal–Square (4.6.12).

The CPU connects edge terminals within each tile and merges disjoint loops into a single continuous path using Disjoint-Set Union (DSU). The scene renders as a full-screen quad where the fragment shader maps pixel coordinates to tile IDs and calculates arc distances to color each pixel.

## Gallery

| Topology | Non-Overlapping · Single Loop | Non-Overlapping · Multi-Loop | Overlapping · Single Loop | Overlapping · Multi-Loop |
| :--- | :---: | :---: | :---: | :---: |
| **Dodecagonal**<br>(4.6.12) | <img src="images/dodecagon_single_non_overlap.png" width="100%" /> | <img src="images/dodecagon_multi_non_overlap.png" width="100%" /> | <img src="images/dodecagon_single_overlap.png" width="100%" /> | <img src="images/dodecagon_multi_overlap.png" width="100%" /> |
| **Octagonal**<br>(4.8²) | <img src="images/octagon_single_non_overlap.png" width="100%" /> | <img src="images/octagon_multi_non_overlap.png" width="100%" /> | <img src="images/octagon_single_overlap.png" width="100%" /> | <img src="images/octagon_multi_overlap.png" width="100%" /> |
| **Hexagonal**<br>(6³) | <img src="images/hexagon_single_non_overlap.png" width="100%" /> | <img src="images/hexagon_multi_non_overlap.png" width="100%" /> | <img src="images/hexagon_single_overlap.png" width="100%" /> | <img src="images/hexagon_multi_overlap.png" width="100%" /> |

## Implementation Details

### Flat Storage & Layout Policies
Grids contain tiles with different numbers of edges.

All terminal states are stored in a single 1D buffer. A `jagged_span` view provides 2D indexing using a `LayoutPolicy` that calculates `{offset, size}` at runtime without storing offset arrays.

### Path Generation & Loop Merging
Each tile edge acts as a terminal:
* **Terminal Pairing**: For non-overlapping paths, terminals connect only to neighboring edges so paths do not cross. For overlapping paths, terminals connect to random edges across the tile.
* **Loop Merging**: Disjoint-Set Union (DSU) tracks connected paths. To create a single continuous loop, adjacent terminals from different sets are swapped to merge their loops together.

### GPU Rendering
Routing data is packed on the CPU and sent to a `std140` Uniform Buffer Object (UBO):
* Each terminal packs its partner index, arc side bit, and loop color ID. Four terminals fit into one `uint32_t`.
* The fragment shader maps UV coordinates to tile IDs and unpacks the connections.
* Arcs and lines are rendered per pixel using an analytical distance function (`ArcDistanceUV`).

## Controls

| Key | Action |
| :--- | :--- |
| `1`, `2`, `3` | Switch grid topology (1: Dodecagonal, 2: Octagonal, 3: Hexagonal) |
| `O` | Toggle overlapping paths |
| `L` | Toggle single continuous loop mode |
| `Arrow Keys` | Adjust grid width and height |

## Dependencies

* C++23 compiler (GCC 13+, Clang 16+, MSVC 2022+)
* CMake (>= 3.16)
* OpenGL 4.1 Core Profile
* GLFW3, GLM

## References

1. **[Diamond Octagon Truchet Pattern](https://www.shadertoy.com/view/wdBSRm)** by Shane.
2. **[4.6.12 Truchet Weave](https://www.shadertoy.com/view/WsyXWR)** by Shane.