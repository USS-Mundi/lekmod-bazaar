#pragma once

// Pure seat rules, with no game headers, tested natively.

// A seat whose model left waits for it. Only that model takes it back (bOwnLabel: the seat's last player is the
// asking model); a seat on the human's own team the human may hand to any model. An opponent seat never passes to
// another model, whatever it asks for.
bool McpMayTakeLeftSeat(bool bOwnLabel, bool bOnScreenTeam);
