#version 410 core

out vec4 FragColor;
uniform vec2 iResolution;
uniform float iTime;
uniform int u_allowOverlaps;
uniform int u_gridType;
uniform int W;
uniform int H;

layout(std140) uniform RoutingData { uvec4 ROUTING[4096]; };

ivec3 GetRouting(int flat_idx) {
  int vec_idx = flat_idx / 16;
  int comp_idx = (flat_idx / 4) % 4;
  int bit_shift = (flat_idx % 4) * 8;

  uint packed_uint = ROUTING[vec_idx][comp_idx];
  uint val = (packed_uint >> bit_shift) & 255U;

  if (val == 255U) return ivec3(-1, 0, -1);

  int target = int(val & 15U);
  int flip = int((val >> 4U) & 1U);
  int path_id = int((val >> 5U) & 7U);

  return ivec3(target, flip, path_id);
}

const float PI = 3.141592653589;
const float R2 = 1.414213562373;
const float R3 = 1.732050807569;
const float HR3 = R3 / 2.0;
const float IR3 = R3 / 3.0;
const float HIR3 = R3 / 6.0;
const float I3 = 1.0 / 3.0;
const float I6 = 1.0 / 6.0;

const float c = tan(PI * 2. / 6.) / (3. * tan(PI * 2. / 6.) + 3.);
const float e = 1. / 3. - c;
const float f = 1. / (3. * cos(PI * 2. / 6.)) - c;

const vec2 FOLD_VEC1 = vec2(-0.5, HR3);
const vec2 FOLD_VEC2 = vec2(HR3, 0.5);
const vec2 DOD_NORMS[6] =
    vec2[](vec2(1.0, 0.0), vec2(HR3, 0.5), vec2(0.5, HR3), vec2(0.0, 1.0),
           vec2(-0.5, HR3), vec2(-HR3, 0.5));
const vec2 HEX_OFFS[6] =
    vec2[](vec2(IR3, I3), vec2(0.0, I3 * 2.0), vec2(-IR3, I3), vec2(-IR3, -I3),
           vec2(0.0, -I3 * 2.0), vec2(IR3, -I3));
const vec2 SQ_OFFS[6] =
    vec2[](vec2(IR3, 0.0), vec2(HIR3, 0.5), vec2(-HIR3, 0.5), vec2(-IR3, 0.0),
           vec2(-HIR3, -0.5), vec2(HIR3, -0.5));

float w_line;

void solveCircle(vec2 a, vec2 b, out vec2 o, out float r) {
  vec2 m = a + b;
  o = dot(a, a) / dot(m, a) * m;
  r = length(o - a);
}

vec4 ArcDistanceUV(vec2 p, vec2 a, vec2 b) {
  float u, v_dist, arc_length;

  if (abs(a.x * b.y - a.y * b.x) < 1e-5) {
    vec2 ab = b - a;
    arc_length = length(ab);
    u = dot(p - a, ab) / dot(ab, ab);
    vec2 n = normalize(ab);
    v_dist = dot(p - a, vec2(-n.y, n.x));

    return vec4(u, v_dist, arc_length, 0.0);
  } else {
    vec2 o;
    float r_circ;
    solveCircle(a, b, o, r_circ);

    vec2 nm = normalize((a + b) / 2. - o), na = normalize(a - o),
         nb = normalize(b - o), np = normalize(p - o);

    float tha = asin(na.x * nm.y - na.y * nm.x);
    float thb = asin(nb.x * nm.y - nb.y * nm.x);
    float th = asin(np.x * nm.y - np.y * nm.x);

    u = (th - tha) / (thb - tha);
    arc_length = abs(thb - tha) * r_circ;
    v_dist = distance(p, o) - r_circ;

    return vec4(u, v_dist, arc_length, r_circ);
  }
}

vec3 Hsv2rgb(vec3 c) {
  vec4 K = vec4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
  vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
  return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}

vec3 ColorFromID(int id) {
  float hue = fract(float(id) * 0.6180339887);
  float saturation = 0.7;
  float brightness = 0.95;
  return Hsv2rgb(vec3(hue, saturation, brightness));
}

void DrawBackground(float v_dist, float currentDist, int pa_idx, int pb_idx,
                    vec2 pa, vec2 pb, bool flip, vec3 loopColor,
                    inout float closestDist, inout vec3 regionColor,
                    inout float isColored) {
  if (currentDist >= closestDist) return;
  closestDist = currentDist;

  float canonical_v_dist = v_dist;
  if (pa_idx > pb_idx && abs(pa.x * pb.y - pa.y * pb.x) < 1e-5) {
    canonical_v_dist = -v_dist;
  }

  bool isLaneA = (canonical_v_dist < 0.0) != flip;
  vec3 baseColor = isLaneA ? loopColor : vec3(0.0);
  regionColor = baseColor * smoothstep(0.005, 0.02, currentDist);

  float signed_dist = isLaneA ? currentDist : -currentDist;
  isColored = smoothstep(-w_line, w_line, signed_dist);
}

struct TileData {
  int id;
  vec2 local_pos;
  float edge_mask;
  int num_terminals;
  float radius;
  int base_route_idx;
  float angle_offset;
  float angle_mult;
};

int Mod2(int n) { return n & 1; }
int GetHorizontalRowStart(int y) {
  if (y <= 0) return 0;
  int count = 2 * W + (W + 1);
  if (y > 1) count += (y - 1) * ((2 * W + 1) + (W + 1));
  return count;
}
int GetVerticalRowStart(int y) {
  return GetHorizontalRowStart(y) +
         ((y <= 0 || y >= H) ? (2 * W) : (2 * W + 1));
}
int GetVertexRowStart(int vy) {
  if (vy <= 0) return 0;
  int count = 2 * W + 1;
  if (vy > 1) count += (vy - 1) * (2 * W + 2);
  return count;
}
int GetVertexId(int vx, int vy) {
  return (vy >= H && (H % 2 == 0)) ? GetVertexRowStart(vy) + vx - 1
                                   : GetVertexRowStart(vy) + vx;
}
int GetTotalDodecagons() { return W * H; }
int GetTotalSquares() { return GetHorizontalRowStart(H) + 2 * W; }
int GetTotalVertices() { return GetVertexRowStart(H) + 2 * W + 1; }

int GetEdgeId(int dx, int dy, int le) {
  int ht = GetHorizontalRowStart(dy), vr = GetVerticalRowStart(dy),
      hb = GetHorizontalRowStart(dy + 1), nw, ne, e, se, sw, w = vr + dx;
  e = vr + dx + 1;
  if (Mod2(dy) == 0) {
    nw = ht + 2 * dx;
    ne = ht + 2 * dx + 1;
    sw = hb + 2 * dx;
    se = hb + 2 * dx + 1;
  } else {
    nw = ht + 2 * dx + 1;
    ne = ht + 2 * dx + 2;
    if (dy == H - 1) {
      sw = hb + 2 * dx;
      se = hb + 2 * dx + 1;
    } else {
      sw = hb + 2 * dx + 1;
      se = hb + 2 * dx + 2;
    }
  }
  if (le == 0) return e;
  if (le == 1) return se;
  if (le == 2) return sw;
  if (le == 3) return w;
  if (le == 4) return nw;
  if (le == 5) return ne;
  return -1;
}

int GetVertexIdForTile(int dx, int dy, int lv) {
  int ty = dy, by = dy + 1, nwx, nx, nex, swx, sx, sex;
  if (Mod2(dy) == 0) {
    nwx = 2 * dx;
    nx = 2 * dx + 1;
    nex = 2 * dx + 2;
    swx = 2 * dx;
    sx = 2 * dx + 1;
    sex = 2 * dx + 2;
  } else {
    nwx = 2 * dx + 1;
    nx = 2 * dx + 2;
    nex = 2 * dx + 3;
    swx = 2 * dx + 1;
    sx = 2 * dx + 2;
    sex = 2 * dx + 3;
  }
  if (lv == 0) return GetVertexId(sex, by);
  if (lv == 1) return GetVertexId(sx, by);
  if (lv == 2) return GetVertexId(swx, by);
  if (lv == 3) return GetVertexId(nwx, ty);
  if (lv == 4) return GetVertexId(nx, ty);
  if (lv == 5) return GetVertexId(nex, ty);
  return -1;
}

void SetupGrid(vec2 uv, out vec2 global_pos, out float scale) {
  if (u_gridType == 0) {
    float min_x = -IR3 - 0.1, max_x = float(W) * 2.0 * IR3 + 0.1, min_y = -0.7,
          max_y = float(H) - 0.3;
    float grid_w = max_x - min_x, grid_h = max_y - min_y;
    scale = max(grid_w / (iResolution.x / iResolution.y), grid_h);
    global_pos = (uv * scale * 1.02) +
                 vec2((min_x + max_x) * 0.5, (min_y + max_y) * 0.5);
  } else if (u_gridType == 1) {
    vec2 gridSize = vec2(float(W), float(H));
    scale = max(gridSize.x / iResolution.x, gridSize.y / iResolution.y) *
            iResolution.y;
    global_pos = uv * scale + (gridSize - 1.0) * 0.5;
  } else if (u_gridType == 2) {
    vec2 gridSize = vec2(float(W), float(H) * HR3);
    scale = max(gridSize.x / iResolution.x, gridSize.y / iResolution.y) * 1.1 *
            iResolution.y;
    global_pos =
        uv * scale + vec2((float(W - 1) + 0.5) * 0.5, float(H - 1) * HR3 * 0.5);
  }
}

int GetShapeGeometry(vec2 global_pos, out vec2 local_pos, out float edge_mask,
                     out int terminals, out float radius, out float angle_off,
                     out float angle_mult) {
  float w_outline = w_line * 3.5;

  if (u_gridType == 0) {
    vec2 p = global_pos;
    p.x /= IR3;
    p.x -= mod(floor(p.y), 2.0);
    vec2 ip = floor(p), fp = fract(p);
    if (fp.x > mix(fp.y, 1.0 - fp.y, mod(ip.x, 2.0))) ip.x += 1.0;
    fp = p - ip;
    fp.x *= IR3;
    fp.y -= I3 + mod(ip.x + 1.0, 2.0) * I3;
    if (mod(ip.x, 2.0) > 0.5) fp.y = -fp.y;
    fp.x = abs(fp.x);
    if (dot(fp, FOLD_VEC1) < 0.0)
      fp = fp - FOLD_VEC1 * dot(fp, FOLD_VEC1) * 2.0;
    fp.x = abs(fp.x);

    float dd = max(fp.y, dot(fp, FOLD_VEC2));
    vec2 op = global_pos;
    angle_mult = 0.0;

    if (dd - c < 0.0) {
      op = global_pos * vec2(R3, 1.0) + vec2(0.5, 0.0);
      ip = floor(op);
      fp = op - ip - 0.5;
      fp.x *= IR3;
      fp.y -= I6 * (1.0 - 2.0 * mod(ip.x + ip.y, 2.0));
      fp *= mat2(HR3, 0.5, -0.5, HR3);

      edge_mask = smoothstep(-w_outline, 0.0, dd - c);
      terminals = 6;
      angle_off = PI * 2.0 * I6;
      radius = c;
      local_pos = fp;
      return 2;
    }

    if ((abs(fp.x) - e) < 0.0) {
      op = global_pos * vec2(R3, 2.0) +
           vec2(0.5 - 0.5 * mod(floor(global_pos.y * 2.0 + 0.5), 2.0), 0.5);
      ip = floor(op);
      fp = op - ip - 0.5;
      fp.x *= IR3;
      fp.y /= 2.0;
      float tha = 0.0;
      if (mod(ip.y, 2.0) > 0.5)
        tha = (PI * 2.0 * I3) * 0.5 *
              (2.0 - mod(ip.x + 1.0 + floor(ip.y / 2.0), 2.0));
      fp *= mat2(cos(tha), sin(tha), -sin(tha), cos(tha));

      edge_mask = smoothstep(e - w_outline, e, max(abs(fp.x), abs(fp.y)));
      terminals = 4;
      angle_off = PI * 0.5;
      radius = e;
      local_pos = fp;
      return 1;
    }

    op = global_pos * vec2(HR3, 1.0) +
         vec2(0.5 - 0.5 * mod(floor(global_pos.y + 0.5), 2.0), 0.5);
    ip = floor(op);
    fp = op - ip - 0.5;
    fp.x *= IR3 * 2.0;
    float d_dod = -1.0;
    for (int j = 0; j < 6; j++)
      d_dod = max(d_dod, abs(dot(fp, DOD_NORMS[j])) - f);

    edge_mask = smoothstep(-w_outline, 0.0, d_dod);
    terminals = 12;
    angle_off = PI * I6;
    radius = f;
    local_pos = fp;
    return 0;

  } else if (u_gridType == 1) {
    vec2 octId = floor(global_pos + 0.5), octLocal = global_pos - octId;
    vec2 diaId = floor(global_pos), diaLocal = global_pos - (diaId + 0.5);

    vec2 absOct = abs(octLocal);
    float octSDF =
        max((absOct.x + absOct.y) / R2, max(absOct.x, absOct.y)) - 0.5;
    float diaSDF = abs(diaLocal).x + abs(diaLocal).y - (1.0 - R2 / 2.0);

    bool isDia = (diaSDF <= 0.0);
    local_pos = isDia ? diaLocal : octLocal;
    edge_mask = smoothstep(-w_outline, 0.0, isDia ? diaSDF : octSDF);
    terminals = isDia ? 4 : 8;
    radius = isDia ? ((1.0 - R2 / 2.0) / R2) : 0.5;
    angle_off = isDia ? PI / 2.0 : PI / 4.0;
    angle_mult = isDia ? (-3.0 * PI / 4.0) : (-PI / 2.0);

    return isDia ? 1 : 0;

  } else if (u_gridType == 2) {
    vec2 s = vec2(1.0, R3);
    vec4 hC =
        floor(vec4(global_pos, global_pos - vec2(0.5, HR3)) / s.xyxy) + 0.5;
    vec4 h = vec4(global_pos - hC.xy * s, global_pos - (hC.zw + 0.5) * s);
    bool isGrid0 = dot(h.xy, h.xy) < dot(h.zw, h.zw);
    local_pos = isGrid0 ? h.xy : h.zw;
    vec2 abs_fp = abs(local_pos);

    edge_mask = smoothstep(-w_outline, 0.0,
                           max(abs_fp.x, dot(abs_fp, vec2(0.5, HR3))) - 0.5);
    terminals = 6;
    radius = 0.5;
    angle_off = PI * 2.0 * I6;
    angle_mult = 0.0;

    return isGrid0 ? 0 : 1;
  }
  return -1;
}

int ResolveTileID(vec2 global_pos, int shape_type) {
  if (u_gridType == 0) {
    int result_id = -1;
    float min_dist_sq = 9999.0;
    int rough_y = int(floor(global_pos.y + 0.5)),
        rough_x = int(floor(global_pos.x * HR3));

    for (int y = -1; y <= 1; y++) {
      for (int x = -1; x <= 1; x++) {
        int tx = rough_x + x, ty = rough_y + y;
        vec2 center =
            vec2((float(tx) * 2.0 + mod(float(ty), 2.0)) * IR3, float(ty));
        bool valid = (tx >= 0 && tx < W && ty >= 0 && ty < H);

        if (shape_type == 0) {
          float dSq = dot(global_pos - center, global_pos - center);
          if (dSq < min_dist_sq - 1e-4) {
            min_dist_sq = dSq;
            result_id = valid ? (ty * W + tx) : -1;
          } else if (abs(dSq - min_dist_sq) < 1e-4 && valid)
            result_id = (ty * W + tx);
        } else if (shape_type == 1) {
          for (int i = 0; i < 6; i++) {
            float dSq = dot(global_pos - (center + SQ_OFFS[i]),
                            global_pos - (center + SQ_OFFS[i]));
            if (dSq < min_dist_sq - 1e-4) {
              min_dist_sq = dSq;
              result_id =
                  valid ? (GetTotalDodecagons() + GetEdgeId(tx, ty, i)) : -1;
            } else if (abs(dSq - min_dist_sq) < 1e-4 && valid)
              result_id = GetTotalDodecagons() + GetEdgeId(tx, ty, i);
          }
        } else if (shape_type == 2) {
          for (int i = 0; i < 6; i++) {
            float dSq = dot(global_pos - (center + HEX_OFFS[i]),
                            global_pos - (center + HEX_OFFS[i]));
            if (dSq < min_dist_sq - 1e-4) {
              min_dist_sq = dSq;
              result_id = valid ? (GetTotalDodecagons() + GetTotalSquares() +
                                   GetVertexIdForTile(tx, ty, i))
                                : -1;
            } else if (abs(dSq - min_dist_sq) < 1e-4 && valid)
              result_id = GetTotalDodecagons() + GetTotalSquares() +
                          GetVertexIdForTile(tx, ty, i);
          }
        }
      }
    }
    return result_id;

  } else if (u_gridType == 1) {
    vec2 octId = floor(global_pos + 0.5), diaId = floor(global_pos);
    bool validOct = (octId.x >= 0.0 && octId.x < float(W)) &&
                    (octId.y >= 0.0 && octId.y < float(H));
    bool validDia = (diaId.x >= 0.0 && diaId.x < float(W - 1)) &&
                    (diaId.y >= 0.0 && diaId.y < float(H - 1));
    int totalOcts = int(W * H);
    int octIndex = int(octId.y * float(W) + octId.x);
    int diaIndex = totalOcts + int(diaId.y * float(W - 1) + diaId.x);
    return (shape_type == 1) ? (validDia ? diaIndex : -1)
                             : (validOct ? octIndex : -1);

  } else if (u_gridType == 2) {
    vec2 s = vec2(1.0, R3);
    vec4 hC =
        floor(vec4(global_pos, global_pos - vec2(0.5, HR3)) / s.xyxy) + 0.5;
    bool isGrid0 = (shape_type == 0);
    int row = isGrid0 ? int(round(hC.y * 2.0)) : int(round(hC.w * 2.0 + 1.0));
    int col = isGrid0 ? int(round(hC.x - 0.5)) : int(round(hC.z + 0.5));
    return (row >= 0 && row < H && col >= 0 && col < W) ? (row * W + col) : -1;
  }
  return -1;
}

int GetRoutingIndex(int id) {
  if (u_gridType == 0) {
    int num_dod = GetTotalDodecagons(), num_sq = GetTotalSquares();
    if (id < num_dod) return id * 12;
    if (id < num_dod + num_sq) return num_dod * 12 + (id - num_dod) * 4;
    return num_dod * 12 + num_sq * 4 + (id - num_dod - num_sq) * 6;
  } else if (u_gridType == 1) {
    int totalOcts = int(W * H);
    return (id < totalOcts) ? id * 8 : totalOcts * 8 + (id - totalOcts) * 4;
  } else if (u_gridType == 2) {
    return id * 6;
  }
  return -1;
}

TileData getTile(vec2 global_pos) {
  TileData tile;
  int shape_type = GetShapeGeometry(global_pos, tile.local_pos, tile.edge_mask,
                                    tile.num_terminals, tile.radius,
                                    tile.angle_offset, tile.angle_mult);
  tile.id = ResolveTileID(global_pos, shape_type);
  tile.base_route_idx = tile.id >= 0 ? GetRoutingIndex(tile.id) : -1;
  return tile;
}

vec2 GetTerminalPos(TileData tile, int i) {
  float angle = tile.angle_mult + tile.angle_offset * float(i);
  return vec2(cos(angle), sin(angle)) * tile.radius;
}

bool TileHasRoutes(TileData t) {
  if (t.id < 0) return false;
  for (int p = 0; p < t.num_terminals; p += 2) {
    if (GetRouting(t.base_route_idx + p).x != -1) return true;
  }
  return false;
}

void main() {
  vec2 uv = (gl_FragCoord.xy - 0.5 * iResolution.xy) / iResolution.y;
  uv.y = -uv.y;

  vec2 global_pos;
  float scale;
  SetupGrid(uv, global_pos, scale);
  w_line = max(length(dFdx(global_pos)), length(dFdy(global_pos)));

  TileData tile = getTile(global_pos);
  bool tile_has_path = TileHasRoutes(tile);
  float tile_outline = tile_has_path ? tile.edge_mask : 0.0;

  bool needs_raymarch = (u_allowOverlaps == 0) && !tile_has_path;

  if (needs_raymarch) {
    vec2 grid_center;
    vec2 bounds_max;
    if (u_gridType == 0) {
      grid_center = vec2(float(W - 1) * IR3, float(H - 1) * 0.5);
      bounds_max = vec2(float(W) * 2.0 * IR3 + 1.0, float(H) + 1.0);
    } else if (u_gridType == 1) {
      grid_center = vec2(float(W - 1) * 0.5, float(H - 1) * 0.5);
      bounds_max = vec2(float(W) + 1.0, float(H) + 1.0);
    } else if (u_gridType == 2) {
      grid_center = vec2(float(W - 1) * 0.5, float(H - 1) * HR3 * 0.5);
      bounds_max = vec2(float(W) + 1.0, float(H) * HR3 + 1.0);
    }

    vec2 valid_pos = clamp(global_pos, vec2(-1.0), bounds_max);
    vec2 dir = grid_center - valid_pos;
    dir = (length(dir) < 0.001) ? vec2(1.0, 0.0) : normalize(dir);

    for (int i = 0; i < 400; i++) {
      tile = getTile(valid_pos);
      if (TileHasRoutes(tile)) break;
      valid_pos += dir * 0.025;
    }
  }

  float closestDist = 9999.0;
  vec3 regionColor = vec3(0.0);
  float isColored = 0.0;
  vec4 overlap_color = vec4(0.0);

  if (tile.id >= 0) {
    for (int i = 0; i < tile.num_terminals; i += 2) {
      ivec3 routeIn = GetRouting(tile.base_route_idx + i);
      ivec3 routeOut = GetRouting(tile.base_route_idx + i + 1);

      int pa_idx = routeIn.x, pb_idx = routeOut.x;
      bool flip = routeIn.y == 1;
      int path_id = routeIn.z;

      if (pa_idx != -1 && pb_idx != -1) {
        vec2 pa = GetTerminalPos(tile, pa_idx);
        vec2 pb = GetTerminalPos(tile, pb_idx);

        vec4 d = ArcDistanceUV(tile.local_pos, pa, pb);

        float v_dist = d.y;
        float canonical_v_dist = v_dist;
        if (pa_idx > pb_idx && abs(pa.x * pb.y - pa.y * pb.x) < 1e-5) {
          canonical_v_dist = -v_dist;
        }

        if (u_allowOverlaps == 1) {
          bool isLaneA = (canonical_v_dist < 0.0) != flip;
          float alpha = 1.0 - smoothstep(0.0, w_line, abs(v_dist) - 0.063);

          vec4 layer = vec4(0.0);
          layer.rgb = isLaneA ? ColorFromID(0) : ColorFromID(1);
          layer.a = alpha;

          overlap_color = mix(overlap_color, layer, layer.a);
        } else {
          vec3 loopColor = ColorFromID(path_id);
          DrawBackground(v_dist, abs(v_dist), pa_idx, pb_idx, pa, pb, flip,
                         loopColor, closestDist, regionColor, isColored);
        }
      }
    }
  }

  const vec3 outline_color = vec3(0.6);
  vec3 bg = mix(vec3(0.0), outline_color, tile_outline);

  if (u_allowOverlaps == 1) {
    vec3 final_rgb = mix(bg, overlap_color.rgb, overlap_color.a);
    FragColor = vec4(final_rgb, 1.0);
  } else {
    vec3 final_rgb = mix(bg, regionColor, isColored);
    FragColor = vec4(final_rgb, 1.0);
  }
}