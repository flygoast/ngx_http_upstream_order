# ngx_http_upstream_order_module

## Introduction

The **ngx_http_upstream_order_module** simplely attempt to access upstreams
according to the order of apearance in upstream conf. If one host have multi
ips, they will be accssed according to the order in resolved data structure.

## Synopsis

    upstream backend {
        server server1;
        server server2;
        order;
    }


## Diretives

* **syntax**: ***order***
* **default**: --
* **context**: upstream

Enables upstream order.


## Status

This module is compatible with following nginx releases:
- 1.2.7

## Author

FengGu <flygoast@gmail.com>
