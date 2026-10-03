#pragma once
// 极简 JSON 解析器（只读，UTF-8），纯标准 C++，无第三方依赖。
#include <string>
#include <vector>
#include <cstdlib>

namespace YuMediaPlayer
{
	namespace MiniJson
	{
		struct Value
		{
			enum Type { Null, Bool, Number, String, Array, Object };
			Type type = Null;
			bool b = false;
			double num = 0;
			std::string str;                 // UTF-8
			std::vector<Value> arr;          // Array 的元素
			std::vector<std::string> keys;   // Object 的键
			std::vector<Value> vals;         // Object 的值（与 keys 一一对应）

			const Value* Get(const char* key) const
			{
				if (type != Object) return nullptr;
				for (size_t i = 0; i < keys.size(); i++)
					if (keys[i] == key) return &vals[i];
				return nullptr;
			}
			std::string GetString(const char* key) const
			{
				const Value* v = Get(key);
				return (v && v->type == String) ? v->str : std::string();
			}
			double GetNumber(const char* key, double def = 0) const
			{
				const Value* v = Get(key);
				return (v && v->type == Number) ? v->num : def;
			}
		};

		class Parser
		{
		public:
			explicit Parser(const std::string& s) : m_s(s), m_p(0) {}

			bool Parse(Value& out)
			{
				SkipWs();
				if (!ParseValue(out, 0)) return false;
				SkipWs();
				return m_p == m_s.size();
			}

		private:
			const std::string& m_s;
			size_t m_p;

			void SkipWs()
			{
				while (m_p < m_s.size() && (m_s[m_p] == ' ' || m_s[m_p] == '\t' || m_s[m_p] == '\r' || m_s[m_p] == '\n'))
					m_p++;
			}
			bool Match(const char* lit)
			{
				size_t n = 0;
				while (lit[n]) n++;
				if (m_s.compare(m_p, n, lit) != 0) return false;
				m_p += n;
				return true;
			}
			static void AppendUtf8(std::string& o, unsigned cp)
			{
				if (cp < 0x80) o += (char)cp;
				else if (cp < 0x800) { o += (char)(0xC0 | (cp >> 6)); o += (char)(0x80 | (cp & 0x3F)); }
				else if (cp < 0x10000) { o += (char)(0xE0 | (cp >> 12)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
				else { o += (char)(0xF0 | (cp >> 18)); o += (char)(0x80 | ((cp >> 12) & 0x3F)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
			}
			bool Hex4(unsigned& v)
			{
				if (m_p + 4 > m_s.size()) return false;
				v = 0;
				for (int i = 0; i < 4; i++)
				{
					char c = m_s[m_p++];
					v <<= 4;
					if (c >= '0' && c <= '9') v |= c - '0';
					else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
					else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10;
					else return false;
				}
				return true;
			}
			bool ParseString(std::string& out)
			{
				if (m_p >= m_s.size() || m_s[m_p] != '"') return false;
				m_p++;
				out.clear();
				while (m_p < m_s.size())
				{
					char c = m_s[m_p++];
					if (c == '"') return true;
					if (c != '\\') { out += c; continue; }
					if (m_p >= m_s.size()) return false;
					char e = m_s[m_p++];
					switch (e)
					{
					case '"': out += '"'; break;
					case '\\': out += '\\'; break;
					case '/': out += '/'; break;
					case 'b': out += '\b'; break;
					case 'f': out += '\f'; break;
					case 'n': out += '\n'; break;
					case 'r': out += '\r'; break;
					case 't': out += '\t'; break;
					case 'u':
					{
						unsigned cp;
						if (!Hex4(cp)) return false;
						if (cp >= 0xD800 && cp <= 0xDBFF && m_p + 1 < m_s.size() && m_s[m_p] == '\\' && m_s[m_p + 1] == 'u')
						{
							m_p += 2;
							unsigned lo;
							if (!Hex4(lo)) return false;
							if (lo >= 0xDC00 && lo <= 0xDFFF)
								cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
						}
						AppendUtf8(out, cp);
						break;
					}
					default: return false;
					}
				}
				return false;
			}
			bool ParseValue(Value& v, int depth)
			{
				if (depth > 64 || m_p >= m_s.size()) return false;
				char c = m_s[m_p];
				if (c == '{')
				{
					m_p++;
					v.type = Value::Object;
					SkipWs();
					if (m_p < m_s.size() && m_s[m_p] == '}') { m_p++; return true; }
					for (;;)
					{
						SkipWs();
						std::string key;
						if (!ParseString(key)) return false;
						SkipWs();
						if (m_p >= m_s.size() || m_s[m_p] != ':') return false;
						m_p++;
						SkipWs();
						Value child;
						if (!ParseValue(child, depth + 1)) return false;
						v.keys.push_back(std::move(key));
						v.vals.push_back(std::move(child));
						SkipWs();
						if (m_p >= m_s.size()) return false;
						if (m_s[m_p] == ',') { m_p++; continue; }
						if (m_s[m_p] == '}') { m_p++; return true; }
						return false;
					}
				}
				if (c == '[')
				{
					m_p++;
					v.type = Value::Array;
					SkipWs();
					if (m_p < m_s.size() && m_s[m_p] == ']') { m_p++; return true; }
					for (;;)
					{
						SkipWs();
						Value child;
						if (!ParseValue(child, depth + 1)) return false;
						v.arr.push_back(std::move(child));
						SkipWs();
						if (m_p >= m_s.size()) return false;
						if (m_s[m_p] == ',') { m_p++; continue; }
						if (m_s[m_p] == ']') { m_p++; return true; }
						return false;
					}
				}
				if (c == '"') { v.type = Value::String; return ParseString(v.str); }
				if (Match("true")) { v.type = Value::Bool; v.b = true; return true; }
				if (Match("false")) { v.type = Value::Bool; v.b = false; return true; }
				if (Match("null")) { v.type = Value::Null; return true; }
				// 数字
				const char* start = m_s.c_str() + m_p;
				char* end = nullptr;
				double d = std::strtod(start, &end);
				if (end == start) return false;
				m_p += (size_t)(end - start);
				v.type = Value::Number;
				v.num = d;
				return true;
			}
		};

		inline bool Parse(const std::string& text, Value& out)
		{
			Parser p(text);
			return p.Parse(out);
		}
	}
}
