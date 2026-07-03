-module(lithium_scoring).

-export([job_success/1, job_failure/1, job_timeout/1, get_score/1]).

-define(MAX_SCORE,     100).
-define(MIN_SCORE,       0).
-define(SUCCESS_DELTA,   2).
-define(FAILURE_DELTA,  -5).
-define(TIMEOUT_DELTA, -10).
-define(SUSPEND_BELOW,  20).

job_success(NodeId) ->
    adjust_score(NodeId, ?SUCCESS_DELTA, success).

job_failure(NodeId) ->
    adjust_score(NodeId, ?FAILURE_DELTA, failure).

job_timeout(NodeId) ->
    adjust_score(NodeId, ?TIMEOUT_DELTA, timeout).

get_score(NodeId) ->
    case lithium_registry:get_node(NodeId) of
        {ok, Node} -> maps:get(score, Node, 100);
        _          -> 0
    end.

adjust_score(NodeId, Delta, Reason) ->
    case lithium_registry:get_node(NodeId) of
        {error, not_found} ->
            ok;
        {ok, Node} ->
            OldScore = maps:get(score, Node, 100),
            NewScore = max(?MIN_SCORE, min(?MAX_SCORE, OldScore + Delta)),

            %% update jobs_completed counter on success
            JobsDone = maps:get(jobs_completed, Node, 0),
            Updates  = case Reason of
                success ->
                    #{score          => NewScore,
                      jobs_completed => JobsDone + 1};
                _ ->
                    #{score => NewScore}
            end,

            lithium_registry:update_score(NodeId, Updates),

            io:format("  [scoring] ~s: ~p → ~p (~p)~n",
                      [NodeId, OldScore, NewScore, Reason]),

            %% suspend node if score too low
            case NewScore =< ?SUSPEND_BELOW of
                true ->
                    io:format("  [scoring] ~s suspended (score: ~p)~n",
                              [NodeId, NewScore]),
                    lithium_registry:mark_suspended(NodeId);
                false ->
                    ok
            end
    end.
