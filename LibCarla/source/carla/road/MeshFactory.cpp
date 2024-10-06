// Copyright (c) 2020 Computer Vision Center (CVC) at the Universitat Autonoma
// de Barcelona (UAB).
//
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.

#include <carla/road/MeshFactory.h>

#include <vector>

#include <carla/geom/Vector3D.h>
#include <carla/geom/Rtree.h>
#include <carla/road/element/LaneMarking.h>
#include <carla/road/element/RoadInfoMarkRecord.h>
#include <carla/road/Map.h>
#include <carla/road/Deformation.h>

namespace carla {
namespace geom {

  // MeshFactory构造函数，接受一个参数用于初始化路面相关参数
  MeshFactory::MeshFactory(rpc::OpendriveGenerationParameters params) {
    road_param.resolution = static_cast<float>(params.vertex_distance); // 设置顶点距离
    road_param.max_road_len = static_cast<float>(params.max_road_length); // 设置最大道路长度
    road_param.extra_lane_width = static_cast<float>(params.additional_width); // 设置额外车道宽度
    road_param.wall_height = static_cast<float>(params.wall_height); // 设置墙体高度
    road_param.vertex_width_resolution = static_cast<float>(params.vertex_width_resolution); // 设置顶点宽度分辨率
  }

  // 为避免浮点精度错误，使用此epsilon值将路径点从车道边缘移开
  static constexpr double EPSILON = 10.0 * std::numeric_limits<double>::epsilon();
  static constexpr double MESH_EPSILON = 50.0 * std::numeric_limits<double>::epsilon();

  // 根据路面生成网格
  std::unique_ptr<Mesh> MeshFactory::Generate(const road::Road &road) const {
    Mesh out_mesh; // 创建输出网格
    for (auto &&lane_section : road.GetLaneSections()) { // 遍历所有车道段
      out_mesh += *Generate(lane_section); // 生成每个车道段的网格并添加到输出网格中
    }
    return std::make_unique<Mesh>(out_mesh); // 返回生成的网格
  }

  // 根据车道段生成网格
  std::unique_ptr<Mesh> MeshFactory::Generate(const road::LaneSection &lane_section) const {
    Mesh out_mesh; // 创建输出网格
    for (auto &&lane_pair : lane_section.GetLanes()) { // 遍历车道段中的所有车道
      out_mesh += *Generate(lane_pair.second); // 生成每条车道的网格并添加到输出网格中
    }
    return std::make_unique<Mesh>(out_mesh); // 返回生成的网格
  }

  // 根据车道生成网格，默认起始点和结束点
  std::unique_ptr<Mesh> MeshFactory::Generate(const road::Lane &lane) const {
    const double s_start = lane.GetDistance() + EPSILON; // 计算起始点
    const double s_end = lane.GetDistance() + lane.GetLength() - EPSILON; // 计算结束点
    return Generate(lane, s_start, s_end); // 调用重载函数生成网格
  }

  // 根据车道生成细分网格，默认起始点和结束点
  std::unique_ptr<Mesh> MeshFactory::GenerateTesselated(const road::Lane& lane) const {
    const double s_start = lane.GetDistance() + EPSILON; // 计算起始点
    const double s_end = lane.GetDistance() + lane.GetLength() - EPSILON; // 计算结束点
    return GenerateTesselated(lane, s_start, s_end); // 调用重载函数生成细分网格
  }

  // 根据车道和给定的起始、结束位置生成网格
  std::unique_ptr<Mesh> MeshFactory::Generate(
      const road::Lane &lane, const double s_start, const double s_end) const {
    RELEASE_ASSERT(road_param.resolution > 0.0); // 确保分辨率大于0
    DEBUG_ASSERT(s_start >= 0.0); // 确保起始点合法
    DEBUG_ASSERT(s_end <= lane.GetDistance() + lane.GetLength()); // 确保结束点合法
    DEBUG_ASSERT(s_end >= EPSILON); // 确保结束点大于Epsilon
    DEBUG_ASSERT(s_start < s_end); // 确保起始点小于结束点
    
    // ID为0的车道在OpenDRIVE中没有物理表示
    Mesh out_mesh; 
    if (lane.GetId() == 0) {
      return std::make_unique<Mesh>(out_mesh); // 返回空网格
    }
    
    double s_current = s_start; // 当前s值初始化为起始点

    std::vector<geom::Vector3D> vertices; // 存储顶点的向量
    if (lane.IsStraight()) { // 如果车道是直的
      // 网格优化：如果车道是直的，只需在开始和结束处添加顶点
      const auto edges = lane.GetCornerPositions(s_current, road_param.extra_lane_width); // 获取当前车道边缘位置
      vertices.push_back(edges.first); // 添加左边缘顶点
      vertices.push_back(edges.second); // 添加右边缘顶点
    } else {
      // 遍历车道的's'并根据宽度存储顶点
      do {
        // 获取当前路径点的车道边缘位置
        const auto edges = lane.GetCornerPositions(s_current, road_param.extra_lane_width);
        vertices.push_back(edges.first); // 添加左边缘顶点
        vertices.push_back(edges.second); // 添加右边缘顶点

        // 更新当前路径点的"s"
        s_current += road_param.resolution; // 增加当前s值
      } while(s_current < s_end); // 继续直到达到结束点
    }

    // 确保网格是连续的，并且车道之间没有间隙，
    // 在车道的末尾添加几何形状
    if (s_end - (s_current - road_param.resolution) > EPSILON) {
      const auto edges = lane.GetCornerPositions(s_end - MESH_EPSILON, road_param.extra_lane_width); // 获取结束点的车道边缘位置
      vertices.push_back(edges.first); // 添加左边缘顶点
      vertices.push_back(edges.second); // 添加右边缘顶点
    }

    // 添加材质，创建三角形带并结束材质
    out_mesh.AddMaterial(
        lane.GetType() == road::Lane::LaneType::Sidewalk ? "sidewalk" : "road"); // 根据车道类型选择材质
    out_mesh.AddTriangleStrip(vertices); // 添加三角形带
    out_mesh.EndMaterial(); // 结束材质
    return std::make_unique<Mesh>(out_mesh); // 返回生成的网格

  std::unique_ptr<Mesh> MeshFactory::GenerateTesselated(
    const road::Lane& lane, const double s_start, const double s_end) const { // 生成细分网格
    RELEASE_ASSERT(road_param.resolution > 0.0); // 确保分辨率大于零
    DEBUG_ASSERT(s_start >= 0.0); // 确保起始s值非负
    DEBUG_ASSERT(s_end <= lane.GetDistance() + lane.GetLength()); // 确保结束s值在有效范围内
    DEBUG_ASSERT(s_end >= EPSILON); // 确保结束s值大于最小值
    DEBUG_ASSERT(s_start < s_end); // 确保起始s值小于结束s值
    // lane_id为0的车道在OpenDRIVE中没有物理表示
    Mesh out_mesh; // 创建网格对象
    if (lane.GetId() == 0) { // 检查车道ID
      return std::make_unique<Mesh>(out_mesh); // 如果ID为0，返回空网格
    }
    double s_current = s_start; // 初始化当前s值

    std::vector<geom::Vector3D> vertices; // 存储顶点的向量
    // 确保宽度方向上的最小顶点数为两个
    const int vertices_in_width = road_param.vertex_width_resolution >= 2 ? road_param.vertex_width_resolution : 2; // 设置宽度方向上的顶点数
    const int segments_number = vertices_in_width - 1; // 计算段数

    std::vector<geom::Vector2D> uvs; // 存储UV坐标
    int uvx = 0; // UV的X坐标
    int uvy = 0; // UV的Y坐标
    // 遍历车道的's'并根据其宽度存储顶点
    do {
      // 获取当前车道在当前位置的边缘位置
      std::pair<geom::Vector3D, geom::Vector3D> edges = lane.GetCornerPositions(s_current, road_param.extra_lane_width); // 获取边缘位置
      const geom::Vector3D segments_size = (edges.second - edges.first) / segments_number; // 计算每个段的大小
      geom::Vector3D current_vertex = edges.first; // 当前顶点初始化为左边缘
      uvx = 0; // 重置UV的X坐标
      for (int i = 0; i < vertices_in_width; ++i) { // 遍历宽度方向的顶点
        uvs.push_back(geom::Vector2D(uvx, uvy)); // 添加UV坐标
        vertices.push_back(current_vertex); // 添加当前顶点
        current_vertex = current_vertex + segments_size; // 更新当前顶点位置
        uvx++; // 更新UV的X坐标
      }
      uvy++; // 更新UV的Y坐标
      // 更新当前waypoint的"s"
      s_current += road_param.resolution; // 增加当前s值
    } while (s_current < s_end); // 当当前s值小于结束s值时继续

    // 确保网格是连续的，没有道路之间的间隙，
    // 在车道末尾添加几何体

    if (s_end - (s_current - road_param.resolution) > EPSILON) { // 检查是否需要在末尾添加几何体
      std::pair<carla::geom::Vector3D, carla::geom::Vector3D> edges =
        lane.GetCornerPositions(s_end - MESH_EPSILON, road_param.extra_lane_width); // 获取结束位置的边缘
      const geom::Vector3D segments_size = (edges.second - edges.first) / segments_number; // 计算段的大小
      geom::Vector3D current_vertex = edges.first; // 当前顶点初始化为左边缘
      uvx = 0; // 重置UV的X坐标
      for (int i = 0; i < vertices_in_width; ++i) // 遍历宽度方向的顶点
      {
        uvs.push_back(geom::Vector2D(uvx, uvy)); // 添加UV坐标
        vertices.push_back(current_vertex); // 添加当前顶点
        current_vertex = current_vertex + segments_size; // 更新当前顶点位置
        uvx++; // 更新UV的X坐标
      }
    }
    out_mesh.AddVertices(vertices); // 添加顶点到网格
    out_mesh.AddUVs(uvs); // 添加UV坐标到网格

    // 添加材质，创建索引并结束材质
    out_mesh.AddMaterial(
      lane.GetType() == road::Lane::LaneType::Sidewalk ? "sidewalk" : "road"); // 根据车道类型选择材质

    const size_t number_of_rows = (vertices.size() / vertices_in_width); // 计算行数

    for (size_t i = 0; i < (number_of_rows - 1); ++i) { // 遍历行
      for (size_t j = 0; j < vertices_in_width - 1; ++j) { // 遍历列
        out_mesh.AddIndex(   j       + (   i       * vertices_in_width ) + 1); // 添加索引
        out_mesh.AddIndex( ( j + 1 ) + (   i       * vertices_in_width ) + 1); // 添加索引
        out_mesh.AddIndex(   j       + ( ( i + 1 ) * vertices_in_width ) + 1); // 添加索引

        out_mesh.AddIndex( ( j + 1 ) + (   i       * vertices_in_width ) + 1); // 添加索引
        out_mesh.AddIndex( ( j + 1 ) + ( ( i + 1 ) * vertices_in_width ) + 1); // 添加索引
        out_mesh.AddIndex(   j       + ( ( i + 1 ) * vertices_in_width ) + 1); // 添加索引
      }
    }
    out_mesh.EndMaterial(); // 结束材质
    return std::make_unique<Mesh>(out_mesh); // 返回生成的网格
}

  void MeshFactory::GenerateLaneSectionOrdered(
    const road::LaneSection &lane_section,
    std::map<carla::road::Lane::LaneType , std::vector<std::unique_ptr<Mesh>>>& result) const {

    const int vertices_in_width = road_param.vertex_width_resolution >= 2 ? road_param.vertex_width_resolution : 2;
    std::vector<size_t> redirections;
    for (auto &&lane_pair : lane_section.GetLanes()) {
      auto it = std::find(redirections.begin(), redirections.end(), lane_pair.first);
      if ( it == redirections.end() ) {
        redirections.push_back(lane_pair.first);
        it = std::find(redirections.begin(), redirections.end(), lane_pair.first);
      }
      size_t PosToAdd = it - redirections.begin();

      Mesh out_mesh;
      switch(lane_pair.second.GetType())
      {
        case road::Lane::LaneType::Driving:
        case road::Lane::LaneType::Parking:
        case road::Lane::LaneType::Bidirectional:
        {
          out_mesh += *GenerateTesselated(lane_pair.second);
          break;
        }
        case road::Lane::LaneType::Shoulder:
        case road::Lane::LaneType::Sidewalk:
        case road::Lane::LaneType::Biking:
        {
          out_mesh += *GenerateSidewalk(lane_pair.second);
          break;
        }
        default:
        {
          out_mesh += *GenerateTesselated(lane_pair.second);
          break;
        }
      }

      if( result[lane_pair.second.GetType()].size() <= PosToAdd ){
        result[lane_pair.second.GetType()].push_back(std::make_unique<Mesh>(out_mesh));
      } else {
        uint32_t verticesinwidth  = SelectVerticesInWidth(vertices_in_width, lane_pair.second.GetType());
        (result[lane_pair.second.GetType()][PosToAdd])->ConcatMesh(out_mesh, verticesinwidth);
      }
    }
  }


  std::unique_ptr<Mesh> MeshFactory::GenerateSidewalk(const road::LaneSection &lane_section) const{
    Mesh out_mesh;
    for (auto &&lane_pair : lane_section.GetLanes()) {
      const double s_start = lane_pair.second.GetDistance() + EPSILON;
      const double s_end = lane_pair.second.GetDistance() + lane_pair.second.GetLength() - EPSILON;
      out_mesh += *GenerateSidewalk(lane_pair.second, s_start, s_end);
    }
    return std::make_unique<Mesh>(out_mesh);
  }
  std::unique_ptr<Mesh> MeshFactory::GenerateSidewalk(const road::Lane &lane) const{
    const double s_start = lane.GetDistance() + EPSILON;
    const double s_end = lane.GetDistance() + lane.GetLength() - EPSILON;
    return GenerateSidewalk(lane, s_start, s_end);
  }
  std::unique_ptr<Mesh> MeshFactory::GenerateSidewalk(
    const road::Lane &lane, const double s_start,
    const double s_end ) const {

    RELEASE_ASSERT(road_param.resolution > 0.0);
    DEBUG_ASSERT(s_start >= 0.0);
    DEBUG_ASSERT(s_end <= lane.GetDistance() + lane.GetLength());
    DEBUG_ASSERT(s_end >= EPSILON);
    DEBUG_ASSERT(s_start < s_end);
    // The lane with lane_id 0 have no physical representation in OpenDRIVE
    Mesh out_mesh;
    if (lane.GetId() == 0) {
      return std::make_unique<Mesh>(out_mesh);
    }
    double s_current = s_start;

    std::vector<geom::Vector3D> vertices;
    // Ensure minimum vertices in width are two
    const int vertices_in_width = 6;
    const int segments_number = vertices_in_width - 1;
    std::vector<geom::Vector2D> uvs;
    int uvy = 0;

    // Iterate over the lane's 's' and store the vertices based on it's width
    do {
      // Get the location of the edges of the current lane at the current waypoint
      std::pair<geom::Vector3D, geom::Vector3D> edges =
        lane.GetCornerPositions(s_current, road_param.extra_lane_width);

      geom::Vector3D low_vertex_first = edges.first - geom::Vector3D(0,0,1);
      geom::Vector3D low_vertex_second = edges.second - geom::Vector3D(0,0,1);
      vertices.push_back(low_vertex_first);
      uvs.push_back(geom::Vector2D(0, uvy));

      vertices.push_back(edges.first);
      uvs.push_back(geom::Vector2D(1, uvy));

      vertices.push_back(edges.first);
      uvs.push_back(geom::Vector2D(1, uvy));

      vertices.push_back(edges.second);
      uvs.push_back(geom::Vector2D(2, uvy));

      vertices.push_back(edges.second);
      uvs.push_back(geom::Vector2D(2, uvy));

      vertices.push_back(low_vertex_second);
      uvs.push_back(geom::Vector2D(3, uvy));

      // Update the current waypoint's "s"
      s_current += road_param.resolution;
      uvy++;
    } while (s_current < s_end);

    // This ensures the mesh is constant and have no gaps between roads,
    // adding geometry at the very end of the lane

    if (s_end - (s_current - road_param.resolution) > EPSILON) {
      std::pair<carla::geom::Vector3D, carla::geom::Vector3D> edges =
        lane.GetCornerPositions(s_end - MESH_EPSILON, road_param.extra_lane_width);

      geom::Vector3D low_vertex_first = edges.first - geom::Vector3D(0,0,1);
      geom::Vector3D low_vertex_second = edges.second - geom::Vector3D(0,0,1);

      vertices.push_back(low_vertex_first);
      uvs.push_back(geom::Vector2D(0, uvy));

      vertices.push_back(edges.first);
      uvs.push_back(geom::Vector2D(1, uvy));

      vertices.push_back(edges.first);
      uvs.push_back(geom::Vector2D(1, uvy));

      vertices.push_back(edges.second);
      uvs.push_back(geom::Vector2D(2, uvy));

      vertices.push_back(edges.second);
      uvs.push_back(geom::Vector2D(2, uvy));

      vertices.push_back(low_vertex_second);
      uvs.push_back(geom::Vector2D(3, uvy));

    }

    out_mesh.AddVertices(vertices);
    out_mesh.AddUVs(uvs);
    // Add the adient material, create the strip and close the material
    out_mesh.AddMaterial(
      lane.GetType() == road::Lane::LaneType::Sidewalk ? "sidewalk" : "road");

    const int number_of_rows = (vertices.size() / vertices_in_width);

    for (size_t i = 0; i < (number_of_rows - 1); ++i) {
      for (size_t j = 0; j < vertices_in_width - 1; ++j) {

        if(j == 1 || j == 3){
          continue;
        }

        out_mesh.AddIndex(   j       + (   i       * vertices_in_width ) + 1);
        out_mesh.AddIndex( ( j + 1 ) + (   i       * vertices_in_width ) + 1);
        out_mesh.AddIndex(   j       + ( ( i + 1 ) * vertices_in_width ) + 1);

        out_mesh.AddIndex( ( j + 1 ) + (   i       * vertices_in_width ) + 1);
        out_mesh.AddIndex( ( j + 1 ) + ( ( i + 1 ) * vertices_in_width ) + 1);
        out_mesh.AddIndex(   j       + ( ( i + 1 ) * vertices_in_width ) + 1);

      }
    }
    out_mesh.EndMaterial();
    return std::make_unique<Mesh>(out_mesh);
  }
  std::unique_ptr<Mesh> MeshFactory::GenerateWalls(const road::LaneSection &lane_section) const {
    Mesh out_mesh;

    const auto min_lane = lane_section.GetLanes().begin()->first == 0 ?
        1 : lane_section.GetLanes().begin()->first;
    const auto max_lane = lane_section.GetLanes().rbegin()->first == 0 ?
        -1 : lane_section.GetLanes().rbegin()->first;

    for (auto &&lane_pair : lane_section.GetLanes()) {
      const auto &lane = lane_pair.second;
      const double s_start = lane.GetDistance() + EPSILON;
      const double s_end = lane.GetDistance() + lane.GetLength() - EPSILON;
      if (lane.GetId() == max_lane) {
        out_mesh += *GenerateLeftWall(lane, s_start, s_end);
      }
      if (lane.GetId() == min_lane) {
        out_mesh += *GenerateRightWall(lane, s_start, s_end);
      }
    }
    return std::make_unique<Mesh>(out_mesh);
  }

  std::unique_ptr<Mesh> MeshFactory::GenerateRightWall(
      const road::Lane &lane, const double s_start, const double s_end) const {
    RELEASE_ASSERT(road_param.resolution > 0.0);
    DEBUG_ASSERT(s_start >= 0.0);
    DEBUG_ASSERT(s_end <= lane.GetDistance() + lane.GetLength());
    DEBUG_ASSERT(s_end >= EPSILON);
    DEBUG_ASSERT(s_start < s_end);
    // The lane with lane_id 0 have no physical representation in OpenDRIVE
    Mesh out_mesh;
    if (lane.GetId() == 0) {
      return std::make_unique<Mesh>(out_mesh);
    }
    double s_current = s_start;
    const geom::Vector3D height_vector = geom::Vector3D(0.f, 0.f, road_param.wall_height);

    std::vector<geom::Vector3D> r_vertices;
    if (lane.IsStraight()) {
      // Mesh optimization: If the lane is straight just add vertices at the
      // begining and at the end of it
      const auto edges = lane.GetCornerPositions(s_current, road_param.extra_lane_width);
      r_vertices.push_back(edges.first + height_vector);
      r_vertices.push_back(edges.first);
    } else {
      // Iterate over the lane's 's' and store the vertices based on it's width
      do {
        // Get the location of the edges of the current lane at the current waypoint
        const auto edges = lane.GetCornerPositions(s_current, road_param.extra_lane_width);
        r_vertices.push_back(edges.first + height_vector);
        r_vertices.push_back(edges.first);

        // Update the current waypoint's "s"
        s_current += road_param.resolution;
      } while(s_current < s_end);
    }

    // This ensures the mesh is constant and have no gaps between roads,
    // adding geometry at the very end of the lane
    if (s_end - (s_current - road_param.resolution) > EPSILON) {
      const auto edges = lane.GetCornerPositions(s_end - MESH_EPSILON, road_param.extra_lane_width);
      r_vertices.push_back(edges.first + height_vector);
      r_vertices.push_back(edges.first);
    }

    // Add the adient material, create the strip and close the material
    out_mesh.AddMaterial(
        lane.GetType() == road::Lane::LaneType::Sidewalk ? "sidewalk" : "road");
    out_mesh.AddTriangleStrip(r_vertices);
    out_mesh.EndMaterial();
    return std::make_unique<Mesh>(out_mesh);
  }

  std::unique_ptr<Mesh> MeshFactory::GenerateLeftWall(
      const road::Lane &lane, const double s_start, const double s_end) const {
    RELEASE_ASSERT(road_param.resolution > 0.0);
    DEBUG_ASSERT(s_start >= 0.0);
    DEBUG_ASSERT(s_end <= lane.GetDistance() + lane.GetLength());
    DEBUG_ASSERT(s_end >= EPSILON);
    DEBUG_ASSERT(s_start < s_end);
    // The lane with lane_id 0 have no physical representation in OpenDRIVE
    Mesh out_mesh;
    if (lane.GetId() == 0) {
      return std::make_unique<Mesh>(out_mesh);
    }
    double s_current = s_start;
    const geom::Vector3D height_vector = geom::Vector3D(0.f, 0.f, road_param.wall_height);

    std::vector<geom::Vector3D> l_vertices;
    if (lane.IsStraight()) {
      // Mesh optimization: If the lane is straight just add vertices at the
      // begining and at the end of it
      const auto edges = lane.GetCornerPositions(s_current, road_param.extra_lane_width);
      l_vertices.push_back(edges.second);
      l_vertices.push_back(edges.second + height_vector);
    } else {
      // Iterate over the lane's 's' and store the vertices based on it's width
      do {
        // Get the location of the edges of the current lane at the current waypoint
        const auto edges = lane.GetCornerPositions(s_current, road_param.extra_lane_width);
        l_vertices.push_back(edges.second);
        l_vertices.push_back(edges.second + height_vector);

        // Update the current waypoint's "s"
        s_current += road_param.resolution;
      } while(s_current < s_end);
    }

    // This ensures the mesh is constant and have no gaps between roads,
    // adding geometry at the very end of the lane
    if (s_end - (s_current - road_param.resolution) > EPSILON) {
      const auto edges = lane.GetCornerPositions(s_end - MESH_EPSILON, road_param.extra_lane_width);
      l_vertices.push_back(edges.second);
      l_vertices.push_back(edges.second + height_vector);
    }

    // Add the adient material, create the strip and close the material
    out_mesh.AddMaterial(
        lane.GetType() == road::Lane::LaneType::Sidewalk ? "sidewalk" : "road");
    out_mesh.AddTriangleStrip(l_vertices);
    out_mesh.EndMaterial();
    return std::make_unique<Mesh>(out_mesh);
  }

  std::vector<std::unique_ptr<Mesh>> MeshFactory::GenerateWithMaxLen(
      const road::Road &road) const {
    std::vector<std::unique_ptr<Mesh>> mesh_uptr_list;
    for (auto &&lane_section : road.GetLaneSections()) {
      auto section_uptr_list = GenerateWithMaxLen(lane_section);
      mesh_uptr_list.insert(
          mesh_uptr_list.end(),
          std::make_move_iterator(section_uptr_list.begin()),
          std::make_move_iterator(section_uptr_list.end()));
    }
    return mesh_uptr_list;
  }

  std::vector<std::unique_ptr<Mesh>> MeshFactory::GenerateWithMaxLen(
      const road::LaneSection &lane_section) const {
    std::vector<std::unique_ptr<Mesh>> mesh_uptr_list;
    if (lane_section.GetLength() < road_param.max_road_len) {
      mesh_uptr_list.emplace_back(Generate(lane_section));
    } else {
      double s_current = lane_section.GetDistance() + EPSILON;
      const double s_end = lane_section.GetDistance() + lane_section.GetLength() - EPSILON;
      while(s_current + road_param.max_road_len < s_end) {
        const auto s_until = s_current + road_param.max_road_len;
        Mesh lane_section_mesh;
        for (auto &&lane_pair : lane_section.GetLanes()) {
          lane_section_mesh += *Generate(lane_pair.second, s_current, s_until);
        }
        mesh_uptr_list.emplace_back(std::make_unique<Mesh>(lane_section_mesh));
        s_current = s_until;
      }
      if (s_end - s_current > EPSILON) {
        Mesh lane_section_mesh;
        for (auto &&lane_pair : lane_section.GetLanes()) {
          lane_section_mesh += *Generate(lane_pair.second, s_current, s_end);
        }
        mesh_uptr_list.emplace_back(std::make_unique<Mesh>(lane_section_mesh));
      }
    }
    return mesh_uptr_list;
  }

std::map<road::Lane::LaneType , std::vector<std::unique_ptr<Mesh>>> MeshFactory::GenerateOrderedWithMaxLen(
      const road::Road &road) const {
    std::map<road::Lane::LaneType , std::vector<std::unique_ptr<Mesh>>> mesh_uptr_list;
    for (auto &&lane_section : road.GetLaneSections()) {
      std::map<road::Lane::LaneType , std::vector<std::unique_ptr<Mesh>>> section_uptr_list = GenerateOrderedWithMaxLen(lane_section);
      mesh_uptr_list.insert(
        std::make_move_iterator(section_uptr_list.begin()),
        std::make_move_iterator(section_uptr_list.end()));
    }
    return mesh_uptr_list;
  }

  std::map<road::Lane::LaneType , std::vector<std::unique_ptr<Mesh>>> MeshFactory::GenerateOrderedWithMaxLen(
    const road::LaneSection &lane_section) const {
      const int vertices_in_width = road_param.vertex_width_resolution >= 2 ? road_param.vertex_width_resolution : 2;
      std::map<road::Lane::LaneType , std::vector<std::unique_ptr<Mesh>>> mesh_uptr_list;

      if (lane_section.GetLength() < road_param.max_road_len) {
        GenerateLaneSectionOrdered(lane_section, mesh_uptr_list);
      } else {
        double s_current = lane_section.GetDistance() + EPSILON;
        const double s_end = lane_section.GetDistance() + lane_section.GetLength() - EPSILON;
        std::vector<size_t> redirections;
        while(s_current + road_param.max_road_len < s_end) {
          const auto s_until = s_current + road_param.max_road_len;

          for (auto &&lane_pair : lane_section.GetLanes()) {
            Mesh lane_section_mesh;
            switch(lane_pair.second.GetType())
            {
              case road::Lane::LaneType::Driving:
              case road::Lane::LaneType::Parking:
              case road::Lane::LaneType::Bidirectional:
              {
                lane_section_mesh += *GenerateTesselated(lane_pair.second, s_current, s_until);
                break;
              }
              case road::Lane::LaneType::Shoulder:
              case road::Lane::LaneType::Sidewalk:
              case road::Lane::LaneType::Biking:
              {
                lane_section_mesh += *GenerateSidewalk(lane_pair.second, s_current, s_until);
                break;
              }
              default:
              {
                 lane_section_mesh += *GenerateTesselated(lane_pair.second, s_current, s_until);
                break;
              }
            }
            auto it = std::find(redirections.begin(), redirections.end(), lane_pair.first);
            if (it == redirections.end()) {
              redirections.push_back(lane_pair.first);
              it = std::find(redirections.begin(), redirections.end(), lane_pair.first);
            }

            size_t PosToAdd = it - redirections.begin();
            if (mesh_uptr_list[lane_pair.second.GetType()].size() <= PosToAdd) {
              mesh_uptr_list[lane_pair.second.GetType()].push_back(std::make_unique<Mesh>(lane_section_mesh));
            } else {
              uint32_t verticesinwidth = SelectVerticesInWidth(vertices_in_width, lane_pair.second.GetType());
              (mesh_uptr_list[lane_pair.second.GetType()][PosToAdd])->ConcatMesh(lane_section_mesh, verticesinwidth);
            }
          }
          s_current = s_until;
        }
        if (s_end - s_current > EPSILON) {
          for (auto &&lane_pair : lane_section.GetLanes()) {
            Mesh lane_section_mesh;
            switch(lane_pair.second.GetType())
            {
              case road::Lane::LaneType::Driving:
              case road::Lane::LaneType::Parking:
              case road::Lane::LaneType::Bidirectional:
              {
                lane_section_mesh += *GenerateTesselated(lane_pair.second, s_current, s_end);
                break;
              }
              case road::Lane::LaneType::Shoulder:
              case road::Lane::LaneType::Sidewalk:
              case road::Lane::LaneType::Biking:
              {
                lane_section_mesh += *GenerateSidewalk(lane_pair.second, s_current, s_end);
                break;
              }
              default:
              {
                lane_section_mesh += *GenerateTesselated(lane_pair.second, s_current, s_end);
                break;
              }
            }

            auto it = std::find(redirections.begin(), redirections.end(), lane_pair.first);
            if (it == redirections.end()) {
              redirections.push_back(lane_pair.first);
              it = std::find(redirections.begin(), redirections.end(), lane_pair.first);
            }

            size_t PosToAdd = it - redirections.begin();

            if (mesh_uptr_list[lane_pair.second.GetType()].size() <= PosToAdd) {
              mesh_uptr_list[lane_pair.second.GetType()].push_back(std::make_unique<Mesh>(lane_section_mesh));
            } else {
              *(mesh_uptr_list[lane_pair.second.GetType()][PosToAdd]) += lane_section_mesh;
            }
          }
        }
      }
      return mesh_uptr_list;
  }

  std::vector<std::unique_ptr<Mesh>> MeshFactory::GenerateWallsWithMaxLen(
      const road::Road &road) const {
    std::vector<std::unique_ptr<Mesh>> mesh_uptr_list;
    for (auto &&lane_section : road.GetLaneSections()) {
      auto section_uptr_list = GenerateWallsWithMaxLen(lane_section);
      mesh_uptr_list.insert(
          mesh_uptr_list.end(),
          std::make_move_iterator(section_uptr_list.begin()),
          std::make_move_iterator(section_uptr_list.end()));
    }
    return mesh_uptr_list;
  }

  std::vector<std::unique_ptr<Mesh>> MeshFactory::GenerateWallsWithMaxLen(
      const road::LaneSection &lane_section) const {
    std::vector<std::unique_ptr<Mesh>> mesh_uptr_list;

    const auto min_lane = lane_section.GetLanes().begin()->first == 0 ?
        1 : lane_section.GetLanes().begin()->first;
    const auto max_lane = lane_section.GetLanes().rbegin()->first == 0 ?
        -1 : lane_section.GetLanes().rbegin()->first;

    if (lane_section.GetLength() < road_param.max_road_len) {
      mesh_uptr_list.emplace_back(GenerateWalls(lane_section));
    } else {
      double s_current = lane_section.GetDistance() + EPSILON;
      const double s_end = lane_section.GetDistance() + lane_section.GetLength() - EPSILON;
      while(s_current + road_param.max_road_len < s_end) {
        const auto s_until = s_current + road_param.max_road_len;
        Mesh lane_section_mesh;
        for (auto &&lane_pair : lane_section.GetLanes()) {
          const auto &lane = lane_pair.second;
          if (lane.GetId() == max_lane) {
            lane_section_mesh += *GenerateLeftWall(lane, s_current, s_until);
          }
          if (lane.GetId() == min_lane) {
            lane_section_mesh += *GenerateRightWall(lane, s_current, s_until);
          }
        }
        mesh_uptr_list.emplace_back(std::make_unique<Mesh>(lane_section_mesh));
        s_current = s_until;
      }
      if (s_end - s_current > EPSILON) {
        Mesh lane_section_mesh;
        for (auto &&lane_pair : lane_section.GetLanes()) {
          const auto &lane = lane_pair.second;
          if (lane.GetId() == max_lane) {
            lane_section_mesh += *GenerateLeftWall(lane, s_current, s_end);
          }
          if (lane.GetId() == min_lane) {
            lane_section_mesh += *GenerateRightWall(lane, s_current, s_end);
          }
        }
        mesh_uptr_list.emplace_back(std::make_unique<Mesh>(lane_section_mesh));
      }
    }
    return mesh_uptr_list;
  }

  std::vector<std::unique_ptr<Mesh>> MeshFactory::GenerateAllWithMaxLen(
      const road::Road &road) const {
    std::vector<std::unique_ptr<Mesh>> mesh_uptr_list;

    // Get road meshes
    auto roads = GenerateWithMaxLen(road);
    mesh_uptr_list.insert(
        mesh_uptr_list.end(),
        std::make_move_iterator(roads.begin()),
        std::make_move_iterator(roads.end()));

    // Get wall meshes only if is not a junction
    if (!road.IsJunction()) {
      auto walls = GenerateWallsWithMaxLen(road);

      if (roads.size() == walls.size()) {
        for (size_t i = 0; i < walls.size(); ++i) {
          *mesh_uptr_list[i] += *walls[i];
        }
      } else {
        mesh_uptr_list.insert(
            mesh_uptr_list.end(),
            std::make_move_iterator(walls.begin()),
            std::make_move_iterator(walls.end()));
      }
    }

    return mesh_uptr_list;
  }

  void MeshFactory::GenerateAllOrderedWithMaxLen(
      const road::Road &road,
      std::map<road::Lane::LaneType , std::vector<std::unique_ptr<Mesh>>>& roads
      ) const {

    // Get road meshes
    std::map<road::Lane::LaneType , std::vector<std::unique_ptr<Mesh>>> result = GenerateOrderedWithMaxLen(road);
    for (auto &pair_map : result)
    {
      std::vector<std::unique_ptr<Mesh>>& origin = roads[pair_map.first];
      std::vector<std::unique_ptr<Mesh>>& source = pair_map.second;
      std::move(source.begin(), source.end(), std::back_inserter(origin));
    }
  }

  void MeshFactory::GenerateLaneMarkForRoad(
    const road::Road& road,
    std::vector<std::unique_ptr<Mesh>>& inout,
    std::vector<std::string>& outinfo ) const
  {
    for (auto&& lane_section : road.GetLaneSections()) {
      for (auto&& lane : lane_section.GetLanes()) {
        if (lane.first != 0) {
          switch(lane.second.GetType())
          {
            case road::Lane::LaneType::Driving:
            case road::Lane::LaneType::Parking:
            case road::Lane::LaneType::Bidirectional:
            {
              GenerateLaneMarksForNotCenterLine(lane_section, lane.second, inout, outinfo);
              outinfo.push_back("white");
              break;
            }
          }
        } else {
          if(lane.second.GetType() == road::Lane::LaneType::None ){
            GenerateLaneMarksForCenterLine(road, lane_section, lane.second, inout, outinfo);
            outinfo.push_back("yellow");
          }
        }
      }
    }
  }

  void MeshFactory::GenerateLaneMarksForNotCenterLine(
    const road::LaneSection& lane_section,
    const road::Lane& lane,
    std::vector<std::unique_ptr<Mesh>>& inout,
    std::vector<std::string>& outinfo ) const {
    Mesh out_mesh;
    const double s_start = lane_section.GetDistance();
    const double s_end = lane_section.GetDistance() + lane_section.GetLength();
    double s_current = s_start;
    std::vector<geom::Vector3D> vertices;
    std::vector<size_t> indices;

    do {
      //Get Lane info
      const carla::road::element::RoadInfoMarkRecord* road_info_mark = lane.GetInfo<carla::road::element::RoadInfoMarkRecord>(s_current);
      if (road_info_mark != nullptr) {
        carla::road::element::LaneMarking lane_mark_info(*road_info_mark);

        switch (lane_mark_info.type) {
          case carla::road::element::LaneMarking::Type::Solid: {
            size_t currentIndex = out_mesh.GetVertices().size() + 1;

            std::pair<geom::Vector3D, geom::Vector3D> edges = 
              ComputeEdgesForLanemark(lane_section, lane, s_current, lane_mark_info.width);

            out_mesh.AddVertex(edges.first);
            out_mesh.AddVertex(edges.second);

            out_mesh.AddIndex(currentIndex);
            out_mesh.AddIndex(currentIndex + 1);
            out_mesh.AddIndex(currentIndex + 2);

            out_mesh.AddIndex(currentIndex + 1);
            out_mesh.AddIndex(currentIndex + 3);
            out_mesh.AddIndex(currentIndex + 2);

            s_current += road_param.resolution;
            break;
          }
          case carla::road::element::LaneMarking::Type::Broken: {
            size_t currentIndex = out_mesh.GetVertices().size() + 1;

            std::pair<geom::Vector3D, geom::Vector3D> edges = 
              ComputeEdgesForLanemark(lane_section, lane, s_current, lane_mark_info.width);

            out_mesh.AddVertex(edges.first);
            out_mesh.AddVertex(edges.second);

            s_current += road_param.resolution * 3;
            if (s_current > s_end)
            {
              s_current = s_end;
            }

            edges = ComputeEdgesForLanemark(lane_section, lane, s_current, lane_mark_info.width);

            out_mesh.AddVertex(edges.first);
            out_mesh.AddVertex(edges.second);
            
            out_mesh.AddIndex(currentIndex);
            out_mesh.AddIndex(currentIndex + 1);
            out_mesh.AddIndex(currentIndex + 2);

            out_mesh.AddIndex(currentIndex + 1);
            out_mesh.AddIndex(currentIndex + 3);
            out_mesh.AddIndex(currentIndex + 2);

            s_current += road_param.resolution * 3;

            break;
          }
          case carla::road::element::LaneMarking::Type::SolidSolid: {
            s_current += road_param.resolution;
            break;
          }
          case carla::road::element::LaneMarking::Type::SolidBroken: {
            s_current += road_param.resolution;
            break;
          }
          case carla::road::element::LaneMarking::Type::BrokenSolid: {
            s_current += road_param.resolution;
            break;
          }
          case carla::road::element::LaneMarking::Type::BrokenBroken: {
            s_current += road_param.resolution;
            break;
          }
          case carla::road::element::LaneMarking::Type::BottsDots: {
            s_current += road_param.resolution;
            break;
          }
          case carla::road::element::LaneMarking::Type::Grass: {
            s_current += road_param.resolution;
            break;
          }
          case carla::road::element::LaneMarking::Type::Curb: {
            s_current += road_param.resolution;
            break;
          }
          case carla::road::element::LaneMarking::Type::Other: {
            s_current += road_param.resolution;
            break;
          }
          default: {
            s_current += road_param.resolution;
            break;
          }
        }
      }
    } while (s_current < s_end);

    if (out_mesh.IsValid()) {
      const carla::road::element::RoadInfoMarkRecord* road_info_mark = lane.GetInfo<carla::road::element::RoadInfoMarkRecord>(s_current);
      if (road_info_mark != nullptr) {
        carla::road::element::LaneMarking lane_mark_info(*road_info_mark);
        
        std::pair<geom::Vector3D, geom::Vector3D> edges = 
              ComputeEdgesForLanemark(lane_section, lane, s_end, lane_mark_info.width);

        out_mesh.AddVertex(edges.first);
        out_mesh.AddVertex(edges.second);
      }
      inout.push_back(std::make_unique<Mesh>(out_mesh));
    }
  }

  void MeshFactory::GenerateLaneMarksForCenterLine(
    const road::Road& road,
    const road::LaneSection& lane_section,
    const road::Lane& lane,
    std::vector<std::unique_ptr<Mesh>>& inout,
    std::vector<std::string>& outinfo ) const
  {
    Mesh out_mesh;
    const double s_start = lane_section.GetDistance();
    const double s_end = lane_section.GetDistance() + lane_section.GetLength();
    double s_current = s_start;
    std::vector<geom::Vector3D> vertices;
    std::vector<size_t> indices;

    do {
      //Get Lane info
      const carla::road::element::RoadInfoMarkRecord* road_info_mark = lane.GetInfo<carla::road::element::RoadInfoMarkRecord>(s_current);
      if (road_info_mark != nullptr) {
        carla::road::element::LaneMarking lane_mark_info(*road_info_mark);

        switch (lane_mark_info.type) {
          case carla::road::element::LaneMarking::Type::Solid: {
            size_t currentIndex = out_mesh.GetVertices().size() + 1;

            carla::road::element::DirectedPoint rightpoint = road.GetDirectedPointIn(s_current);
            carla::road::element::DirectedPoint leftpoint = rightpoint;

            rightpoint.ApplyLateralOffset(lane_mark_info.width * 0.5);
            leftpoint.ApplyLateralOffset(lane_mark_info.width * -0.5);

            // Unreal's Y axis hack
            rightpoint.location.y *= -1;
            leftpoint.location.y *= -1;

            out_mesh.AddVertex(rightpoint.location);
            out_mesh.AddVertex(leftpoint.location);

            out_mesh.AddIndex(currentIndex);
            out_mesh.AddIndex(currentIndex + 1);
            out_mesh.AddIndex(currentIndex + 2);

            out_mesh.AddIndex(currentIndex + 1);
            out_mesh.AddIndex(currentIndex + 3);
            out_mesh.AddIndex(currentIndex + 2);

            s_current += road_param.resolution;
            break;
          }
          case carla::road::element::LaneMarking::Type::Broken: {
            size_t currentIndex = out_mesh.GetVertices().size() + 1;

            std::pair<geom::Vector3D, geom::Vector3D> edges = 
              ComputeEdgesForLanemark(lane_section, lane, s_current, lane_mark_info.width);
            
            out_mesh.AddVertex(edges.first);
            out_mesh.AddVertex(edges.second);

            s_current += road_param.resolution * 3;
            if (s_current > s_end) {
              s_current = s_end;
            }

            edges = ComputeEdgesForLanemark(lane_section, lane, s_current, lane_mark_info.width);

            out_mesh.AddVertex(edges.first);
            out_mesh.AddVertex(edges.second);

            out_mesh.AddIndex(currentIndex);
            out_mesh.AddIndex(currentIndex + 1);
            out_mesh.AddIndex(currentIndex + 2);

            out_mesh.AddIndex(currentIndex + 1);
            out_mesh.AddIndex(currentIndex + 3);
            out_mesh.AddIndex(currentIndex + 2);

            s_current += road_param.resolution * 3;

            break;
          }
          case carla::road::element::LaneMarking::Type::SolidSolid: {
            s_current += road_param.resolution;
            break;
          }
          case carla::road::element::LaneMarking::Type::SolidBroken: {
            s_current += road_param.resolution;
            break;
          }
          case carla::road::element::LaneMarking::Type::BrokenSolid: {
            s_current += road_param.resolution;
            break;
          }
          case carla::road::element::LaneMarking::Type::BrokenBroken: {
            s_current += road_param.resolution;
            break;
          }
          case carla::road::element::LaneMarking::Type::BottsDots: {
            s_current += road_param.resolution;
            break;
          }
          case carla::road::element::LaneMarking::Type::Grass: {
            s_current += road_param.resolution;
            break;
          }
          case carla::road::element::LaneMarking::Type::Curb: {
            s_current += road_param.resolution;
            break;
          }
          case carla::road::element::LaneMarking::Type::Other: {
            s_current += road_param.resolution;
            break;
          }
          default: {
            s_current += road_param.resolution;
            break;
          }
        }
      }
    } while (s_current < s_end);

    if (out_mesh.IsValid()) {
      const carla::road::element::RoadInfoMarkRecord* road_info_mark = lane.GetInfo<carla::road::element::RoadInfoMarkRecord>(s_current);
      if (road_info_mark != nullptr)
      {
        carla::road::element::LaneMarking lane_mark_info(*road_info_mark);
        carla::road::element::DirectedPoint rightpoint = road.GetDirectedPointIn(s_current);
        carla::road::element::DirectedPoint leftpoint = rightpoint;

        rightpoint.ApplyLateralOffset(lane_mark_info.width * 0.5f);
        leftpoint.ApplyLateralOffset(lane_mark_info.width * -0.5f);

        // Unreal's Y axis hack
        rightpoint.location.y *= -1;
        leftpoint.location.y *= -1;

        out_mesh.AddVertex(rightpoint.location);
        out_mesh.AddVertex(leftpoint.location);

      }
      inout.push_back(std::make_unique<Mesh>(out_mesh));
    }
  }

  struct VertexWeight {
    Mesh::vertex_type* vertex;
    double weight;
  };
  struct VertexNeighbors {
    Mesh::vertex_type* vertex;
    std::vector<VertexWeight> neighbors;
  };
  struct VertexInfo {
    Mesh::vertex_type * vertex;
    size_t lane_mesh_idx;
    bool is_static;
  };

  // Helper function to compute the weight of neighboring vertices
  static VertexWeight ComputeVertexWeight(
      const MeshFactory::RoadParameters &road_param,
      const VertexInfo &vertex_info,
      const VertexInfo &neighbor_info) {
    const float distance3D = geom::Math::Distance(*vertex_info.vertex, *neighbor_info.vertex);
    // Ignore vertices beyond a certain distance
    if(distance3D > road_param.max_weight_distance) {
      return {neighbor_info.vertex, 0};
    }
    if(abs(distance3D) < EPSILON) {
      return {neighbor_info.vertex, 0};
    }
    float weight = geom::Math::Clamp<float>(1.0f / distance3D, 0.0f, 100000.0f);

    // Additional weight to vertices in the same lane
    if(vertex_info.lane_mesh_idx == neighbor_info.lane_mesh_idx) {
      weight *= road_param.same_lane_weight_multiplier;
      // Further additional weight for fixed verices
      if(neighbor_info.is_static) {
        weight *= road_param.lane_ends_multiplier;
      }
    }
    return {neighbor_info.vertex, weight};
  }

  // Helper function to compute neighborhoord of vertices and their weights
  std::vector<VertexNeighbors> GetVertexNeighborhoodAndWeights(
      const MeshFactory::RoadParameters &road_param,
      std::vector<std::unique_ptr<Mesh>> &lane_meshes) {
    // Build rtree for neighborhood queries
    using Rtree = geom::PointCloudRtree<VertexInfo>;
    using Point = Rtree::BPoint;
    Rtree rtree;
    for (size_t lane_mesh_idx = 0; lane_mesh_idx < lane_meshes.size(); ++lane_mesh_idx) {
      auto& mesh = lane_meshes[lane_mesh_idx];
      for(size_t i = 0; i < mesh->GetVerticesNum(); ++i) {
        auto& vertex = mesh->GetVertices()[i];
        Point point(vertex.x, vertex.y, vertex.z);
        if (i < 2 || i >= mesh->GetVerticesNum() - 2) {
          rtree.InsertElement({point, {&vertex, lane_mesh_idx, true}});
        } else {
          rtree.InsertElement({point, {&vertex, lane_mesh_idx, false}});
        }
      }
    }

    // Find neighbors for each vertex and compute their weight
    std::vector<VertexNeighbors> vertices_neighborhoods;
    for (size_t lane_mesh_idx = 0; lane_mesh_idx < lane_meshes.size(); ++lane_mesh_idx) {
      auto& mesh = lane_meshes[lane_mesh_idx];
      for(size_t i = 0; i < mesh->GetVerticesNum(); ++i) {
        if (i > 2 && i < mesh->GetVerticesNum() - 2) {
          auto& vertex = mesh->GetVertices()[i];
          Point point(vertex.x, vertex.y, vertex.z);
          auto closest_vertices = rtree.GetNearestNeighbours(point, 20);
          VertexNeighbors vertex_neighborhood;
          vertex_neighborhood.vertex = &vertex;
          for(auto& close_vertex : closest_vertices) {
            auto &vertex_info = close_vertex.second;
            if(&vertex == vertex_info.vertex) {
              continue;
            }
            auto vertex_weight = ComputeVertexWeight(
                road_param, {&vertex, lane_mesh_idx, false}, vertex_info);
            if(vertex_weight.weight > 0)
              vertex_neighborhood.neighbors.push_back(vertex_weight);
          }
          vertices_neighborhoods.push_back(vertex_neighborhood);
        }
      }
    }
    return vertices_neighborhoods;
  }

  std::unique_ptr<Mesh> MeshFactory::MergeAndSmooth(std::vector<std::unique_ptr<Mesh>> &lane_meshes) const {
    geom::Mesh out_mesh;

    auto vertices_neighborhoods = GetVertexNeighborhoodAndWeights(road_param, lane_meshes);

    // Laplacian function
    auto Laplacian = [&](const Mesh::vertex_type* vertex, const std::vector<VertexWeight> &neighbors) -> double {
      double sum = 0;
      double sum_weight = 0;
      for(auto &element : neighbors) {
        sum += (element.vertex->z - vertex->z)*element.weight;
        sum_weight += element.weight;
      }
      if(sum_weight > 0)
        return sum / sum_weight;
      else
        return 0;
    };
    // Run iterative algorithm
    double lambda = 0.5;
    int iterations = 100;
    for(int iter = 0; iter < iterations; ++iter) {
      for (auto& vertex_neighborhood : vertices_neighborhoods) {
        auto * vertex = vertex_neighborhood.vertex;
        vertex->z += static_cast<float>(lambda*Laplacian(vertex, vertex_neighborhood.neighbors));
      }
    }

    for(auto &mesh : lane_meshes) {
      out_mesh += *mesh;
    }

    return std::make_unique<Mesh>(out_mesh);
  }

  uint32_t MeshFactory::SelectVerticesInWidth(uint32_t default_num_vertices, road::Lane::LaneType type)
  {
    switch(type)
    {
      case road::Lane::LaneType::Driving:
      case road::Lane::LaneType::Parking:
      case road::Lane::LaneType::Bidirectional:
      {
        return default_num_vertices;
      }
      case road::Lane::LaneType::Shoulder:
      case road::Lane::LaneType::Sidewalk:
      case road::Lane::LaneType::Biking:
      {
        return 6;
      }
      default:
      {
        return 2;
      }
    }
  }

  std::pair<geom::Vector3D, geom::Vector3D> MeshFactory::ComputeEdgesForLanemark(
      const road::LaneSection& lane_section,
      const road::Lane& lane,
      const double s_current,
      const double lanemark_width) const {
    std::pair<geom::Vector3D, geom::Vector3D> edges =
      lane.GetCornerPositions(s_current, road_param.extra_lane_width);

    geom::Vector3D director;
    if (edges.first != edges.second) {
      director = edges.second - edges.first;
      director /= director.Length(); 
    } else {
      const std::map<road::LaneId, road::Lane> & lanes = lane_section.GetLanes();
      for (const auto& lane_pair : lanes) {
        std::pair<geom::Vector3D, geom::Vector3D> another_edge =
          lane_pair.second.GetCornerPositions(s_current, road_param.extra_lane_width);
        if (another_edge.first != another_edge.second) {
          director = another_edge.second - another_edge.first;
          director /= director.Length();
          break;
        }
      }
    }
    geom::Vector3D endmarking = edges.first + director * lanemark_width;
    return std::make_pair(edges.first, endmarking);
  }

} // namespace geom
} // namespace carla
