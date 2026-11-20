#include "McpViewItems.h"
#include <sstream>
#include <cmath>
#include <cstdio>
#include <map>

// Visual C++ 2008, the game's compiler, has sprintf_s but no snprintf; both take the buffer's size.
#if defined(_MSC_VER)
#define MCP_SNPRINTF sprintf_s
#else
#define MCP_SNPRINTF snprintf
#endif

namespace
{
	typedef picojson::object JObj;
	typedef picojson::array JArr;
	typedef picojson::value JVal;
	typedef std::vector<std::pair<std::string, std::string> > Fields;

	const char* const FIRST[] = { "name", "civ", "civilization", "leader", "owner" };
	const char* const DROPPED[] = { "help", "how", "is_combat", "is_worker" };
	const char* const TICKING_EXACT[] = { "gold", "culture", "faith", "science", "turn", "year", "end turn blocking", "golden age turns left",
		"note", "heals", "falling", "moves", "yields", "near", "nearest", "army" };
	const char* const TICKING_LAST[] = { "progress", "turns left", "turns to grow", "food stored", "score", "influence", "moves left",
		"activity", "needs orders", "turns" };

	template <size_t N> bool In(const char* const (&list)[N], const std::string& s)
	{
		for(size_t i = 0; i < N; ++i) if(s == list[i]) return true;
		return false;
	}

	std::string Spaced(const std::string& key)
	{
		std::string s = key;
		for(size_t i = 0; i < s.size(); ++i) if(s[i] == '_') s[i] = ' ';
		return s;
	}

	std::string Number(double d)
	{
		char buf[64];
		if(d == std::floor(d) && d > -2e9 && d < 2e9) MCP_SNPRINTF(buf, sizeof(buf), "%d", (int)d);
		else MCP_SNPRINTF(buf, sizeof(buf), "%.1f", d);
		return buf;
	}

	std::string Scalar(const JVal& v)
	{
		if(v.is<std::string>()) return v.get<std::string>();
		if(v.is<bool>()) return v.get<bool>() ? "yes" : "no";
		if(v.is<double>()) return Number(v.get<double>());
		if(v.is<picojson::null>()) return "none";
		return v.serialize();
	}

	bool IsList(const JVal& v)
	{
		if(!v.is<JArr>()) return false;
		const JArr& a = v.get<JArr>();
		for(size_t i = 0; i < a.size(); ++i) if(a[i].is<JObj>()) return true;
		return false;
	}

	bool IsEmptyArray(const JVal& v) { return v.is<JArr>() && v.get<JArr>().empty(); }

	std::string Inline(const JVal& v)
	{
		if(v.is<JObj>())
		{
			const JObj& o = v.get<JObj>();
			std::string out;
			for(JObj::const_iterator it = o.begin(); it != o.end(); ++it)
			{
				if(In(DROPPED, it->first)) continue;
				if(!out.empty()) out += ", ";
				out += Spaced(it->first) + " " + Inline(it->second);
			}
			return "{" + out + "}";
		}
		if(v.is<JArr>())
		{
			const JArr& a = v.get<JArr>();
			bool bObjects = false;
			for(size_t i = 0; i < a.size(); ++i) if(a[i].is<JObj>() || a[i].is<JArr>()) bObjects = true;
			std::string out;
			for(size_t i = 0; i < a.size(); ++i) out += (i ? (bObjects ? "; " : ", ") : "") + Inline(a[i]);
			return "[" + out + "]";
		}
		return Scalar(v);
	}

	void AddFields(const std::string& prefix, const JObj& o, Fields& out)
	{
		std::string pre = prefix.empty() ? std::string() : prefix + ".";
		for(size_t i = 0; i < sizeof(FIRST) / sizeof(FIRST[0]); ++i)
		{
			JObj::const_iterator it = o.find(FIRST[i]);
			if(it != o.end() && !it->second.is<JObj>() && !it->second.is<JArr>()) out.push_back(std::make_pair(pre + FIRST[i], Scalar(it->second)));
		}
		JObj::const_iterator x = o.find("x"), y = o.find("y");
		bool bAt = x != o.end() && y != o.end() && x->second.is<double>() && y->second.is<double>();
		if(bAt) out.push_back(std::make_pair(pre + "at", "(" + Number(x->second.get<double>()) + "," + Number(y->second.get<double>()) + ")"));
		bool bNamed = o.find("name") != o.end();
		for(JObj::const_iterator it = o.begin(); it != o.end(); ++it)
		{
			const std::string& k = it->first;
			bool bNested = it->second.is<JObj>() || it->second.is<JArr>();
			if(In(FIRST, k) && !bNested) continue;
			if(In(DROPPED, k) || IsEmptyArray(it->second)) continue;
			if(bAt && (k == "x" || k == "y")) continue;
			if(bNamed && k == "type") continue;
			std::string name = pre + Spaced(k);
			if(it->second.is<JObj>()) AddFields(name, it->second.get<JObj>(), out);
			else out.push_back(std::make_pair(name, Inline(it->second)));
		}
	}

	McpViewItem Item(const std::string& section, const std::string& key, const Fields& fields)
	{
		McpViewItem it;
		it.section = section;
		it.key = key;
		it.fields = fields;
		return it;
	}
}

void McpObjectFields(const picojson::object& o, std::vector<std::pair<std::string, std::string> >& out)
{
	AddFields("", o, out);
}

std::string McpObjectKey(const std::string& section, const picojson::object& o, size_t index)
{
	const char* const ids[] = { "id", "city_id", "unit_id", "player_id" };
	JObj::const_iterator owner = o.find("owner");
	std::string whose = owner != o.end() && owner->second.is<std::string>() ? ":" + owner->second.get<std::string>() : std::string();
	for(size_t i = 0; i < sizeof(ids) / sizeof(ids[0]); ++i)
	{
		JObj::const_iterator it = o.find(ids[i]);
		if(it != o.end() && !it->second.is<JObj>() && !it->second.is<JArr>()) return section + ":" + ids[i] + ":" + Scalar(it->second) + whose;
	}
	JObj::const_iterator x = o.find("x"), y = o.find("y");
	if(x != o.end() && y != o.end()) return section + ":" + Scalar(x->second) + "," + Scalar(y->second);
	const char* const names[] = { "name", "civ", "civilization", "type", "tech", "policy", "branch", "resource" };
	for(size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
	{
		JObj::const_iterator it = o.find(names[i]);
		if(it != o.end() && it->second.is<std::string>()) return section + ":" + it->second.get<std::string>();
	}
	return section + ":#" + Number((double)index);
}

void McpScreenItems(const std::string& section, const picojson::value& result, std::vector<McpViewItem>& items)
{
	if(!result.is<JObj>()) return;
	const JObj& o = result.get<JObj>();
	JObj plain;
	std::vector<std::string> lists;
	for(JObj::const_iterator it = o.begin(); it != o.end(); ++it)
	{
		if(IsList(it->second)) lists.push_back(it->first);
		else if(!IsEmptyArray(it->second)) plain[it->first] = it->second;
	}
	Fields f;
	AddFields("", plain, f);
	if(!f.empty()) items.push_back(Item(section, section + ":summary", f));
	for(size_t i = 0; i < lists.size(); ++i)
	{
		std::string name = section + ": " + Spaced(lists[i]);
		const JArr& a = o.find(lists[i])->second.get<JArr>();
		for(size_t k = 0; k < a.size(); ++k)
		{
			if(!a[k].is<JObj>()) continue;
			Fields lf;
			AddFields("", a[k].get<JObj>(), lf);
			items.push_back(Item(name, McpObjectKey(name, a[k].get<JObj>(), k), lf));
		}
	}
}

void McpUniqueKeys(std::vector<McpViewItem>& items)
{
	std::map<std::string, int> seen;
	for(size_t i = 0; i < items.size(); ++i)
	{
		int n = ++seen[items[i].key];
		if(n > 1) items[i].key += "#" + Number((double)n);
	}
}

bool McpTickingField(const std::string& field)
{
	// "_at" and "at" are the same field; the underscore only says the value is shown without its name.
	std::string name = !field.empty() && field[0] == '_' ? field.substr(1) : field;
	if(In(TICKING_EXACT, name)) return true;
	if(name == "clock" || name.compare(0, 6, "clock ") == 0 || name.compare(0, 8, "sources ") == 0) return true;
	if(name.compare(0, 13, "demographics.") == 0) return true;
	size_t dot = name.rfind('.');
	return In(TICKING_LAST, dot == std::string::npos ? name : name.substr(dot + 1));
}

void McpTrimUnitFields(std::vector<std::pair<std::string, std::string> >& fields)
{
	std::vector<std::pair<std::string, std::string> > kept;
	for(size_t i = 0; i < fields.size(); ++i)
	{
		const std::string& k = fields[i].first;
		const std::string& v = fields[i].second;
		if((k == "can found city here" || k == "embarked" || k == "needs orders") && v == "no") continue;
		if(k == "max hp" && v == "100") continue;
		kept.push_back(fields[i]);
	}
	fields.swap(kept);
}

std::set<std::string> McpTickingFields(const std::vector<McpViewItem>& items)
{
	std::set<std::string> out;
	for(size_t i = 0; i < items.size(); ++i)
		for(size_t k = 0; k < items[i].fields.size(); ++k)
			if(McpTickingField(items[i].fields[k].first)) out.insert(items[i].fields[k].first);
	return out;
}

namespace
{
	std::string Lower(const std::string& s)
	{
		std::string out = s;
		for(size_t i = 0; i < out.size(); ++i) if(out[i] >= 'A' && out[i] <= 'Z') out[i] = (char)(out[i] - 'A' + 'a');
		return out;
	}

	std::string Text(const JObj& o, const char* key)
	{
		JObj::const_iterator it = o.find(key);
		return it != o.end() && it->second.is<std::string>() ? it->second.get<std::string>() : std::string();
	}

	bool Flag(const JObj& o, const char* key)
	{
		JObj::const_iterator it = o.find(key);
		return it != o.end() && it->second.is<bool>() && it->second.get<bool>();
	}

	std::string ShortTiles(const JObj& o, const char* key)
	{
		JObj::const_iterator it = o.find(key);
		if(it == o.end() || !it->second.is<JArr>()) return "[]";
		const JArr& a = it->second.get<JArr>();
		std::string out;
		for(size_t i = 0; i < a.size(); ++i)
			if(a[i].is<JObj>()) out += (out.empty() ? "" : "; ") + McpTileShort(a[i].get<JObj>());
		return "[" + out + "]";
	}
}

std::string McpDamagedLine(const std::string& improvement, const std::string& resource, bool road, int x, int y,
	const std::string& city, bool beingRepaired)
{
	std::string what;
	if(!improvement.empty()) what = (resource.empty() ? std::string() : Lower(resource) + " ") + Lower(improvement);
	if(road) what += (what.empty() ? "" : " and ") + std::string("road");
	std::ostringstream o;
	o << "pillaged " << what << " (" << x << "," << y << ")";
	if(!city.empty()) o << " near " << city;
	o << (beingRepaired ? ": a worker is repairing it" : ": a worker on the tile repairs it (unit_action build, the repair in builds_here)");
	return o.str();
}

std::string McpTileShort(const picojson::object& tile)
{
	std::string name;
	const char* const parts[] = { "resource", "improvement", "feature" };
	for(size_t i = 0; i < sizeof(parts) / sizeof(parts[0]); ++i)
	{
		std::string t = Text(tile, parts[i]);
		if(!t.empty()) name += (name.empty() ? "" : " ") + Lower(t);
	}
	if(Flag(tile, "pillaged")) name += " (pillaged)";
	if(Flag(tile, "hills")) name += (name.empty() ? "" : " ") + std::string("hill");
	if(Flag(tile, "lake")) name += (name.empty() ? "" : " ") + std::string("lake");
	if(name.empty()) name = Lower(Text(tile, "terrain"));
	JObj::const_iterator x = tile.find("x"), y = tile.find("y");
	if(x != tile.end() && y != tile.end()) name += " (" + Scalar(x->second) + "," + Scalar(y->second) + ")";
	JObj::const_iterator yi = tile.find("yields");
	if(yi != tile.end() && yi->second.is<JObj>())
	{
		const JObj& yields = yi->second.get<JObj>();
		const char* const keys[] = { "food", "production", "gold", "science", "culture", "faith" };
		const char* const marks[] = { "f", "p", "g", "s", "c", "fa" };
		for(size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i)
		{
			JObj::const_iterator v = yields.find(keys[i]);
			if(v == yields.end() || !v->second.is<double>() || v->second.get<double>() == 0) continue;
			name += " " + Number(v->second.get<double>()) + marks[i];
		}
	}
	if(Flag(tile, "locked")) name += " locked";
	return name;
}

void McpCitizensFields(const picojson::object& citizens, std::vector<std::pair<std::string, std::string> >& out)
{
	out.push_back(std::make_pair(std::string("works"), ShortTiles(citizens, "worked_tiles")));
	out.push_back(std::make_pair(std::string("free"), ShortTiles(citizens, "workable_tiles")));
	JObj rest = citizens;
	// The city's own line has its size; a default focus, no unemployed and growth not avoided go without saying.
	const char* const gone[] = { "worked_tiles", "workable_tiles", "city", "name", "how", "population", "specialists" };
	for(size_t i = 0; i < sizeof(gone) / sizeof(gone[0]); ++i) rest.erase(gone[i]);
	JObj::iterator it = rest.find("focus");
	if(it != rest.end() && it->second.is<std::string>() && it->second.get<std::string>() == "default") rest.erase(it);
	it = rest.find("unemployed");
	if(it != rest.end() && it->second.is<double>() && it->second.get<double>() == 0) rest.erase(it);
	it = rest.find("avoid_growth");
	if(it != rest.end() && it->second.is<bool>() && !it->second.get<bool>()) rest.erase(it);
	AddFields("", rest, out);
	JObj::const_iterator sp = citizens.find("specialists");
	if(sp != citizens.end() && sp->second.is<JArr>() && !sp->second.get<JArr>().empty())
	{
		std::string list;
		const JArr& a = sp->second.get<JArr>();
		for(size_t i = 0; i < a.size(); ++i)
		{
			if(!a[i].is<JObj>()) continue;
			const JObj& o = a[i].get<JObj>();
			JObj::const_iterator name = o.find("specialist"), count = o.find("count"), max = o.find("max"), building = o.find("building");
			if(name == o.end() || !name->second.is<std::string>()) continue;
			list += (list.empty() ? "" : ", ") + name->second.get<std::string>();
			if(count != o.end() && max != o.end()) list += " " + Scalar(count->second) + "/" + Scalar(max->second);
			if(building != o.end() && building->second.is<std::string>()) list += " (" + building->second.get<std::string>() + ")";
		}
		if(!list.empty()) out.push_back(std::make_pair(std::string("specialists"), list));
	}
}

void McpBranchFields(const picojson::array& branches, std::vector<std::pair<std::string, std::string> >& out)
{
	std::string opened, canOpen, later;
	for(size_t i = 0; i < branches.size(); ++i)
	{
		if(!branches[i].is<JObj>()) continue;
		const JObj& b = branches[i].get<JObj>();
		std::string name = Text(b, "name");
		if(Flag(b, "unlocked"))
		{
			int owned = 0, total = 0;
			std::string left;
			JObj::const_iterator pl = b.find("policies");
			if(pl != b.end() && pl->second.is<JArr>())
				for(size_t k = 0; k < pl->second.get<JArr>().size(); ++k)
				{
					const JVal& pv = pl->second.get<JArr>()[k];
					if(!pv.is<JObj>()) continue;
					++total;
					if(Flag(pv.get<JObj>(), "owned")) ++owned;
					else left += (left.empty() ? "" : ", ") + Text(pv.get<JObj>(), "name");
				}
			std::ostringstream count;
			count << name << " " << owned << "/" << total;
			opened += (opened.empty() ? "" : " \xC2\xB7 ") + count.str() + (left.empty() ? "" : " (left: " + left + ")");
		}
		else if(Flag(b, "opens_with_a_policy")) canOpen += (canOpen.empty() ? "" : ", ") + name;
		else
		{
			std::string era = Text(b, "requires_era");
			later += (later.empty() ? "" : ", ") + name + (era.empty() ? "" : " (" + era + ")");
		}
	}
	if(!opened.empty()) out.push_back(std::make_pair(std::string("opened"), opened));
	if(!canOpen.empty()) out.push_back(std::make_pair(std::string("can open"), canOpen));
	if(!later.empty()) out.push_back(std::make_pair(std::string("later"), later));
}

std::string McpBranchHelpFacts(const std::string& help)
{
	size_t at = help.find("Adopting");
	return at == std::string::npos ? help : help.substr(at);
}
