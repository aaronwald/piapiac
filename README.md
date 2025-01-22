# piapiac

```mermaid
graph TD;

grpc-->|epoll| piapiac
piapiac<-->duckdb
piapiac-->|epoll| websocket
tui<-->|rest?| piapiac
piapiac<-->|mqtt| mosquitto
```
