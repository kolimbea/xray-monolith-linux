#include "vkHlslRewrite.h"

#include <cctype>
#include <map>
#include <vector>

namespace xrvk::hlsl
{
    namespace
    {
        bool ident_char(char c)
        {
            return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
        }
        bool space(char c)
        {
            return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v';
        }
        size_t skip_space(const std::string &s, size_t i)
        {
            while (i < s.size() && space(s[i]))
                ++i;
            return i;
        }
        // `word` at i as a whole identifier
        bool word_at(const std::string &s, size_t i, const char *word)
        {
            const size_t n = std::char_traits<char>::length(word);
            if (s.compare(i, n, word) != 0)
                return false;
            if (i > 0 && ident_char(s[i - 1]))
                return false;
            return i + n >= s.size() || !ident_char(s[i + n]);
        }
        bool in_directive_line(const std::string &s, size_t i)
        {
            const size_t ls = s.rfind('\n', i == 0 ? 0 : i - 1);
            const size_t start = ls == std::string::npos ? 0 : ls + 1;
            const size_t k = s.find_first_not_of(" \t", start);
            return k != std::string::npos && k < i && s[k] == '#';
        }

        // s[start] is just after '('; splits the arguments at depth 0. Returns false if unbalanced.
        bool split_args(const std::string &s, size_t start, std::vector<std::string> &args, size_t &end)
        {
            args.clear();
            int depth = 0;
            std::string cur;
            for (size_t i = start; i < s.size(); ++i)
            {
                const char c = s[i];
                if (c == '(' || c == '[' || c == '{')
                    ++depth;
                else if (c == ')' || c == ']' || c == '}')
                {
                    if (depth == 0)
                    {
                        args.push_back(cur);
                        end = i + 1;
                        return true;
                    }
                    --depth;
                }
                else if (c == ',' && depth == 0)
                {
                    args.push_back(cur);
                    cur.clear();
                    continue;
                }
                cur.push_back(c);
            }
            return false;
        }

        std::string trim(const std::string &s)
        {
            const size_t b = s.find_first_not_of(" \t\r\n");
            if (b == std::string::npos)
                return {};
            const size_t e = s.find_last_not_of(" \t\r\n");
            return s.substr(b, e - b + 1);
        }

        // -?\d+
        bool int_literal(const std::string &s, size_t &i)
        {
            size_t k = i;
            if (k < s.size() && s[k] == '-')
                ++k;
            const size_t d = k;
            while (k < s.size() && std::isdigit(static_cast<unsigned char>(s[k])))
                ++k;
            if (k == d)
                return false;
            i = k;
            return true;
        }

        // `-?\d+` or `int2/3/4( -?\d+ (, -?\d+)* )`, surrounded by spaces
        bool literal_offset(const std::string &a)
        {
            const std::string s = trim(a);
            size_t i = 0;
            if (int_literal(s, i))
                return i == s.size();
            if (s.size() < 5 || s.compare(0, 3, "int") != 0 || s[3] < '2' || s[3] > '4')
                return false;
            i = skip_space(s, 4);
            if (i >= s.size() || s[i] != '(')
                return false;
            do
            {
                i = skip_space(s, i + 1);
                if (!int_literal(s, i))
                    return false;
                i = skip_space(s, i);
            } while (i < s.size() && s[i] == ',');
            return i + 1 == s.size() && s[i] == ')';
        }

        // Texture2D / Texture2DArray declarations: name -> kind (first declaration wins)
        std::map<std::string, std::string> texture_kinds(const std::string &s)
        {
            std::map<std::string, std::string> kinds;
            for (size_t i = s.find("Texture2D"); i != std::string::npos; i = s.find("Texture2D", i + 1))
            {
                const bool array = word_at(s, i, "Texture2DArray");
                if (!array && !word_at(s, i, "Texture2D"))
                    continue;
                size_t k = i + (array ? 14 : 9);
                const size_t t = skip_space(s, k);
                if (t < s.size() && s[t] == '<')
                {
                    k = s.find('>', t);
                    if (k == std::string::npos)
                        break;
                    ++k;
                }
                const size_t name = skip_space(s, k);
                if (name == k || name >= s.size() || !ident_char(s[name]))
                    continue;
                size_t e = name;
                while (e < s.size() && ident_char(s[e]))
                    ++e;
                kinds.emplace(s.substr(name, e - name), array ? "Texture2DArray" : "Texture2D");
            }
            return kinds;
        }

        std::string rule_load_offset(const std::string &s)
        {
            const auto kinds = texture_kinds(s);
            std::string out;
            size_t pos = 0;
            std::vector<std::string> args;
            for (size_t i = s.find("Load"); i != std::string::npos; i = s.find("Load", i + 1))
            {
                if (i < pos || !word_at(s, i, "Load"))
                    continue;
                const size_t open = skip_space(s, i + 4);
                if (open >= s.size() || s[open] != '(')
                    continue;
                // receiver: identifier \s* . \s* Load
                size_t k = i;
                while (k > 0 && space(s[k - 1]))
                    --k;
                if (k == 0 || s[k - 1] != '.')
                    continue;
                --k;
                while (k > 0 && space(s[k - 1]))
                    --k;
                const size_t name_end = k;
                while (k > 0 && ident_char(s[k - 1]))
                    --k;
                if (k == name_end || k < pos)
                    continue;
                const auto kind = kinds.find(s.substr(k, name_end - k));
                if (kind == kinds.end() || in_directive_line(s, k))
                    continue;
                size_t end = 0;
                if (!split_args(s, open + 1, args, end) || args.size() != 2 || literal_offset(args[1]))
                    continue;
                out.append(s, pos, k - pos);
                out += kind->first + ".Load((" + trim(args[0]) + ") + ";
                out += kind->second == "Texture2D" ? "int3((" + trim(args[1]) + "), 0))"
                                                   : "int4((" + trim(args[1]) + "), 0, 0))";
                pos = end;
                i = end - 1;
            }
            out.append(s, pos, std::string::npos);
            return out;
        }

        std::string rule_loop_attribute(const std::string &s)
        {
            std::string out;
            size_t pos = 0;
            std::vector<std::string> args;
            for (size_t i = s.find('['); i != std::string::npos; i = s.find('[', i + 1))
            {
                if (i < pos)
                    continue;
                const size_t w = skip_space(s, i + 1);
                const bool unroll = word_at(s, w, "unroll");
                if (!unroll && !word_at(s, w, "loop"))
                    continue;
                const size_t open = skip_space(s, w + (unroll ? 6 : 4));
                if (open >= s.size() || s[open] != '(')
                    continue;
                size_t end = 0;
                if (!split_args(s, open + 1, args, end) || args.size() != 1)
                    continue;
                const size_t close = s.find_first_not_of(" \t", end);
                if (close == std::string::npos || s[close] != ']')
                    continue;
                size_t d = 0;
                const std::string count = trim(args[0]);
                if (int_literal(count, d) && d == count.size() && count[0] != '-')
                    continue;
                out.append(s, pos, i - pos);
                out += unroll ? "[unroll]" : "[loop]";
                pos = close + 1;
                i = close;
            }
            out.append(s, pos, std::string::npos);
            return out;
        }
    } // namespace

    std::string rewrite_compat(const std::string &text)
    {
        return rule_loop_attribute(rule_load_offset(text));
    }
} // namespace xrvk::hlsl
