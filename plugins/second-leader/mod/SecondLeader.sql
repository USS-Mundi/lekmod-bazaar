CREATE TABLE IF NOT EXISTS DllPlugins (File TEXT NOT NULL);
INSERT INTO DllPlugins (File) VALUES ('SecondLeader.dll');

-- Second Leader: the setup option and its text. The name starts with a space so the setup screen, which sorts
-- options by name, lists it first.
INSERT INTO GameOptions (Type, Description, Help, "Default", Visible, SupportsSinglePlayer, SupportsMultiplayer)
VALUES ('GAMEOPTION_SECOND_LEADER', 'TXT_KEY_GAMEOPTION_SECOND_LEADER', NULL, 0, 1, 1, 1);

INSERT INTO Language_en_US (Tag, Text) VALUES ('TXT_KEY_GAMEOPTION_SECOND_LEADER', ' Second Leader For Everyone!');

-- A row ('Root', <folder>) moves the pairs record and the agents' file out of My Games\Sid Meier's Civilization 5\Second Leader.
CREATE TABLE IF NOT EXISTS SecondLeaderSettings (Name TEXT PRIMARY KEY, Value TEXT);
