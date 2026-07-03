-module(lithium_scheduler_app).
-behaviour(application).

-export([start/2, stop/1]).

start(_StartType, _StartArgs) ->
    io:format("~n🔋 Lithium Scheduler v1.0.0~n"),
    io:format("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━~n"),

    lithium_db:start(),
    lithium_crypto:start(),
    lithium_registry:start(),
    lithium_job_registry:start(),

    Dispatch = cowboy_router:compile([
        {'_', [
            {"/ping",       lithium_ping_handler,      []},
            {"/heartbeat",  lithium_heartbeat_handler, []},
            {"/nodes",      lithium_nodes_handler,     []},
            {"/jobs",       lithium_job_handler,       []},
            {"/jobs/:id",   lithium_job_handler,       []},
            {"/job_result", lithium_result_handler,    []},
            {"/pubkey",     lithium_pubkey_handler,    []}
        ]}
    ]),

    {ok, _} = cowboy:start_clear(lithium_http,
        [{port, 7700}],
        #{env => #{dispatch => Dispatch}}
    ),

    io:format("  Listening on port 7700~n"),
    io:format("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━~n~n"),

    lithium_scheduler_sup:start_link().

stop(_State) ->
    ok.
