-module(lithium_nodes_handler).
-export([init/2]).

init(Req, State) ->
    Nodes = lithium_registry:all_nodes(),
    Lines = lists:map(fun(Node) ->
        NodeId  = maps:get(node_id, Node),
        Status  = maps:get(status,  Node),
        Score   = maps:get(score,   Node),
        Missed  = maps:get(missed_pings, Node),
        LastSeen = maps:get(last_seen, Node),
        io_lib:format(
            "{\"node_id\":\"~s\",\"status\":\"~s\",\"score\":~p,\"missed_pings\":~p,\"last_seen\":~p}",
            [NodeId, Status, Score, Missed, LastSeen])
    end, Nodes),
    Joined = string:join(Lines, ","),
    Body   = list_to_binary("[" ++ Joined ++ "]"),
    Req2   = cowboy_req:reply(200,
        #{<<"content-type">> => <<"application/json">>},
        Body, Req),
    {ok, Req2, State}.
