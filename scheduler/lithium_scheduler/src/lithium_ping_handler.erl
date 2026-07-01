-module(lithium_ping_handler).
-export([init/2]).

init(Req, State) ->
    Body = <<"{\"status\":\"ok\",\"service\":\"lithium-scheduler\",\"version\":\"1.0.0\"}">>,
    Req2 = cowboy_req:reply(200,
        #{<<"content-type">> => <<"application/json">>},
        Body, Req),
    {ok, Req2, State}.
