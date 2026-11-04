#include "McpClocks.h"
#include <cstdio>

#if defined(_MSC_VER)
#define MCP_SNPRINTF sprintf_s
#else
#define MCP_SNPRINTF snprintf
#endif

namespace
{
	long long Left(const McpClock& c) { return c.need100 - c.have100; }

	long long CeilDiv(long long a, long long b) { return a >= 0 ? (a + b - 1) / b : -((-a) / b); }
	long long FloorDiv(long long a, long long b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

	int TurnsAt(long long left, long long first, long long rate)
	{
		if(left <= 0 || first >= left) return 1;
		if(rate <= 0) return -1;
		return (int)(1 + CeilDiv(left - first, rate));
	}

	std::string Num(long long n)
	{
		char buf[32];
		MCP_SNPRINTF(buf, sizeof(buf), "%lld", n);
		return buf;
	}

	std::string Whole(long long x100) { return McpAmount(x100, false); }
}

int McpClockTurns(const McpClock& c)
{
	return TurnsAt(Left(c), c.first100, c.rate100);
}

long long McpClockNeedFor(const McpClock& c, int k)
{
	if(k < 1) k = 1;
	long long need = CeilDiv(Left(c) - c.first100 - (long long)(k - 1) * c.rate100, k);
	return need > 0 ? need : 0;
}

long long McpClockSlackFor(const McpClock& c, int k)
{
	if(k < 1) return 0;
	long long slack = FloorDiv(c.first100 + (long long)(k - 1) * c.rate100 - Left(c), k);
	return slack > 0 ? slack : 0;
}

long long McpClockSlack(const McpClock& c)
{
	int t = McpClockTurns(c);
	return t < 0 ? 0 : McpClockSlackFor(c, t);
}

std::string McpAmount(long long x100, bool bUp)
{
	bool bNegative = x100 < 0;
	long long a = bNegative ? -x100 : x100;
	long long tenths = (bUp != bNegative) ? CeilDiv(a, 10) : a / 10;
	std::string s = Num(tenths / 10);
	if(tenths % 10) s += "." + Num(tenths % 10);
	return (bNegative ? "-" : "") + s;
}

std::string McpClockBody(const McpClock& c)
{
	std::string head = Whole(c.have100) + "/" + Whole(c.need100) + " at " + McpAmount(c.rate100, false) + "/t: ";
	int t = McpClockTurns(c);
	if(c.gameTurns >= 0 && c.gameTurns != t)
		return head + Num(c.gameTurns) + "t by the game's count · need and slack unavailable";
	if(t < 0)
	{
		McpClock plusOne = c;
		plusOne.first100 = plusOne.rate100 = 100;
		return head + "never at this rate · at +1/t: " + Num(McpClockTurns(plusOne)) + "t";
	}
	std::string out = head + Num(t) + "t";
	if(t > 1) out += " · need +" + McpAmount(McpClockNeedFor(c, t - 1), true) + " for " + Num(t - 1) + "t";
	return out + " · slack " + McpAmount(McpClockSlack(c), false);
}

std::string McpClockLine(const McpClock& c)
{
	return c.name + " " + McpClockBody(c);
}

std::string McpClockLands(const McpClock& c, int nowTurn)
{
	int t = McpClockTurns(c);
	if(c.gameTurns >= 0 && c.gameTurns != t) t = c.gameTurns;
	return t < 0 ? std::string("never") : "T" + Num(nowTurn + t);
}

bool McpClockAtEdge(const McpClock& c)
{
	int t = McpClockTurns(c);
	if(t < 0 || (c.gameTurns >= 0 && c.gameTurns != t)) return false;
	return McpClockSlack(c) == 0;
}

std::string McpQueueLine(const std::vector<std::pair<std::string, int> >& items)
{
	std::string out;
	long long total = 0;
	for(size_t i = 0; i < items.size(); ++i)
	{
		total += items[i].second;
		out += (i ? " → " : "") + items[i].first + " " + Num(total) + "t";
	}
	return out;
}

std::string McpTargetLine(const std::string& label, const McpClock& c, int byTurn, int nowTurn)
{
	int k = byTurn - nowTurn;
	if(k < 1) return label + ": that turn has passed";
	int t = McpClockTurns(c);
	if(t >= 0 && t <= k) return label + ": on track, lands T" + Num(nowTurn + t) + " · slack " + McpAmount(McpClockSlackFor(c, k), false) + "/t";
	std::string at = Whole(Left(c)) + " left at " + McpAmount(c.rate100, false) + "/t ";
	std::string lands = t < 0 ? std::string("never lands at this rate") : "lands T" + Num(nowTurn + t);
	return label + ": " + at + lands + " · need +" + McpAmount(McpClockNeedFor(c, k), true) + "/t";
}
