-module(lithium_job_assign).

-export([assign/1]).

assign(JobId) ->
    case lithium_job_registry:get_job(JobId) of
        {error, not_found} ->
            {error, job_not_found};
        {ok, Job} ->
            case pick_node() of
                {error, no_nodes} ->
                    io:format("  [assign] No available nodes for job ~s~n", [JobId]),
                    {error, no_nodes};
                {ok, NodeId} ->
                    Now = erlang:system_time(second),
                    lithium_job_registry:update_job_status(JobId, #{
                        status     => assigned,
                        node_id    => NodeId,
                        started_at => Now
                    }),
                    io:format("  [assign] Job ~s assigned to ~s~n", [JobId, NodeId]),
                    dispatch_to_node(NodeId, JobId, Job),
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
                    none ->
                        N;
                    _ ->
                        ScoreN   = maps:get(score, N),
                        ScoreAcc = maps:get(score, Acc),
                        case ScoreN > ScoreAcc of
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
    io:format("  [dispatch] Sending job ~s to ~s~n",          [JobId, NodeId]),
    io:format("  [dispatch] Runtime: ~s | Command: ~s~n",     [Runtime, Command]),
    io:format("  [dispatch] Client: ~s~n",                    [ClientId]),
    spawn(fun() -> simulate_execution(JobId, NodeId, Command) end).

simulate_execution(JobId, NodeId, Command) ->
    timer:sleep(2000),
    Result = list_to_binary(
        io_lib:format("Executed '~s' on ~s", [Command, NodeId])),
    lithium_job_registry:update_job_status(JobId, #{
        status => complete,
        result => Result
    }),
    io:format("  [exec] Job ~s complete on ~s~n", [JobId, NodeId]).
