-module(lithium_nodes_handler).
-export([init/2]).

init(Req, State) ->
    Nodes = lithium_registry:all_nodes(),
    Lines = lists:map(fun(Node) ->
        NodeId   = maps:get(node_id,        Node),
        Status   = maps:get(status,         Node),
        Score    = maps:get(score,          Node),
        Missed   = maps:get(missed_pings,   Node),
        LastSeen = maps:get(last_seen,      Node),
        Jobs     = maps:get(jobs_completed, Node, 0),
        Ip       = maps:get(ip,             Node, <<"unknown">>),
        Port     = maps:get(port,           Node, <<"7701">>),
        io_lib:format(
            "{\"node_id\":\"~s\",\"status\":\"~s\",\"score\":~p,"
            "\"jobs_completed\":~p,\"missed_pings\":~p,"
            "\"last_seen\":~p,\"ip\":\"~s\",\"port\":\"~s\"}",
            [NodeId, Status, Score, Jobs, Missed, LastSeen, Ip, Port])
    end, Nodes),
    Joined = string:join(Lines, ","),
    Body   = list_to_binary("[" ++ Joined ++ "]"),
    Req2   = cowboy_req:reply(200,
        #{<<"content-type">> => <<"application/json">>},
        Body, Req),
    {ok, Req2, State}.
