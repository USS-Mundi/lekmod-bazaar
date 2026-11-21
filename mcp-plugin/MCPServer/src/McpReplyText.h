#pragma once

#include <string>
#include "picojson.h"

// A reply field holding what the model must do next. It travels as the first line of the reply, "DO NOW: ...".
#define MCP_DO_NOW "do_now"

// The text a tool's reply travels as: a plain-text reply as it is, any other as its JSON, led by its do_now line.
std::string McpReplyText(const picojson::value& v);

// A player wrote to the seat and the lines are in the reply: the wait's reply says so after whatever it already asks for
// (on an open turn, turn_view comes first: the view gate refuses say until it is opened).
void McpAddMessageNotice(picojson::object& reply);
