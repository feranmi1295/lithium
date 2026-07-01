-module(lithium_monitor).
-behaviour(gen_server).

-export([start_link/0]).
-export([init/1, handle_call/3, handle_cast/2, handle_info/2,
         terminate/2, code_change/3]).

-define(CHECK_INTERVAL, 15000). %% check every 15 seconds
-define(DEGRADE_AFTER,  30).    %% seconds before degraded
-define(INACTIVE_AFTER, 60).    %% seconds before inactive

start_link() ->
    gen_server:start_link({local, ?MODULE}, ?MODULE, [], []).

init([]) ->
    io:format("  Heartbeat monitor started~n"),
    erlang:send_after(?CHECK_INTERVAL, self(), check),
    {ok, #{}}.

handle_info(check, State) ->
    check_nodes(),
    erlang:send_after(?CHECK_INTERVAL, self(), check),
    {noreply, State};

handle_info(_Info, State) ->
    {noreply, State}.

handle_call(_Req, _From, State) -> {reply, ok, State}.
handle_cast(_Msg, State)        -> {noreply, State}.
terminate(_Reason, _State)      -> ok.
code_change(_OldVsn, State, _)  -> {ok, State}.

check_nodes() ->
    Now  = erlang:system_time(second),
    Nodes = lithium_registry:all_nodes(),
    lists:foreach(fun(Node) ->
        NodeId   = maps:get(node_id,   Node),
        LastSeen = maps:get(last_seen, Node),
        Status   = maps:get(status,    Node),
        Elapsed  = Now - LastSeen,

        if
            Status =:= active, Elapsed > ?INACTIVE_AFTER ->
                lithium_registry:mark_inactive(NodeId);
            Status =:= active, Elapsed > ?DEGRADE_AFTER ->
                lithium_registry:mark_degraded(NodeId);
            true ->
                ok
        end
    end, Nodes).
