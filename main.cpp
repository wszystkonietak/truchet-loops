#include <algorithm>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#include "DodecagonTopology.hpp"
#include "HexagonTopology.hpp"
#include "OctagonTopology.hpp"
#include "RenderUtils.hpp"
#include "TruchetPathGenerator.hpp"

struct AppState {
  int grid_type = 0;
  bool allow_overlaps = false;
  bool one_loop = true;
  int width = 8;
  int height = 8;
  bool needs_rebuild = true;
} appState;

void key_callback(GLFWwindow* window, int key, int scancode, int action,
                  int mods) {
  if (action == GLFW_PRESS) {
    if (key == GLFW_KEY_1) {
      appState.grid_type = 0;
      appState.needs_rebuild = true;
    }
    if (key == GLFW_KEY_2) {
      appState.grid_type = 1;
      appState.needs_rebuild = true;
    }
    if (key == GLFW_KEY_3) {
      appState.grid_type = 2;
      appState.needs_rebuild = true;
    }

    if (key == GLFW_KEY_O) {
      appState.allow_overlaps = !appState.allow_overlaps;
      appState.needs_rebuild = true;
    }
    if (key == GLFW_KEY_L) {
      appState.one_loop = !appState.one_loop;
      appState.needs_rebuild = true;
    }
  }

  if (action == GLFW_PRESS || action == GLFW_REPEAT) {
    if (key == GLFW_KEY_UP) {
      appState.height++;
      appState.needs_rebuild = true;
    }
    if (key == GLFW_KEY_DOWN) {
      appState.height = std::max(1, appState.height - 1);
      appState.needs_rebuild = true;
    }
    if (key == GLFW_KEY_RIGHT) {
      appState.width++;
      appState.needs_rebuild = true;
    }
    if (key == GLFW_KEY_LEFT) {
      appState.width = std::max(1, appState.width - 1);
      appState.needs_rebuild = true;
    }
  }
}

template <>
struct ShaderMappingConfig<DodecagonTopology> {
  static constexpr int base_tile_rotation = 7;
  static constexpr int clockwise_neighbor_edges[] = {0, 0, 1, 0, 2, 2,
                                                     3, 2, 4, 2, 5, 0};
};

template <>
struct ShaderMappingConfig<OctagonTopology> {
  static constexpr int base_tile_rotation = 0;
  static constexpr int clockwise_neighbor_edges[] = {4, 3, 6, 0, 0, 1, 2, 2};
};

template <>
struct ShaderMappingConfig<HexTopology> {
  static constexpr int base_tile_rotation = 4;
  static constexpr int clockwise_neighbor_edges[] = {};
};

template <typename Topology>
void RebuildContext(const Topology& topology, std::mt19937& rng,
                    GLuint shader_program, GLuint& ubo) {
  int total_flat_edges = topology.GetTotalFlatEdges();
  std::vector<int> flat_tiles(total_flat_edges, -1);
  jagged_span tiles_view(flat_tiles.data(), topology.GetLayoutPolicy());

  if (appState.allow_overlaps) {
    TruchetPathGenerator<Topology, true> solver(topology, rng);
    solver.Generate(tiles_view, appState.one_loop);
  } else {
    TruchetPathGenerator<Topology, false> solver(topology, rng);
    solver.Generate(tiles_view, appState.one_loop);
  }

  int base_tile_count = appState.width * appState.height;
  std::vector<RouteData> flat_shader_data(total_flat_edges, {-1, false, -1});
  jagged_span shader_data_view(flat_shader_data.data(),
                               topology.GetLayoutPolicy());

  MapTilesForShader(topology, tiles_view, base_tile_count, shader_data_view);
  std::vector<uint32_t> ubo_data = PackData(shader_data_view, topology);

  if (ubo == 0) {
    glGenBuffers(1, &ubo);
  }

  glBindBuffer(GL_UNIFORM_BUFFER, ubo);
  glBufferData(GL_UNIFORM_BUFFER, ubo_data.size() * sizeof(uint32_t),
               ubo_data.data(), GL_DYNAMIC_DRAW);

  glBindBufferBase(GL_UNIFORM_BUFFER, 0, ubo);
  GLuint block_index = glGetUniformBlockIndex(shader_program, "RoutingData");
  glUniformBlockBinding(shader_program, block_index, 0);
}

int main(int argc, char* argv[]) {
  std::string shader_dir = argc > 1 ? argv[1] : "./";

  std::random_device rd;
  std::mt19937 rng(rd());

  GLFWwindow* window = InitializeWindow(400, 400, "truchet");

  glfwSetKeyCallback(window, key_callback);

  GLuint shader_program = CreateShaderProgram(shader_dir);
  GLuint ubo = 0;
  GLuint vao = CreateVao();

  while (!glfwWindowShouldClose(window)) {
    if (appState.needs_rebuild) {
      if (appState.grid_type == 0) {
        DodecagonTopology topology(appState.width, appState.height);
        RebuildContext(topology, rng, shader_program, ubo);
      } else if (appState.grid_type == 1) {
        OctagonTopology topology(appState.width, appState.height);
        RebuildContext(topology, rng, shader_program, ubo);
      } else if (appState.grid_type == 2) {
        HexTopology topology(appState.width, appState.height);
        RebuildContext(topology, rng, shader_program, ubo);
      }
      appState.needs_rebuild = false;
    }

    int w, h;
    glfwGetFramebufferSize(window, &w, &h);
    glViewport(0, 0, w, h);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(shader_program);

    glUniform2f(glGetUniformLocation(shader_program, "iResolution"), w, h);
    glUniform1f(glGetUniformLocation(shader_program, "iTime"), glfwGetTime());
    glUniform1i(glGetUniformLocation(shader_program, "u_allowOverlaps"),
                appState.allow_overlaps);

    glUniform1i(glGetUniformLocation(shader_program, "u_gridType"),
                appState.grid_type);
    glUniform1i(glGetUniformLocation(shader_program, "W"), appState.width);
    glUniform1i(glGetUniformLocation(shader_program, "H"), appState.height);

    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    glfwSwapBuffers(window);
    glfwPollEvents();
  }

  glDeleteVertexArrays(1, &vao);
  glDeleteProgram(shader_program);
  glDeleteBuffers(1, &ubo);

  glfwTerminate();
  return 0;
}