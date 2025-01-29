#pragma once

#include <duckdb.h>
#include <string>

namespace eight99bushwick
{
  namespace piapiac
  {
    class DuckDBMgr
    {
    private:
      duckdb_database _db;
      duckdb_connection _conn;

    public:
      DuckDBMgr() noexcept;
      virtual ~DuckDBMgr() noexcept;
      DuckDBMgr(const DuckDBMgr &) = delete;
      DuckDBMgr &operator=(const DuckDBMgr &) = delete;
      DuckDBMgr(DuckDBMgr &&) = delete;
      DuckDBMgr &operator=(DuckDBMgr &&) = delete;

      bool Init() noexcept;
      void Destroy() noexcept;
      bool Query(const std::string &query) noexcept;
      bool Query(const std::string &query, duckdb_result *result) noexcept;
    };
  } // namespace piapiac
}
