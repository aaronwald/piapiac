
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

/*
int db()
{
  DockerMgr dm;
  duckdb_result result;
  idx_t row_count = 0;
  idx_t column_count = 0;

  if (!dm.Init())
  {
    exit(EXIT_FAILURE);
  }

  if (!dm.Query("CREATE TABLE integers(i INTEGER, j INTEGER);"))
  {
    goto cleanup;
  }

  if (!dm.Query("INSERT INTO integers VALUES (3, 4), (5, 6), (7, NULL);"))
  {
    goto cleanup;
  }

  if (!dm.Query("SELECT * FROM integers;", &result))
  {
    goto cleanup;
  }

  // print the names of the result
  row_count = duckdb_row_count(&result);
  column_count = duckdb_column_count(&result);
  for (size_t i = 0; i < column_count; i++)
  {
    printf("%s ", duckdb_column_name(&result, i));
  }
  printf("\n");
  // print the data of the result
  for (size_t row_idx = 0; row_idx < row_count; row_idx++)
  {
    for (size_t col_idx = 0; col_idx < column_count; col_idx++)
    {
      char *val = duckdb_value_varchar(&result, col_idx, row_idx);
      printf("%s ", val);
      duckdb_free(val);
    }
    printf("\n");
  }
  // duckdb_print_result(result);
  exit(EXIT_SUCCESS);
cleanup:
  duckdb_destroy_result(&result);
  exit(EXIT_FAILURE);
}*/