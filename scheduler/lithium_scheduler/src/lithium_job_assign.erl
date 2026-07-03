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
                {ok, Node} ->
                    NodeId = maps:get(node_id, Node),
                    Now    = erlang:system_time(second),
                    lithium_job_registry:update_job_status(JobId, #{
                        status     => assigned,
                        node_id    => NodeId,
                        started_at => Now
                    }),
                    io:format("  [assign] Job ~s → ~s~n", [JobId, NodeId]),
                    spawn(fun() -> dispatch_to_node(Node, JobId, Job) end),
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
            {ok, Best}
    end.

dispatch_to_node(Node, JobId, Job) ->
    NodeId   = maps:get(node_id,   Node),
    Ip       = maps:get(ip,        Node, <<"127.0.0.1">>),
    Port     = maps:get(port,      Node, <<"7701">>),
    Runtime  = maps:get(runtime,   Job),
    Command  = maps:get(command,   Job),
    ClientId = maps:get(client_id, Job),

    %% convert port to integer
    PortInt = case Port of
        P when is_integer(P) -> P;
        P when is_binary(P)  -> binary_to_integer(P);
        P when is_list(P)    -> list_to_integer(P)
    end,

    %% convert IP to string
    IpStr = case Ip of
        I when is_binary(I) -> binary_to_list(I);
        I when is_list(I)   -> I
    end,

    Body = list_to_binary(io_lib:format(
        "{\"job_id\":\"~s\",\"client_id\":\"~s\","
        "\"runtime\":\"~s\",\"command\":\"~s\"}",
        [JobId, ClientId, Runtime, Command])),

    Len     = integer_to_list(byte_size(Body)),
    Request = list_to_binary([
        "POST /job HTTP/1.0\r\n",
        "Host: ", IpStr, ":", integer_to_list(PortInt), "\r\n",
        "Content-Type: application/json\r\n",
        "Content-Length: ", Len, "\r\n",
        "\r\n",
        Body
    ]),

    io:format("  [dispatch] Sending job ~s to ~s @ ~s:~p~n",
              [JobId, NodeId, IpStr, PortInt]),

    IpTuple = list_to_tuple([list_to_integer(X)
                              || X <- string:tokens(IpStr, ".")]),

    case gen_tcp:connect(IpTuple, PortInt, [binary, {active, false}], 5000) of
        {ok, Sock} ->
            gen_tcp:send(Sock, Request),
            case gen_tcp:recv(Sock, 0, 5000) of
                {ok, Resp} ->
                    case binary:match(Resp, <<"200">>) of
                        nomatch ->
                            io:format("  [dispatch] Node rejected job~n"),
                            lithium_job_registry:update_job_status(JobId, #{status => failed});
                        _ ->
                            io:format("  [dispatch] Node accepted job ~s~n", [JobId])
                    end;
                _ ->
                    io:format("  [dispatch] No response from node~n"),
                    lithium_job_registry:update_job_status(JobId, #{status => failed})
            end,
            gen_tcp:close(Sock);
        {error, Reason} ->
            io:format("  [dispatch] Cannot reach ~s:~p — ~p~n", [IpStr, PortInt, Reason]),
            lithium_job_registry:update_job_status(JobId, #{status => failed})
    end.
