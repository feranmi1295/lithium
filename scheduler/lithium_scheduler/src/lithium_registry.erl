-module(lithium_registry).

-export([start/0, register_node/5, update_heartbeat/3,
         get_node/1, all_nodes/0, mark_degraded/1,
         mark_inactive/1, mark_suspended/1, update_score/2]).

start() ->
    ets:new(lithium_nodes, [named_table, public, set,
                            {keypos, 1}, {read_concurrency, true}]),
    Nodes = lithium_db:load_nodes(),
    lists:foreach(fun(Node) ->
        NodeId = maps:get(node_id, Node),
        ets:insert(lithium_nodes, {NodeId, Node})
    end, Nodes),
    io:format("  Node registry initialized (~p nodes restored)~n",
              [length(Nodes)]).

register_node(NodeId, PublicKey, Ip, Port, Fingerprint) ->
    Now = erlang:system_time(second),
    Node = #{
        node_id        => NodeId,
        public_key     => PublicKey,
        ip             => Ip,
        port           => Port,
        fingerprint    => Fingerprint,
        registered_at  => Now,
        last_seen      => Now,
        status         => active,
        score          => 100,
        missed_pings   => 0,
        jobs_completed => 0
    },
    ets:insert(lithium_nodes, {NodeId, Node}),
    lithium_db:save_node(NodeId, Node),
    io:format("  [registry] Node registered: ~s @ ~s:~s~n",
              [NodeId, Ip, Port]),
    {ok, NodeId}.

update_heartbeat(NodeId, Ip, Port) ->
    Now = erlang:system_time(second),
    case ets:lookup(lithium_nodes, NodeId) of
        [{NodeId, Node}] ->
            Updated = Node#{
                last_seen    => Now,
                missed_pings => 0,
                status       => active,
                ip           => Ip,
                port         => Port
            },
            ets:insert(lithium_nodes, {NodeId, Updated}),
            lithium_db:save_node(NodeId, Updated),
            {ok, updated};
        [] ->
            {error, not_found}
    end.

update_score(NodeId, Updates) ->
    case ets:lookup(lithium_nodes, NodeId) of
        [{NodeId, Node}] ->
            Updated = maps:merge(Node, Updates),
            ets:insert(lithium_nodes, {NodeId, Updated}),
            lithium_db:save_node(NodeId, Updated),
            {ok, updated};
        [] ->
            {error, not_found}
    end.

get_node(NodeId) ->
    case ets:lookup(lithium_nodes, NodeId) of
        [{NodeId, Node}] -> {ok, Node};
        []               -> {error, not_found}
    end.

all_nodes() ->
    ets:foldl(fun({_Id, Node}, Acc) -> [Node | Acc] end,
              [], lithium_nodes).

mark_degraded(NodeId) ->
    case ets:lookup(lithium_nodes, NodeId) of
        [{NodeId, Node}] ->
            Missed  = maps:get(missed_pings, Node, 0) + 1,
            Updated = Node#{status => degraded, missed_pings => Missed},
            ets:insert(lithium_nodes, {NodeId, Updated}),
            lithium_db:save_node(NodeId, Updated),
            io:format("  [registry] Node degraded: ~s (missed: ~p)~n",
                      [NodeId, Missed]),
            {ok, degraded};
        [] -> {error, not_found}
    end.

mark_inactive(NodeId) ->
    case ets:lookup(lithium_nodes, NodeId) of
        [{NodeId, Node}] ->
            Updated = Node#{status => inactive},
            ets:insert(lithium_nodes, {NodeId, Updated}),
            lithium_db:save_node(NodeId, Updated),
            io:format("  [registry] Node inactive: ~s~n", [NodeId]),
            {ok, inactive};
        [] -> {error, not_found}
    end.

mark_suspended(NodeId) ->
    case ets:lookup(lithium_nodes, NodeId) of
        [{NodeId, Node}] ->
            Updated = Node#{status => suspended},
            ets:insert(lithium_nodes, {NodeId, Updated}),
            lithium_db:save_node(NodeId, Updated),
            io:format("  [registry] Node suspended: ~s~n", [NodeId]),
            {ok, suspended};
        [] -> {error, not_found}
    end.
