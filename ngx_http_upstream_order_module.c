/*
 * Copyright (c) 2013, FengGu <flygoast@gmail.com>
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>


typedef struct {
    struct sockaddr     *sockaddr;
    socklen_t            socklen;
    ngx_str_t            name;
    ngx_uint_t           down;      /* unsigned  down:1; */
} ngx_http_upstream_order_peer_t;


typedef struct {
    ngx_uint_t                        number;

    unsigned                          single:1;

    ngx_str_t                        *name;

    ngx_http_upstream_order_peer_t    peer[1];
} ngx_http_upstream_order_peers_t;


typedef struct {
    ngx_http_upstream_order_peers_t  *peers;
    ngx_uint_t                        index;
} ngx_http_upstream_order_peer_data_t;


static ngx_int_t ngx_http_upstream_init_order(ngx_conf_t *cf,
    ngx_http_upstream_srv_conf_t *us);
static ngx_int_t ngx_http_upstream_init_order_peer(ngx_http_request_t *r,
    ngx_http_upstream_srv_conf_t *us);
static ngx_int_t ngx_http_upstream_get_order_peer(ngx_peer_connection_t *pc,
    void *data);
static void ngx_http_upstream_free_order_peer(ngx_peer_connection_t *pc,
    void *data, ngx_uint_t state);
static char *ngx_http_upstream_order(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);


static ngx_command_t  ngx_http_upstream_order_commands[] = {

    { ngx_string("order"),
      NGX_HTTP_UPS_CONF|NGX_CONF_NOARGS,
      ngx_http_upstream_order,
      0,
      0,
      NULL },

      ngx_null_command
};


static ngx_http_module_t  ngx_http_upstream_order_module_ctx = {
    NULL,                                  /* preconfiguration */
    NULL,                                  /* postconfiguration */

    NULL,                                  /* create main configuration */
    NULL,                                  /* init main configuration */

    NULL,                                  /* create server configuration */
    NULL,                                  /* merge server configuration */

    NULL,                                  /* create location configuration */
    NULL                                   /* merge location configuration */
};


ngx_module_t  ngx_http_upstream_order_module = {
    NGX_MODULE_V1,
    &ngx_http_upstream_order_module_ctx,   /* module context */
    ngx_http_upstream_order_commands,      /* module directives */
    NGX_HTTP_MODULE,                       /* module type */
    NULL,                                  /* init master */
    NULL,                                  /* init module */
    NULL,                                  /* init process */
    NULL,                                  /* init thread */
    NULL,                                  /* exit thread */
    NULL,                                  /* exit process */
    NULL,                                  /* exit master */
    NGX_MODULE_V1_PADDING
};


static char *
ngx_http_upstream_order(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_upstream_srv_conf_t  *uscf;

    uscf = ngx_http_conf_get_module_srv_conf(cf, ngx_http_upstream_module);

    uscf->peer.init_upstream = ngx_http_upstream_init_order;

    uscf->flags = NGX_HTTP_UPSTREAM_CREATE
                  |NGX_HTTP_UPSTREAM_DOWN;

    return NGX_CONF_OK;
}


static ngx_int_t
ngx_http_upstream_init_order(ngx_conf_t *cf, ngx_http_upstream_srv_conf_t *us)
{
    ngx_uint_t                        i, j, n;
    ngx_http_upstream_server_t       *server;
    ngx_http_upstream_order_peers_t  *peers;

    us->peer.init = ngx_http_upstream_init_order_peer;

    if (us->servers == NULL) {
        return NGX_ERROR;
    }

    server = us->servers->elts;

    n = 0;

    for (i = 0; i < us->servers->nelts; i++) {
        n += server[i].naddrs;
    }

    if (n == 0) {
        return NGX_ERROR;
    }

    peers = ngx_pcalloc(cf->pool, sizeof(ngx_http_upstream_order_peers_t) +
                              sizeof(ngx_http_upstream_order_peer_t) * (n - 1));
    if (peers == NULL) {
        return NGX_ERROR;
    }

    peers->single = (n == 1);
    peers->number = n;
    peers->name = &us->host;
    
    n = 0;

    for (i = 0; i < us->servers->nelts; i++) {
        for (j = 0; j < server[i].naddrs; j++) {
            peers->peer[n].sockaddr = server[i].addrs[j].sockaddr;
            peers->peer[n].socklen = server[i].addrs[j].socklen;
            peers->peer[n].name = server[i].addrs[j].name;
            peers->peer[n].down = server[i].down;

            n++;
        }
    }

    us->peer.data = peers;

    return NGX_OK;
}


static ngx_int_t
ngx_http_upstream_init_order_peer(ngx_http_request_t *r,
    ngx_http_upstream_srv_conf_t *us)
{
    ngx_http_upstream_order_peer_data_t  *op;

    op = r->upstream->peer.data;

    if (op == NULL) {
        op = ngx_palloc(r->pool, sizeof(ngx_http_upstream_order_peer_data_t));
        if (op == NULL) {
            return NGX_ERROR;
        }

        r->upstream->peer.data = op;
    }

    op->peers = us->peer.data;
    op->index = 0;

    r->upstream->peer.free = ngx_http_upstream_free_order_peer;
    r->upstream->peer.get = ngx_http_upstream_get_order_peer;
    r->upstream->peer.tries = op->peers->number;

    return NGX_OK;
}


static ngx_int_t
ngx_http_upstream_get_order_peer(ngx_peer_connection_t *pc, void *data)
{
    ngx_http_upstream_order_peer_data_t  *op = data;
    ngx_http_upstream_order_peer_t       *peer;

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, pc->log, 0,
                   "upstream_order: get order peer, try: %ui", pc->tries);

    if (op->peers->single) {
        peer = &op->peers->peer[0];

        if (peer->down) {
            goto failed;
        }

    } else {

        peer = &op->peers->peer[op->index++];

        if (peer == NULL) {
            goto failed;
        }

        ngx_log_debug2(NGX_LOG_DEBUG_HTTP, pc->log, 0,
                       "upstream_order: choose peer %ui for tries %ui",
                       index, pc->tries);
    }

    pc->sockaddr = peer->sockaddr;
    pc->socklen = peer->socklen;
    pc->name = &peer->name;

    return NGX_OK;

failed:

    pc->name = op->peers->name;

    return NGX_BUSY;
}


static void 
ngx_http_upstream_free_order_peer(ngx_peer_connection_t *pc,
    void *data, ngx_uint_t state)
{
    ngx_http_upstream_order_peer_data_t  *op = data;

    ngx_log_debug2(NGX_LOG_DEBUG_HTTP, pc->log, 0,
                  "upstream_order: free peer %ui %ui", pc->tries, state);

    if (state == 0 && pc->tries == 0) {
        return;
    }

    if (op->peers->single) {
        pc->tries = 0;
        return;
    }

    if (pc->tries) {
        pc->tries--;
    }
}
