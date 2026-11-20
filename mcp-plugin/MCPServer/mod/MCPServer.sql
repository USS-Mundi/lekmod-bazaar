CREATE TABLE IF NOT EXISTS DllPlugins (File TEXT NOT NULL);
INSERT INTO DllPlugins (File) VALUES ('MCPServer.dll');

-- City-states an ally switches on (README: "City-states an ally can switch on"). What a switched-on city-state
-- allows its ally scales with the ally's influence; every number and line of text here can be changed.

-- From MinInfluence up to the next row: the share of its military units it sends out of its own land (at least
-- SendMin, never its last unit), whether what its units see stays uncovered on the ally's map, and whether its
-- units may enter the ally's land.
CREATE TABLE IF NOT EXISTS McpCityStateTiers (MinInfluence INTEGER PRIMARY KEY, SendPercent INTEGER NOT NULL, SendMin INTEGER NOT NULL, RevealSight BOOLEAN NOT NULL, EnterAllyLand BOOLEAN NOT NULL);
INSERT OR REPLACE INTO McpCityStateTiers (MinInfluence, SendPercent, SendMin, RevealSight, EnterAllyLand) VALUES (60, 0, 0, 0, 0);
INSERT OR REPLACE INTO McpCityStateTiers (MinInfluence, SendPercent, SendMin, RevealSight, EnterAllyLand) VALUES (70, 10, 1, 0, 0);
INSERT OR REPLACE INTO McpCityStateTiers (MinInfluence, SendPercent, SendMin, RevealSight, EnterAllyLand) VALUES (80, 20, 1, 1, 0);
INSERT OR REPLACE INTO McpCityStateTiers (MinInfluence, SendPercent, SendMin, RevealSight, EnterAllyLand) VALUES (90, 30, 2, 1, 0);
INSERT OR REPLACE INTO McpCityStateTiers (MinInfluence, SendPercent, SendMin, RevealSight, EnterAllyLand) VALUES (100, 40, 2, 1, 1);
INSERT OR REPLACE INTO McpCityStateTiers (MinInfluence, SendPercent, SendMin, RevealSight, EnterAllyLand) VALUES (110, 50, 3, 1, 1);
INSERT OR REPLACE INTO McpCityStateTiers (MinInfluence, SendPercent, SendMin, RevealSight, EnterAllyLand) VALUES (120, 60, 3, 1, 1);
INSERT OR REPLACE INTO McpCityStateTiers (MinInfluence, SendPercent, SendMin, RevealSight, EnterAllyLand) VALUES (130, 70, 4, 1, 1);
INSERT OR REPLACE INTO McpCityStateTiers (MinInfluence, SendPercent, SendMin, RevealSight, EnterAllyLand) VALUES (140, 80, 4, 1, 1);
INSERT OR REPLACE INTO McpCityStateTiers (MinInfluence, SendPercent, SendMin, RevealSight, EnterAllyLand) VALUES (150, 90, 5, 1, 1);

-- What the city-state's player reads about its ally, from MinInfluence up to the next row.
CREATE TABLE IF NOT EXISTS McpCityStateDispositions (MinInfluence INTEGER PRIMARY KEY, Name TEXT NOT NULL, Text TEXT NOT NULL);
INSERT OR REPLACE INTO McpCityStateDispositions (MinInfluence, Name, Text) VALUES (60, 'Ally', 'They lead your court, but only just. Talk freely, tell them what you see near your land, and keep your army home; what you send out is a courtesy, and costly orders deserve something back.');
INSERT OR REPLACE INTO McpCityStateDispositions (MinInfluence, Name, Text) VALUES (90, 'Trusted ally', 'They have proved themselves. Take risks for them within what you can send out, and tell them who else is courting you.');
INSERT OR REPLACE INTO McpCityStateDispositions (MinInfluence, Name, Text) VALUES (120, 'Devoted ally', 'Their cause is yours. Act for them without waiting to be asked, and turn rival envoys away.');
INSERT OR REPLACE INTO McpCityStateDispositions (MinInfluence, Name, Text) VALUES (150, 'Sworn', 'You would fall for them. Their enemies are yours, your secrets are theirs, and no rival''s gold moves you.');

-- SWITCHED_ON_INFLUENCE_COST: influence the ally loses each turn, on top of the normal decay, while a model plays it.
-- CONTESTED_LEAD / SECURE_LEAD: the ally's lead over the runner-up below which its loyalty is in play, and at which
-- no one is close. TREND_TURNS / TREND_AMOUNT: a change of this much influence over this many turns reads as rising
-- or falling.
CREATE TABLE IF NOT EXISTS McpCityStateSettings (Name TEXT PRIMARY KEY, Value INTEGER NOT NULL);
INSERT OR REPLACE INTO McpCityStateSettings (Name, Value) VALUES ('MCP_CITY_STATE_SWITCHED_ON_INFLUENCE_COST', 1);
INSERT OR REPLACE INTO McpCityStateSettings (Name, Value) VALUES ('MCP_CITY_STATE_CONTESTED_LEAD', 20);
INSERT OR REPLACE INTO McpCityStateSettings (Name, Value) VALUES ('MCP_CITY_STATE_SECURE_LEAD', 60);
INSERT OR REPLACE INTO McpCityStateSettings (Name, Value) VALUES ('MCP_CITY_STATE_TREND_TURNS', 5);
INSERT OR REPLACE INTO McpCityStateSettings (Name, Value) VALUES ('MCP_CITY_STATE_TREND_AMOUNT', 15);
