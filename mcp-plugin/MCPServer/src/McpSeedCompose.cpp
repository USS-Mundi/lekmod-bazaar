#include "McpSeedCompose.h"
#include <algorithm>
#include <sstream>
#include "McpSeedLibraryData.inc"

const char* const kMcpSeedTraits[MCP_SEED_TRAITS] = { "victory", "wonder", "minor", "boldness", "friendship", "loyalty", "neediness", "balance",
	"forgiveness", "warhate", "meanness", "denounce" };
const char* const kMcpSeedColumns[MCP_SEED_TRAITS] = { "VictoryCompetitiveness", "WonderCompetitiveness", "MinorCivCompetitiveness", "Boldness",
	"DoFWillingness", "Loyalty", "Neediness", "DiploBalance", "Forgiveness", "WarmongerHate", "Meanness", "DenounceWillingness" };

namespace
{
	const int FAMILY[MCP_SEED_TRAITS] = { 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2 };   // appetite, bonds, reaction
	const size_t WORD_CAP = 230;
	enum Class { NONE, BEDROCK, PLAIN, FAULT, BORN };
	const char* const CLASS_NAME[] = { "", "bedrock", "plain", "fault", "born" };

	typedef unsigned int U32;

	U32 Rotr(U32 x, int n) { return (x >> n) | (x << (32 - n)); }

	const U32 K256[64] = {
		0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
		0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
		0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
		0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
		0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
		0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2 };

	struct Pool
	{
		const std::vector<McpSeedLine>* lines;
		Pool() : lines(NULL) {}
	};

	std::string KeyOf(const std::string& trait, const std::string& pole, const std::string& form) { return trait + " " + pole + " " + form; }

	const std::vector<McpSeedLine>* Find(const McpSeedLibrary& lib, const std::string& trait, const std::string& pole, const std::string& form)
	{
		McpSeedLibrary::const_iterator it = lib.find(KeyOf(trait, pole, form));
		return it != lib.end() && !it->second.empty() ? &it->second : NULL;
	}

	size_t Words(const std::string& s)
	{
		size_t n = 0;
		bool in = false;
		for(size_t i = 0; i < s.size(); ++i)
		{
			bool space = s[i] == ' ' || s[i] == '\n' || s[i] == '\t' || s[i] == '\r';
			if(!space && !in) ++n;
			in = !space;
		}
		return n;
	}

	std::string Replaced(std::string s, const std::string& what, const std::string& with)
	{
		for(size_t at = s.find(what); at != std::string::npos; at = s.find(what, at + with.size())) s.replace(at, what.size(), with);
		return s;
	}

	// One deal: the classes of the twelve traits and everything the page is built from.
	struct Deal
	{
		const McpSeedInput& in;
		const McpSeedLibrary& lib;
		bool two;
		Class cls[MCP_SEED_TRAITS];
		char pole[MCP_SEED_TRAITS];
		char tabled[MCP_SEED_TRAITS];     // '+', '-' or 0
		std::string err;

		Deal(const McpSeedInput& i, const McpSeedLibrary& l) : in(i), lib(l), two(!i.b.empty())
		{
			for(int t = 0; t < MCP_SEED_TRAITS; ++t) { cls[t] = NONE; pole[t] = 0; tabled[t] = 0; }
		}

		unsigned long long H(const std::string& a, const std::string& b) const { return McpSeedHash(in.dealKey + ":" + a + ":" + b); }
		int Mean2(int t) const { return two ? in.a[t] + in.b[t] : 2 * in.a[t]; }     // twice the trait's value
		int Strength2(int t) const { int d = Mean2(t) - 10; return d < 0 ? -d : d; }
		int Gap(int t) const { int d = in.a[t] - in.b[t]; return d < 0 ? -d : d; }

		void Classify()
		{
			for(int t = 0; t < MCP_SEED_TRAITS; ++t)
			{
				int x = in.a[t];
				if(!two)
				{
					int moved = in.base.empty() ? 0 : x - in.base[t];
					if((moved >= 2 && x >= 6) || (moved <= -2 && x <= 4)) { cls[t] = BORN; pole[t] = moved > 0 ? '+' : '-'; }
					else if(x >= 7) { cls[t] = PLAIN; pole[t] = '+'; }
					else if(x <= 3) { cls[t] = PLAIN; pole[t] = '-'; }
					continue;
				}
				int y = in.b[t], gap = Gap(t), hi = std::max(x, y), lo = std::min(x, y);
				if(gap >= 4 && hi >= 6 && lo <= 4) { cls[t] = FAULT; pole[t] = '~'; }
				else if(gap <= 2 && x + y >= 13) { cls[t] = BEDROCK; pole[t] = '+'; }
				else if(gap <= 2 && x + y <= 7) { cls[t] = BEDROCK; pole[t] = '-'; }
				else if(hi >= 7 && gap < 4) { cls[t] = PLAIN; pole[t] = '+'; }
				else if(lo <= 3 && gap < 4) { cls[t] = PLAIN; pole[t] = '-'; }
			}
		}

		bool Pick(int t, char p, const char* form, size_t n, std::vector<std::string>& ids, std::string& text)
		{
			const std::vector<McpSeedLine>* pool = Find(lib, kMcpSeedTraits[t], std::string(1, p), form);
			if(!pool) { err = std::string("no signed ") + kMcpSeedTraits[t] + " " + p + " " + form + " line in the library"; return false; }
			size_t at = std::string(form) == "fault" ? n % pool->size() : (size_t)((H(kMcpSeedTraits[t], form) + n) % pool->size());
			ids.push_back((*pool)[at].id);
			text = (*pool)[at].text;
			return true;
		}

		bool Frame(const char* id, std::string& text)
		{
			const std::vector<McpSeedLine>* pool = Find(lib, "-", "-", "frame");
			for(size_t i = 0; pool && i < pool->size(); ++i) if((*pool)[i].id == id) { text = (*pool)[i].text; return true; }
			err = std::string("no signed frame line ") + id + " in the library";
			return false;
		}
	};

	struct ByStated
	{
		const Deal* d;
		bool operator()(int a, int b) const
		{
			int ba = d->cls[a] == BEDROCK ? 0 : 1, bb = d->cls[b] == BEDROCK ? 0 : 1;
			if(ba != bb) return ba < bb;
			if(d->Strength2(a) != d->Strength2(b)) return d->Strength2(a) > d->Strength2(b);
			return a < b;
		}
	};

	struct ByFault
	{
		const Deal* d;
		bool operator()(int a, int b) const
		{
			if(d->Gap(a) != d->Gap(b)) return d->Gap(a) > d->Gap(b);
			return d->H(kMcpSeedTraits[a], "tie") < d->H(kMcpSeedTraits[b], "tie");
		}
	};

	struct ByFree
	{
		const Deal* d;
		bool operator()(int a, int b) const
		{
			if(d->Strength2(a) != d->Strength2(b)) return d->Strength2(a) < d->Strength2(b);
			return d->H(kMcpSeedTraits[a], "unw") < d->H(kMcpSeedTraits[b], "unw");
		}
	};

	bool Has(const std::vector<int>& v, int t) { return std::find(v.begin(), v.end(), t) != v.end(); }

	std::string Spaced(const std::vector<std::string>& parts)
	{
		std::string out;
		for(size_t i = 0; i < parts.size(); ++i) out += (i ? " " : "") + parts[i];
		return out;
	}
}

std::string McpSha256Hex(const std::string& data)
{
	U32 h[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
	std::string m = data;
	unsigned long long bits = (unsigned long long)data.size() * 8;
	m += (char)0x80;
	while(m.size() % 64 != 56) m += (char)0;
	for(int i = 7; i >= 0; --i) m += (char)((bits >> (i * 8)) & 0xff);
	for(size_t off = 0; off < m.size(); off += 64)
	{
		U32 w[64];
		for(int i = 0; i < 16; ++i)
			w[i] = ((U32)(unsigned char)m[off + i * 4] << 24) | ((U32)(unsigned char)m[off + i * 4 + 1] << 16)
				| ((U32)(unsigned char)m[off + i * 4 + 2] << 8) | (U32)(unsigned char)m[off + i * 4 + 3];
		for(int i = 16; i < 64; ++i)
		{
			U32 s0 = Rotr(w[i - 15], 7) ^ Rotr(w[i - 15], 18) ^ (w[i - 15] >> 3), s1 = Rotr(w[i - 2], 17) ^ Rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
			w[i] = w[i - 16] + s0 + w[i - 7] + s1;
		}
		U32 a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
		for(int i = 0; i < 64; ++i)
		{
			U32 t1 = hh + (Rotr(e, 6) ^ Rotr(e, 11) ^ Rotr(e, 25)) + ((e & f) ^ (~e & g)) + K256[i] + w[i];
			U32 t2 = (Rotr(a, 2) ^ Rotr(a, 13) ^ Rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
			hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
		}
		h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
	}
	static const char HEX[] = "0123456789abcdef";
	std::string out;
	for(int i = 0; i < 8; ++i)
		for(int k = 28; k >= 0; k -= 4) out += HEX[(h[i] >> k) & 0xf];
	return out;
}

unsigned long long McpSeedHash(const std::string& joined)
{
	std::string hex = McpSha256Hex(joined);
	unsigned long long v = 0;
	for(size_t i = 0; i < 12; ++i) v = v * 16 + (unsigned long long)(hex[i] <= '9' ? hex[i] - '0' : hex[i] - 'a' + 10);
	return v;
}

int McpSeedRoll(int base, const std::string& key, const std::string& leader, const std::string& trait, int lo, int hi)
{
	int v = base + (int)(McpSeedHash(key + ":" + leader + ":" + trait) % 5) - 2;
	return v < lo ? lo : v > hi ? hi : v;
}

void McpSeedLibraryParse(const std::string& text, bool unsignedToo, McpSeedLibrary& lib)
{
	std::istringstream lines(text);
	std::string line;
	while(std::getline(lines, line))
	{
		if(!line.empty() && line[line.size() - 1] == '\r') line.erase(line.size() - 1);
		if(line.empty() || line[0] == '#' || line.find_first_not_of(" \t") == std::string::npos) continue;
		std::string col[5];
		size_t at = 0;
		bool ok = true;
		for(int c = 0; c < 5 && ok; ++c)
		{
			at = line.find_first_not_of(" \t", at);
			if(at == std::string::npos) { ok = false; break; }
			size_t end = line.find_first_of(" \t", at);
			if(end == std::string::npos) end = line.size();
			col[c] = line.substr(at, end - at);
			at = end;
		}
		if(!ok) continue;
		at = line.find_first_not_of(" \t", at);
		if(at == std::string::npos) continue;
		if(col[0] != "ok" && !unsignedToo) continue;
		McpSeedLine l;
		l.id = col[4];
		l.text = line.substr(at);
		lib[KeyOf(col[1], col[2], col[3])].push_back(l);
	}
}

const McpSeedLibrary& McpSeedLibraryBuiltIn()
{
	static McpSeedLibrary lib;
	if(lib.empty())
	{
		std::string text;
		for(size_t i = 0; i < sizeof(kSeedLibrary) / sizeof(kSeedLibrary[0]); ++i) text += std::string(kSeedLibrary[i]) + "\n";
		McpSeedLibraryParse(text, false, lib);
	}
	return lib;
}

bool McpComposeSeed(const McpSeedInput& in, const McpSeedLibrary& lib, std::string& page, McpSeedRecord& rec, std::string& err)
{
	rec = McpSeedRecord();
	page.clear();
	if(in.a.size() != MCP_SEED_TRAITS || (!in.b.empty() && in.b.size() != MCP_SEED_TRAITS) || (!in.base.empty() && in.base.size() != MCP_SEED_TRAITS)
		|| (!in.table.empty() && in.table.size() != MCP_SEED_TRAITS))
	{
		err = "a sheet needs twelve values, in the order of kMcpSeedTraits";
		return false;
	}
	Deal d(in, lib);
	d.Classify();

	std::vector<int> stated;
	for(int t = 0; t < MCP_SEED_TRAITS; ++t) if(d.cls[t] == BEDROCK || d.cls[t] == PLAIN || d.cls[t] == BORN) stated.push_back(t);
	if(!in.table.empty() && stated.size() < 2)     // a flat sheet: say where it stands at this table
		for(int t = 0; t < MCP_SEED_TRAITS; ++t)
		{
			const std::vector<int>& others = in.table[t];
			if(d.cls[t] != NONE || others.empty()) continue;
			int mine2 = d.Mean2(t), n = (int)others.size(), sum = 0, hi = others[0], lo = others[0];
			for(int k = 0; k < n; ++k) { sum += others[k]; hi = std::max(hi, others[k]); lo = std::min(lo, others[k]); }
			// mine against the table's mean with mine in it, by 1.5 or more; every value here is doubled
			if(mine2 > hi && mine2 * n - sum >= 3 * (n + 1)) d.tabled[t] = '+';
			else if(mine2 < lo && sum - mine2 * n >= 3 * (n + 1)) d.tabled[t] = '-';
			if(d.tabled[t]) stated.push_back(t);
		}
	ByStated byStated; byStated.d = &d;
	std::stable_sort(stated.begin(), stated.end(), byStated);
	std::vector<int> cut;
	if(stated.size() > 5) { cut.assign(stated.begin() + 5, stated.end()); stated.resize(5); }
	std::sort(stated.begin(), stated.end());

	std::vector<int> faults, chosen;
	for(int t = 0; t < MCP_SEED_TRAITS; ++t) if(d.cls[t] == FAULT) faults.push_back(t);
	ByFault byFault; byFault.d = &d;
	std::stable_sort(faults.begin(), faults.end(), byFault);
	for(size_t i = 0; i < faults.size(); ++i)      // at most 2, from different families where possible
	{
		bool fresh = true;
		for(size_t k = 0; k < chosen.size(); ++k) if(FAMILY[chosen[k]] == FAMILY[faults[i]]) fresh = false;
		if(chosen.size() < 2 && fresh) chosen.push_back(faults[i]);
	}
	for(size_t i = 0; i < faults.size(); ++i) if(chosen.size() < 2 && !Has(chosen, faults[i])) chosen.push_back(faults[i]);
	for(size_t i = 0; i < faults.size(); ++i) if(!Has(chosen, faults[i])) cut.push_back(faults[i]);
	std::sort(chosen.begin(), chosen.end());

	std::vector<int> free, unwritten;
	for(int t = 0; t < MCP_SEED_TRAITS; ++t) if(FAMILY[t] > 0 && d.cls[t] == NONE && !d.tabled[t]) free.push_back(t);
	ByFree byFree; byFree.d = &d;
	std::stable_sort(free.begin(), free.end(), byFree);
	for(size_t i = 0; i < free.size() && unwritten.size() + chosen.size() < 2; ++i) unwritten.push_back(free[i]);

	for(;;)
	{
		rec.ids.clear();
		std::string opener, text;
		if(!d.Frame(in.second.empty() ? "open1" : chosen.empty() ? "open2one" : "open2", opener)) { err = d.err; return false; }
		std::vector<std::string> p1, p2;
		for(size_t i = 0; i < stated.size(); ++i)
		{
			int t = stated[i];
			bool table = d.cls[t] == NONE;
			if(!d.Pick(t, table ? d.tabled[t] : d.pole[t], table ? "plain" : CLASS_NAME[d.cls[t]], 0, rec.ids, text)) { err = d.err; return false; }
			if(d.tabled[t])
			{
				const std::vector<McpSeedLine>* pool = Find(lib, "-", "-", "table");
				if(!pool) { err = "no signed table line in the library"; return false; }
				text += " " + (*pool)[(size_t)(d.H(kMcpSeedTraits[t], "table") % pool->size())].text;
			}
			p1.push_back(text);
		}
		if(!chosen.empty())
		{
			if(!d.Frame("faultlead", text)) { err = d.err; return false; }
			p2.push_back(text);
			for(size_t i = 0; i < chosen.size(); ++i)
			{
				if(!d.Pick(chosen[i], '~', "fault", i, rec.ids, text)) { err = d.err; return false; }
				p2.push_back(text);
			}
			if(!d.Frame("faultend", text)) { err = d.err; return false; }
			p2.push_back(text);
			if(!unwritten.empty())
			{
				if(!d.Frame("unwafter", text)) { err = d.err; return false; }
				p2.push_back(text);
			}
		}
		else if(!unwritten.empty())
		{
			if(!d.Frame(unwritten.size() == 1 && !stated.empty() ? "unw1" : "unwrest", text)) { err = d.err; return false; }
			p2.push_back(text);
		}
		for(size_t i = 0; i < unwritten.size(); ++i)
		{
			if(!d.Pick(unwritten[i], '?', "unwritten", 0, rec.ids, text)) { err = d.err; return false; }
			p2.push_back(text);
		}
		page = Replaced(Replaced(opener, "{civ}", in.civ), "{second}", in.second);
		if(!p1.empty()) page += "\n\n" + Spaced(p1);
		if(!p2.empty()) page += "\n\n" + Spaced(p2);
		if(Words(page) <= WORD_CAP || stated.empty()) break;
		size_t drop = 0;     // the weakest stated line goes, bedrock last
		for(size_t i = 1; i < stated.size(); ++i)
		{
			int bi = d.cls[stated[i]] == BEDROCK ? 1 : 0, bd = d.cls[stated[drop]] == BEDROCK ? 1 : 0;
			if(bi < bd || (bi == bd && d.Strength2(stated[i]) < d.Strength2(stated[drop]))) drop = i;
		}
		cut.push_back(stated[drop]);
		stated.erase(stated.begin() + drop);
	}

	rec.words = (int)Words(page);
	for(size_t i = 0; i < cut.size(); ++i) rec.cut.push_back(kMcpSeedTraits[cut[i]]);
	for(int t = 0; t < MCP_SEED_TRAITS; ++t)
	{
		std::string c;
		if(d.cls[t] != NONE) c = std::string(CLASS_NAME[d.cls[t]]) + d.pole[t];
		else if(Has(unwritten, t)) c = "unwritten?";
		else if(d.tabled[t]) c = std::string("table") + d.tabled[t];
		if(!c.empty()) rec.classes.push_back(std::make_pair(std::string(kMcpSeedTraits[t]), c));
	}
	return true;
}

std::string McpSeedThe(const std::string& civName)
{
	static const char* const PEOPLES[] = { "ayyubids", "aztecs", "boers", "celts", "franks", "gauls", "golden horde", "goths", "hittites", "huns", "inca",
		"iroquois", "khmer", "maya", "moors", "mughals", "netherlands", "ottomans", "philippines", "shoshone", "sioux", "songhai", "timurids", "vatican",
		"zulus", "united arab emirates", "emirates", "mongols", "maori", "normans" };
	static const char* const PLACES_IN_S[] = { "honduras", "cyprus", "texas", "wales", "tunis", "laos", "belarus", "barbados" };
	std::string low = civName;
	for(size_t i = 0; i < low.size(); ++i) if(low[i] >= 'A' && low[i] <= 'Z') low[i] = (char)(low[i] - 'A' + 'a');
	if(low.compare(0, 4, "the ") == 0) return "the " + civName.substr(4);
	bool people = false, place = false;
	for(size_t i = 0; i < sizeof(PEOPLES) / sizeof(PEOPLES[0]); ++i) if(low == PEOPLES[i]) people = true;
	for(size_t i = 0; i < sizeof(PLACES_IN_S) / sizeof(PLACES_IN_S[0]); ++i) if(low == PLACES_IN_S[i]) place = true;
	if(people || (!low.empty() && low[low.size() - 1] == 's' && !place)) return "the " + civName;
	return civName;
}

std::string McpSeedLineText(const McpSeedLibrary& lib, const std::string& id)
{
	for(McpSeedLibrary::const_iterator it = lib.begin(); it != lib.end(); ++it)
		for(size_t i = 0; i < it->second.size(); ++i) if(it->second[i].id == id) return it->second[i].text;
	return "";
}
