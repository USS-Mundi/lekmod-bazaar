#pragma once

#include <string>

// Pure seat rules, with no game headers, tested natively.

// A seat whose model left waits for it. Only that model takes it back (bOwnLabel: the seat's last player is the
// asking model); a seat on the human's own team the human may hand to any model. An opponent seat never passes to
// another model, whatever it asks for.
bool McpMayTakeLeftSeat(bool bOwnLabel, bool bOnScreenTeam);

// What take_seat tells a model to do next. One sitting down for the first time on a major civilization's seat greets
// the human first (bBack: it took back a seat it left); a city-state's seat greets no one.
std::string McpTakeSeatNext(bool bTurnNow, bool bCityState, bool bBack);
