#pragma once

#define GL_SILENCE_DEPRECATION
#include <GLFW/glfw3.h>
#include <OpenGL/gl3.h>

#include <cstdint>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

inline std::string ReadFile(std::string_view path) {
  std::ifstream file(path.data());
  std::stringstream buffer;
  buffer << file.rdbuf();
  return buffer.str();
}

inline GLuint CompileShader(GLenum type, std::string_view source) {
  GLuint shader = glCreateShader(type);
  const char* src = source.data();
  glShaderSource(shader, 1, &src, nullptr);
  glCompileShader(shader);

  int success;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
  if (!success) {
    char info_log[512];
    glGetShaderInfoLog(shader, 512, nullptr, info_log);
    std::cerr << "Shader Compilation Error:\n" << info_log << "\n";
  }
  return shader;
}

template <GridTopology Topology, typename ShaderDataView>
std::vector<uint32_t> PackData(const ShaderDataView& shader_data_view,
                               const Topology& topology) {
  int total_flat_edges = topology.GetTotalFlatEdges();

  int total_uints = (total_flat_edges + 3) / 4;
  int padded_uints = (total_uints + 3) & ~3;
  int flat_idx = 0;
  int total_tiles = topology.GetTotalTiles();

  std::vector<uint32_t> ubo_data(padded_uints, 0xFFFFFFFF);

  for (int tile : std::views::iota(0, total_tiles)) {
    for (const auto& terminal_data : shader_data_view.subspan(tile)) {
      if (terminal_data.target != -1) {
        uint32_t packed_val =
            static_cast<uint32_t>(terminal_data.target & 0xF) |
            (terminal_data.flip ? 0x10 : 0x00) |
            ((static_cast<uint32_t>(terminal_data.loop_id) & 0x7) << 5);

        int uint_idx = flat_idx / 4;
        int bit_shift = (flat_idx % 4) * 8;

        ubo_data[uint_idx] &= ~(0xFFU << bit_shift);
        ubo_data[uint_idx] |= (packed_val << bit_shift);
      }

      flat_idx++;
    }
  }

  return ubo_data;
}

inline GLFWwindow* InitializeWindow(int width, int height,
                                    std::string_view title) {
  if (!glfwInit()) {
    return nullptr;
  }

  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);

  GLFWwindow* window =
      glfwCreateWindow(width, height, title.data(), nullptr, nullptr);
  if (!window) {
    glfwTerminate();
    return nullptr;
  }
  glfwMakeContextCurrent(window);
  return window;
}

inline GLuint CreateShaderProgram(std::string_view shader_dir) {
  std::string vert_source = ReadFile(std::string(shader_dir) + "/shader.vert");
  std::string frag_source = ReadFile(std::string(shader_dir) + "/shader.frag");

  GLuint vertex_shader = CompileShader(GL_VERTEX_SHADER, vert_source);
  GLuint fragment_shader = CompileShader(GL_FRAGMENT_SHADER, frag_source);

  GLuint shader_program = glCreateProgram();
  glAttachShader(shader_program, vertex_shader);
  glAttachShader(shader_program, fragment_shader);
  glLinkProgram(shader_program);

  glDeleteShader(vertex_shader);
  glDeleteShader(fragment_shader);

  return shader_program;
}

inline GLuint CreateAndBindUbo(const std::vector<uint32_t>& ubo_data,
                               GLuint shader_program) {
  GLuint ubo;
  glGenBuffers(1, &ubo);
  glBindBuffer(GL_UNIFORM_BUFFER, ubo);
  glBufferData(GL_UNIFORM_BUFFER, ubo_data.size() * sizeof(uint32_t),
               ubo_data.data(), GL_STATIC_DRAW);

  glBindBufferBase(GL_UNIFORM_BUFFER, 0, ubo);
  GLuint block_index = glGetUniformBlockIndex(shader_program, "RoutingData");
  glUniformBlockBinding(shader_program, block_index, 0);

  return ubo;
}

inline GLuint CreateVao() {
  GLuint vao;
  glGenVertexArrays(1, &vao);
  glBindVertexArray(vao);
  return vao;
}
