#pragma once
#include "../hash_combine.h"
#include "../types.h"
#include <cstdio>
#include <deko3d.hpp>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include "memory_heap.h"

namespace Deko3D {

class ShaderCache
{
public:
  ~ShaderCache();

  // Shader types
  enum class ShaderType
  {
    Vertex,
    Geometry,
    Fragment,
    Compute
  };

  static void Create(std::string_view base_path, u32 version, bool debug);
  static void Destroy();

  std::optional<std::vector<u8>> GetShaderDKSH(ShaderType type, std::string_view shader_code);
  bool GetShaderModule(ShaderType type, std::string_view shader_code, dk::Shader& shader, MemoryHeap::Allocation& shader_memory);

  bool GetVertexShader(std::string_view shader_code, dk::Shader& shader, MemoryHeap::Allocation& shader_memory);
  bool GetGeometryShader(std::string_view shader_code, dk::Shader& shader, MemoryHeap::Allocation& shader_memory);
  bool GetFragmentShader(std::string_view shader_code, dk::Shader& shader, MemoryHeap::Allocation& shader_memory);
  bool GetComputeShader(std::string_view shader_code, dk::Shader& shader, MemoryHeap::Allocation& shader_memory);

private:
  static constexpr u32 FILE_VERSION = 2;

  struct CacheIndexKey
  {
    u64 source_hash_low;
    u64 source_hash_high;
    u32 source_length;
    ShaderType shader_type;

    bool operator==(const CacheIndexKey& key) const;
    bool operator!=(const CacheIndexKey& key) const;
  };

  struct CacheIndexEntryHasher
  {
    std::size_t operator()(const CacheIndexKey& e) const noexcept
    {
      std::size_t h = 0;
      hash_combine(h, e.source_hash_low, e.source_hash_high, e.source_length, e.shader_type);
      return h;
    }
  };

  struct CacheIndexData
  {
    u32 file_offset;
    u32 blob_size;
  };

  using CacheIndex = std::unordered_map<CacheIndexKey, CacheIndexData, CacheIndexEntryHasher>;

  ShaderCache();

  static std::string GetShaderCacheBaseFileName(const std::string_view& base_path, bool debug);
  static CacheIndexKey GetCacheKey(ShaderType type, const std::string_view& shader_code);

  void Open(std::string_view base_path, u32 version, bool debug);

  bool CreateNewShaderCache(const std::string& index_filename, const std::string& blob_filename);
  bool ReadExistingShaderCache(const std::string& index_filename, const std::string& blob_filename);
  void CloseShaderCache();

  std::optional<std::vector<u8>> CompileAndAddShaderDKSH(const CacheIndexKey& key, std::string_view shader_code);

  std::FILE* m_index_file = nullptr;
  std::FILE* m_blob_file = nullptr;

  CacheIndex m_index;

  u32 m_version = 0;
  bool m_debug = false;
};

} // namespace Deko3D

extern std::unique_ptr<Deko3D::ShaderCache> g_deko3d_shader_cache;
