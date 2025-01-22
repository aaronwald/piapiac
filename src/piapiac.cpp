
#include "piapiac.hpp"

namespace eight99bushwick
{
  namespace piapiac
  {
    DockerMgr::DockerMgr() noexcept : _db(NULL), _conn(NULL)
    {
    }

    DockerMgr::~DockerMgr() noexcept
    {
      if (_conn)
      {
        duckdb_disconnect(&_conn);
      }

      if (_db)
      {
        duckdb_close(&_db);
      }
    }

    bool DockerMgr::Init() noexcept
    {
      if (duckdb_open(NULL, &_db) == DuckDBError)
      {
        goto fail;
      }

      if (duckdb_connect(_db, &_conn) == DuckDBError)
      {
        goto fail;
      }

      return true;

    fail:
      return false;
    }

    bool DockerMgr::Query(const std::string &query) noexcept
    {
      return duckdb_query(_conn, query.c_str(), NULL) != DuckDBError;
    }

    bool DockerMgr::Query(const std::string &query, duckdb_result *result) noexcept
    {
      return duckdb_query(_conn, query.c_str(), result) != DuckDBError;
    }

  } // namespace piapiac
} // namespace eight99bushwick
