-module(lithium_pubkey_handler).
-export([init/2]).

init(Req, State) ->
    PubKey = lithium_crypto:get_public_key(),
    Body   = iolist_to_binary(
        io_lib:format("{\"public_key\":\"~s\"}", [PubKey])),
    Req2 = cowboy_req:reply(200,
        #{<<"content-type">> => <<"application/json">>},
        Body, Req),
    {ok, Req2, State}.
