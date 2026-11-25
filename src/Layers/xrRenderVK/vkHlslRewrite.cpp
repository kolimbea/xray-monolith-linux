#include "vkHlslRewrite.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
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

    namespace
    {
        // offset in `text` of `line` (1-based) of `file` according to the #line markers; npos if absent
        size_t locate(const std::string &text, const std::string &file, long line)
        {
            std::string cur_file;
            long cur_line = 1;
            size_t pos = 0;
            while (pos < text.size())
            {
                size_t end = text.find('\n', pos);
                if (end == std::string::npos)
                    end = text.size();
                const size_t k = text.find_first_not_of(" \t", pos);
                if (k < end && text.compare(k, 5, "#line") == 0)
                {
                    long n = 0;
                    char name[512] = {};
                    const std::string directive = text.substr(k, end - k);
                    if (sscanf(directive.c_str(), "#line %ld \"%511[^\"]\"", &n, name) >= 1)
                    {
                        cur_line = n;
                        if (name[0])
                            cur_file = name;
                        pos = end + 1;
                        continue;
                    }
                }
                if (cur_file == file && cur_line == line)
                    return pos;
                ++cur_line;
                pos = end + 1;
            }
            return std::string::npos;
        }

        // index of the bracket matching the one at `open` ('(' or '{'), npos if unbalanced
        size_t match(const std::string &s, size_t open)
        {
            const char o = s[open], c = o == '(' ? ')' : '}';
            int depth = 0;
            for (size_t i = open; i < s.size(); ++i)
            {
                if (s[i] == o)
                    ++depth;
                else if (s[i] == c && --depth == 0)
                    return i;
            }
            return std::string::npos;
        }

        bool uses_word(const std::string &s, size_t begin, size_t end, const std::string &w)
        {
            for (size_t i = s.find(w, begin); i != std::string::npos && i + w.size() <= end; i = s.find(w, i + 1))
                if ((i == 0 || !ident_char(s[i - 1])) && (i + w.size() >= s.size() || !ident_char(s[i + w.size()])))
                    return true;
            return false;
        }
    } // namespace

    bool drop_unused_duplicate_input(std::string &text, const std::string &diagnostics)
    {
        static const std::string kMarker = ": error: input semantic '";
        for (size_t m = diagnostics.find(kMarker); m != std::string::npos; m = diagnostics.find(kMarker, m + 1))
        {
            // "<file>:<line>:<col>: error: input semantic 'X' used more than once"
            const size_t ls = diagnostics.rfind('\n', m);
            const std::string head = diagnostics.substr(ls == std::string::npos ? 0 : ls + 1, m - (ls == std::string::npos ? 0 : ls + 1));
            const size_t c2 = head.rfind(':'), c1 = c2 == std::string::npos ? c2 : head.rfind(':', c2 - 1);
            if (c1 == std::string::npos || c1 == 0)
                continue;
            const std::string file = head.substr(0, c1);
            const long line = atol(head.c_str() + c1 + 1), col = atol(head.c_str() + c2 + 1);
            const size_t le = diagnostics.find('\n', m);
            if (diagnostics.substr(m, le == std::string::npos ? std::string::npos : le - m).find("used more than once") == std::string::npos)
                continue;
            const size_t line_at = locate(text, file, line);
            if (line_at == std::string::npos || col < 1)
                continue;
            const size_t sem = line_at + size_t(col - 1);
            // the parameter around the semantic: `type name : SEMANTIC`
            size_t colon = text.rfind(':', sem);
            if (colon == std::string::npos || colon < line_at)
                continue;
            size_t ne = colon;
            while (ne > 0 && space(text[ne - 1]))
                --ne;
            size_t nb = ne;
            while (nb > 0 && ident_char(text[nb - 1]))
                --nb;
            if (nb == ne)
                continue;
            const std::string name = text.substr(nb, ne - nb);
            // parameter bounds at depth 0 of the parameter list
            size_t pb = nb;
            int depth = 0;
            while (pb > 0)
            {
                const char ch = text[pb - 1];
                if (ch == ')' || ch == ']')
                    ++depth;
                else if ((ch == '(' || ch == '[') && depth-- == 0)
                    break;
                else if (ch == ',' && depth == 0)
                    break;
                --pb;
            }
            if (pb == 0)
                continue;
            size_t pe = sem;
            depth = 0;
            while (pe < text.size())
            {
                const char ch = text[pe];
                if (ch == '(' || ch == '[')
                    ++depth;
                else if ((ch == ')' || ch == ']') && depth-- == 0)
                    break;
                else if (ch == ',' && depth == 0)
                    break;
                ++pe;
            }
            const size_t list_open = text.rfind('(', pb);
            const size_t list_close = list_open == std::string::npos ? list_open : match(text, list_open);
            const size_t body_open = list_close == std::string::npos ? list_close : text.find('{', list_close);
            const size_t body_close = body_open == std::string::npos ? body_open : match(text, body_open);
            if (body_close == std::string::npos || uses_word(text, body_open, body_close, name))
                continue; // the body reads it: not a safe rewrite
            // remove the parameter with one of its separating commas
            size_t cut_b = pb, cut_e = pe;
            if (text[pb - 1] == ',')
                cut_b = pb - 1;
            else if (pe < text.size() && text[pe] == ',')
                cut_e = pe + 1;
            text.erase(cut_b, cut_e - cut_b);
            return true;
        }
        return false;
    }

    std::string rewrite_compat(const std::string &text)
    {
        return rule_loop_attribute(rule_load_offset(text));
    }
} // namespace xrvk::hlsl
