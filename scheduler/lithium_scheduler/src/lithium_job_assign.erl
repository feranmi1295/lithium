-module(lithium_job_assign).

-export([assign/1]).

assign(JobId) ->
    case lithium_job_registry:get_job(JobId) of
        {error, not_found} ->
            {error, job_not_found};
        {ok, Job} ->
            case pick_node() of
                {error, no_nodes} ->
                    io:format("  [assign] No available nodes for ~s~n", [JobId]),
                    {error, no_nodes};
                {ok, NodeId} ->
                    Now = erlang:system_time(second),
                    lithium_job_registry:update_job_status(JobId, #{
                        status     => assigned,
                        node_id    => NodeId,
                        started_at => Now
                    }),
                    io:format("  [assign] Job ~s → ~s~n", [JobId, NodeId]),
                    spawn(fun() -> dispatch_to_node(NodeId, JobId, Job) end),
                    {ok, NodeId}
            end
    end.

pick_node() ->
    Nodes  = lithium_registry:all_nodes(),
    Active = lists:filter(fun(N) ->
        maps:get(status, N) =:= active
    end, Nodes),
    case Active of
        [] -> {error, no_nodes};
        _  ->
            Best = lists:foldl(fun(N, Acc) ->
                case Acc of
                    none -> N;
                    _    ->
                        case maps:get(score, N) > maps:get(score, Acc) of
                            true  -> N;
                            false -> Acc
                        end
                end
            end, none, Active),
            {ok, maps:get(node_id, Best)}
    end.

dispatch_to_node(NodeId, JobId, Job) ->
    Runtime  = maps:get(runtime,   Job),
    Command  = maps:get(command,   Job),
    ClientId = maps:get(client_id, Job),

    Body = io_lib:format(
        "{\"job_id\":\"~s\",\"client_id\":\"~s\","
        "\"runtime\":\"~s\",\"command\":\"~s\"}",
        [JobId, ClientId, Runtime, Command]),

    BodyBin = list_to_binary(Body),
    Len     = integer_to_list(byte_size(BodyBin)),

    Request = list_to_binary([
        "POST /job HTTP/1.0\r\n",
        "Host: 127.0.0.1:7701\r\n",
        "Content-Type: application/json\r\n",
        "Content-Length: ", Len, "\r\n",
        "\r\n",
        BodyBin
    ]),

    case gen_tcp:connect({127,0,0,1}, 7701, [binary, {active, false}], 3000) of
        {ok, Sock} ->
            gen_tcp:send(Sock, Request),
            case gen_tcp:recv(Sock, 0, 3000) of
                {ok, Resp} ->
                    case binary:match(Resp, <<"200">>) of
                        nomatch ->
                            io:format("  [dispatch] Node rejected job ~s~n", [JobId]);
                        _ ->
                            io:format("  [dispatch] Node accepted job ~s~n", [JobId])
                    end;
                _ ->
                    io:format("  [dispatch] No response from node~n", [])
            end,
            gen_tcp:close(Sock);
        {error, Reason} ->
            io:format("  [dispatch] Cannot reach node: ~p~n", [Reason]),
            lithium_job_registry:update_job_status(JobId, #{status => failed})
    end.
