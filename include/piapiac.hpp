#pragma once

#include <duckdb.h>
#include <string>

namespace eight99bushwick
{
  namespace piapiac
  {
    class DuckDBMgr
    {
    public:
      DuckDBMgr(const std::string &path) noexcept;
      virtual ~DuckDBMgr() noexcept;
      DuckDBMgr(const DuckDBMgr &) = delete;
      DuckDBMgr &operator=(const DuckDBMgr &) = delete;
      DuckDBMgr(DuckDBMgr &&) = delete;
      DuckDBMgr &operator=(DuckDBMgr &&) = delete;

      bool Init() noexcept;
      void Destroy() noexcept;
      bool Query(const std::string &query) noexcept;
      bool Query(const std::string &query, duckdb_result *result) noexcept;

    private:
      duckdb_database _db;
      duckdb_connection _conn;
      std::string _path;
    };
  } // namespace piapiac
}
