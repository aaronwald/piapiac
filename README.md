# piapiac

```mermaid
%%{init: {'theme':'neutral'}}%%
graph TD;

grpc-->|epoll| piapiac
piapiac<-->duckdb
piapiac-->|epoll| websocket
tui<-->|grpc| grpc
piapiac<-->|mqtt| mosquitto
```
