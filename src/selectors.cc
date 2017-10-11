#include "selectors.hh"

#include "buffer_utils.hh"
#include "context.hh"
#include "flags.hh"
#include "optional.hh"
#include "regex.hh"
#include "string.hh"
#include "unicode.hh"
#include "unit_tests.hh"
#include "utf8_iterator.hh"

#include <algorithm>

namespace Kakoune
{

using Utf8Iterator = utf8::iterator<BufferIterator>;

namespace
{

Selection target_eol(Selection sel)
{
    sel.cursor().target = INT_MAX;
    return sel;
}

Selection utf8_range(const BufferIterator& first, const BufferIterator& last)
{
    return {first.coord(), last.coord()};
}

Selection utf8_range(const Utf8Iterator& first, const Utf8Iterator& last)
{
    return {first.base().coord(), last.base().coord()};
}

ConstArrayView<Codepoint> get_extra_word_chars(const Context& context)
{
    return context.options()["extra_word_chars"].get<Vector<Codepoint, MemoryDomain::Options>>();
}

}

template<WordType word_type>
Optional<Selection>
select_to_next_word(const Context& context, const Selection& selection)
{
    auto extra_word_chars = get_extra_word_chars(context);
    auto& buffer = context.buffer();
    Utf8Iterator begin{buffer.iterator_at(selection.cursor()), buffer};
    if (begin+1 == buffer.end())
        return {};
    if (categorize<word_type>(*begin, extra_word_chars) !=
        categorize<word_type>(*(begin+1), extra_word_chars))
        ++begin;

    if (not skip_while(begin, buffer.end(),
                       [](Codepoint c) { return is_eol(c); }))
        return {};
    Utf8Iterator end = begin+1;

    auto is_word = [&](Codepoint c) { return Kakoune::is_word<word_type>(c, extra_word_chars); };

    if (is_word(*begin))
        skip_while(end, buffer.end(), is_word);
    else if (is_punctuation(*begin))
        skip_while(end, buffer.end(), is_punctuation);

    skip_while(end, buffer.end(), is_horizontal_blank);

    return utf8_range(begin, end-1);
}
template Optional<Selection> select_to_next_word<WordType::Word>(const Context&, const Selection&);
template Optional<Selection> select_to_next_word<WordType::WORD>(const Context&, const Selection&);

template<WordType word_type>
Optional<Selection>
select_to_next_word_end(const Context& context, const Selection& selection)
{
    auto extra_word_chars = get_extra_word_chars(context);
    auto& buffer = context.buffer();
    Utf8Iterator begin{buffer.iterator_at(selection.cursor()), buffer};
    if (begin+1 == buffer.end())
        return {};
    if (categorize<word_type>(*begin, extra_word_chars) !=
        categorize<word_type>(*(begin+1), extra_word_chars))
        ++begin;

    if (not skip_while(begin, buffer.end(),
                       [](Codepoint c) { return is_eol(c); }))
        return {};
    Utf8Iterator end = begin;
    skip_while(end, buffer.end(), is_horizontal_blank);

    auto is_word = [&](Codepoint c) { return Kakoune::is_word<word_type>(c, extra_word_chars); };

    if (is_word(*end))
        skip_while(end, buffer.end(), is_word);
    else if (is_punctuation(*end))
        skip_while(end, buffer.end(), is_punctuation);

    return utf8_range(begin, end-1);
}
template Optional<Selection> select_to_next_word_end<WordType::Word>(const Context&, const Selection&);
template Optional<Selection> select_to_next_word_end<WordType::WORD>(const Context&, const Selection&);

template<WordType word_type>
Optional<Selection>
select_to_previous_word(const Context& context, const Selection& selection)
{
    auto extra_word_chars = get_extra_word_chars(context);
    auto& buffer = context.buffer();
    Utf8Iterator begin{buffer.iterator_at(selection.cursor()), buffer};
    if (begin == buffer.begin())
        return {};
    if (categorize<word_type>(*begin, extra_word_chars) !=
        categorize<word_type>(*(begin-1), extra_word_chars))
        --begin;

    skip_while_reverse(begin, buffer.begin(), [](Codepoint c){ return is_eol(c); });
    Utf8Iterator end = begin;

    auto is_word = [&](Codepoint c) { return Kakoune::is_word<word_type>(c, extra_word_chars); };

    bool with_end = skip_while_reverse(end, buffer.begin(), is_horizontal_blank);
    if (is_word(*end))
        with_end = skip_while_reverse(end, buffer.begin(), is_word);
    else if (is_punctuation(*end))
        with_end = skip_while_reverse(end, buffer.begin(), is_punctuation);

    return utf8_range(begin, with_end ? end : end+1);
}
template Optional<Selection> select_to_previous_word<WordType::Word>(const Context&, const Selection&);
template Optional<Selection> select_to_previous_word<WordType::WORD>(const Context&, const Selection&);

template<WordType word_type>
Optional<Selection>
select_word(const Context& context, const Selection& selection,
            int count, ObjectFlags flags)
{
    auto extra_word_chars = get_extra_word_chars(context);
    auto& buffer = context.buffer();

    auto is_word = [&](Codepoint c) { return Kakoune::is_word<word_type>(c, extra_word_chars); };

    Utf8Iterator first{buffer.iterator_at(selection.cursor()), buffer};
    if (not is_word(*first))
        return {};

    Utf8Iterator last = first;
    if (flags & ObjectFlags::ToBegin)
    {
        skip_while_reverse(first, buffer.begin(), is_word);
        if (not is_word(*first))
            ++first;
    }
    if (flags & ObjectFlags::ToEnd)
    {
        skip_while(last, buffer.end(), is_word);
        if (not (flags & ObjectFlags::Inner))
            skip_while(last, buffer.end(), is_horizontal_blank);
        --last;
    }
    return (flags & ObjectFlags::ToEnd) ? utf8_range(first, last)
                                        : utf8_range(last, first);
}
template Optional<Selection> select_word<WordType::Word>(const Context&, const Selection&, int, ObjectFlags);
template Optional<Selection> select_word<WordType::WORD>(const Context&, const Selection&, int, ObjectFlags);

Optional<Selection>
select_line(const Context& context, const Selection& selection)
{
    auto& buffer = context.buffer();
    Utf8Iterator first{buffer.iterator_at(selection.cursor()), buffer};
    if (*first == '\n' and first + 1 != buffer.end())
        ++first;

    while (first != buffer.begin() and *(first - 1) != '\n')
        --first;

    Utf8Iterator last = first;
    while (last + 1 != buffer.end() and *last != '\n')
        ++last;
    return target_eol(utf8_range(first, last));
}

template<bool only_move>
Optional<Selection>
select_to_line_end(const Context& context, const Selection& selection)
{
    auto& buffer = context.buffer();
    BufferCoord begin = selection.cursor();
    LineCount line = begin.line;
    BufferCoord end = utf8::previous(buffer.iterator_at({line, buffer[line].length() - 1}),
                                     buffer.iterator_at(line)).coord();
    if (end < begin) // Do not go backward when cursor is on eol
        end = begin;
    return target_eol({only_move ? end : begin, end});
}
template Optional<Selection> select_to_line_end<false>(const Context&, const Selection&);
template Optional<Selection> select_to_line_end<true>(const Context&, const Selection&);

template<bool only_move>
Optional<Selection>
select_to_line_begin(const Context&, const Selection& selection)
{
    BufferCoord begin = selection.cursor();
    BufferCoord end = begin.line;
    return Selection{only_move ? end : begin, end};
}
template Optional<Selection> select_to_line_begin<false>(const Context&, const Selection&);
template Optional<Selection> select_to_line_begin<true>(const Context&, const Selection&);

Optional<Selection>
select_to_first_non_blank(const Context& context, const Selection& selection)
{
    auto& buffer = context.buffer();
    auto it = buffer.iterator_at(selection.cursor().line);
    skip_while(it, buffer.iterator_at(selection.cursor().line+1),
               is_horizontal_blank);
    return {it.coord()};
}

Optional<Selection>
select_matching(const Context& context, const Selection& selection)
{
    auto& buffer = context.buffer();
    ConstArrayView<Codepoint> matching_pairs = { '(', ')', '{', '}', '[', ']', '<', '>' };
    Utf8Iterator it{buffer.iterator_at(selection.cursor()), buffer};
    auto match = matching_pairs.end();
    while (not is_eol(*it))
    {
        match = std::find(matching_pairs.begin(), matching_pairs.end(), *it);
        if (match != matching_pairs.end())
            break;
        ++it;
    }
    if (match == matching_pairs.end())
        return {};

    Utf8Iterator begin = it;

    if (((match - matching_pairs.begin()) % 2) == 0)
    {
        int level = 0;
        const Codepoint opening = *match;
        const Codepoint closing = *(match+1);
        while (it != buffer.end())
        {
            if (*it == opening)
                ++level;
            else if (*it == closing and --level == 0)
                return utf8_range(begin, it);
            ++it;
        }
    }
    else
    {
        int level = 0;
        const Codepoint opening = *(match-1);
        const Codepoint closing = *match;
        while (true)
        {
            if (*it == closing)
                ++level;
            else if (*it == opening and --level == 0)
                return utf8_range(begin, it);
            if (it == buffer.begin())
                break;
            --it;
        }
    }
    return {};
}

template<typename Iterator, typename Container>
Optional<Iterator> find_closing(Iterator pos, Iterator end,
                                Container opening, Container closing,
                                int init_level, bool nestable)
{
    const auto opening_len = opening.end() - opening.begin();
    const auto closing_len = closing.end() - closing.begin();

    int level = nestable ? init_level : 0;

    if (end - pos >= opening_len and
        std::equal(opening.begin(), opening.end(), pos))
        pos += opening_len;

    while (pos != end)
    {
        auto close = std::search(pos, end, closing.begin(), closing.end());
        if (close == end)
            return {};

        if (nestable)
        {
            for (auto open = pos; open != close; open += opening_len)
            {
                open = std::search(open, close, opening.begin(), opening.end());
                if (open == close)
                    break;
                ++level;
            }
        }

        pos = close + closing_len;
        if (level == 0)
            return pos-1;
        --level;
    }
    return {};
}

template<typename Container, typename Iterator>
Optional<std::pair<Iterator, Iterator>>
find_surrounding(const Container& container, Iterator pos,
                 StringView opening, StringView closing,
                 ObjectFlags flags, int init_level)
{
    using std::begin; using std::end;
    const bool to_begin = flags & ObjectFlags::ToBegin;
    const bool to_end   = flags & ObjectFlags::ToEnd;
    const bool nestable = opening != closing;

    auto first = pos;
    if (to_begin and opening != *pos)
    {
        using RevIt = std::reverse_iterator<Iterator>;
        auto res = find_closing(RevIt{pos+1}, RevIt{begin(container)},
                                closing | reverse(), opening | reverse(),
                                init_level, nestable);
        if (not res)
            return {};

        first = res->base() - 1; 
    }

    auto last = pos;
    if (to_end)
    {
        auto res = find_closing(pos, end(container), opening, closing,
                                init_level, nestable);
        if (not res)
            return {};

        last = *res; 
    }

    if (flags & ObjectFlags::Inner)
    {
        if (to_begin and first != last)
            first += (int)opening.length();
        if (to_end and first != last)
            last -= (int)closing.length();
    }
    return to_end ? std::pair<Iterator, Iterator>{first, last}
                  : std::pair<Iterator, Iterator>{last, first};
}

Optional<Selection>
select_surrounding(const Context& context, const Selection& selection,
                   StringView opening, StringView closing, int level,
                   ObjectFlags flags)
{
    auto& buffer = context.buffer();
    const bool nestable = opening != closing;
    auto pos = selection.cursor();
    if (not nestable or flags & ObjectFlags::Inner)
    {
        if (auto res = find_surrounding(buffer, buffer.iterator_at(pos),
                                        opening, closing, flags, level))
            return utf8_range(res->first, res->second);
        return {};
    }

    auto c = buffer.byte_at(pos);
    if ((flags == ObjectFlags::ToBegin and c == opening) or
        (flags == ObjectFlags::ToEnd and c == closing))
        ++level;

    auto res = find_surrounding(buffer, buffer.iterator_at(pos),
                                opening, closing, flags, level);
    if (not res)
        return {};

    Selection sel = utf8_range(res->first, res->second);

    if (flags != (ObjectFlags::ToBegin | ObjectFlags::ToEnd) or
        sel.min() != selection.min() or sel.max() != selection.max())
        return sel;

    if (auto res_parent = find_surrounding(buffer, buffer.iterator_at(pos),
                                           opening, closing, flags, level+1))
        return utf8_range(res_parent->first, res_parent->second);
    return {};
}

Optional<Selection>
select_to(const Context& context, const Selection& selection,
          Codepoint c, int count, bool inclusive)
{
    auto& buffer = context.buffer();
    Utf8Iterator begin{buffer.iterator_at(selection.cursor()), buffer};
    Utf8Iterator end = begin;
    do
    {
        ++end;
        skip_while(end, buffer.end(), [c](Codepoint cur) { return cur != c; });
        if (end == buffer.end())
            return {};
    }
    while (--count > 0);

    return utf8_range(begin, inclusive ? end : end-1);
}

Optional<Selection>
select_to_reverse(const Context& context, const Selection& selection,
                  Codepoint c, int count, bool inclusive)
{
    auto& buffer = context.buffer();
    Utf8Iterator begin{buffer.iterator_at(selection.cursor()), buffer};
    Utf8Iterator end = begin;
    do
    {
        --end;
        if (skip_while_reverse(end, buffer.begin(),
                               [c](Codepoint cur) { return cur != c; }))
            return {};
    }
    while (--count > 0);

    return utf8_range(begin, inclusive ? end : end+1);
}

Optional<Selection>
select_number(const Context& context, const Selection& selection,
              int count, ObjectFlags flags)
{
    auto is_number = [&](char c) {
        return (c >= '0' and c <= '9') or
               (not (flags & ObjectFlags::Inner) and c == '.');
    };

    auto& buffer = context.buffer();
    BufferIterator first = buffer.iterator_at(selection.cursor());
    BufferIterator last = first;

    if (not is_number(*first) and *first != '-')
        return {};

    if (flags & ObjectFlags::ToBegin)
    {
        skip_while_reverse(first, buffer.begin(), is_number);
        if (not is_number(*first) and *first != '-' and
            first+1 != buffer.end())
            ++first;
    }

    if (flags & ObjectFlags::ToEnd)
    {
        if (*last == '-')
            ++last;
        skip_while(last, buffer.end(), is_number);
        if (last != buffer.begin())
            --last;
    }

    return (flags & ObjectFlags::ToEnd) ? Selection{first.coord(), last.coord()}
                                        : Selection{last.coord(), first.coord()};
}

Optional<Selection>
select_sentence(const Context& context, const Selection& selection,
                int count, ObjectFlags flags)
{
    auto is_end_of_sentence = [](char c) {
        return c == '.' or c == ';' or c == '!' or c == '?';
    };

    auto& buffer = context.buffer();
    BufferIterator first = buffer.iterator_at(selection.cursor());

    if (not (flags & ObjectFlags::ToEnd))
    {
        BufferIterator prev_non_blank = first-1;
        skip_while_reverse(prev_non_blank, buffer.begin(),
                           [](char c) { return is_horizontal_blank(c) or is_eol(c); });
        if (is_end_of_sentence(*prev_non_blank))
            first = prev_non_blank;
    }

    BufferIterator last = first;

    if (flags & ObjectFlags::ToBegin)
    {
        bool saw_non_blank = false;
        while (first != buffer.begin())
        {
            char cur = *first;
            char prev = *(first-1);
            if (not is_horizontal_blank(cur))
                saw_non_blank = true;
            if (is_eol(prev) and is_eol(cur))
            {
                ++first;
                break;
            }
            else if (is_end_of_sentence(prev))
            {
                if (saw_non_blank)
                    break;
                else if (flags & ObjectFlags::ToEnd)
                    last = first-1;
            }
            --first;
        }
        skip_while(first, buffer.end(), is_horizontal_blank);
    }
    if (flags & ObjectFlags::ToEnd)
    {
        while (last != buffer.end())
        {
            char cur = *last;
            if (is_end_of_sentence(cur) or
                (is_eol(cur) and (last+1 == buffer.end() or is_eol(*(last+1)))))
                break;
            ++last;
        }
        if (not (flags & ObjectFlags::Inner) and last != buffer.end())
        {
            ++last;
            skip_while(last, buffer.end(), is_horizontal_blank);
            --last;
        }
    }
    return (flags & ObjectFlags::ToEnd) ? Selection{first.coord(), last.coord()}
                                        : Selection{last.coord(), first.coord()};
}

Optional<Selection>
select_paragraph(const Context& context, const Selection& selection,
                 int count, ObjectFlags flags)
{
    auto& buffer = context.buffer();
    BufferIterator first = buffer.iterator_at(selection.cursor());

    if (not (flags & ObjectFlags::ToEnd) and first.coord() > BufferCoord{0,1} and
        *(first-1) == '\n' and *(first-2) == '\n')
        --first;
    else if ((flags & ObjectFlags::ToEnd) and
             first != buffer.begin() and (first+1) != buffer.end() and
             *(first-1) == '\n' and *first == '\n')
        ++first;

    BufferIterator last = first;

    if ((flags & ObjectFlags::ToBegin) and first != buffer.begin())
    {
        skip_while_reverse(first, buffer.begin(),
                           [](Codepoint c){ return is_eol(c); });
        if (flags & ObjectFlags::ToEnd)
            last = first;
        while (first != buffer.begin())
        {
            char cur = *first;
            char prev = *(first-1);
            if (is_eol(prev) and is_eol(cur))
            {
                ++first;
                break;
            }
            --first;
        }
    }
    if (flags & ObjectFlags::ToEnd)
    {
        if (last != buffer.end() and is_eol(*last))
            ++last;
        while (last != buffer.end())
        {
            if (last != buffer.begin() and is_eol(*last) and is_eol(*(last-1)))
            {
                if (not (flags & ObjectFlags::Inner))
                    skip_while(last, buffer.end(),
                               [](Codepoint c){ return is_eol(c); });
                break;
            }
            ++last;
        }
        --last;
    }
    return (flags & ObjectFlags::ToEnd) ? Selection{first.coord(), last.coord()}
                                        : Selection{last.coord(), first.coord()};
}

Optional<Selection>
select_whitespaces(const Context& context, const Selection& selection,
                   int count, ObjectFlags flags)
{
    auto is_whitespace = [&](char c) {
        return c == ' ' or c == '\t' or
            (not (flags & ObjectFlags::Inner) and c == '\n');
    };
    auto& buffer = context.buffer();
    BufferIterator first = buffer.iterator_at(selection.cursor());
    BufferIterator last  = first;

    if (not is_whitespace(*first))
        return {};

    if (flags & ObjectFlags::ToBegin)
    {
        if (is_whitespace(*first))
        {
            skip_while_reverse(first, buffer.begin(), is_whitespace);
            if (not is_whitespace(*first))
                ++first;
        }
    }
    if (flags & ObjectFlags::ToEnd)
    {
        if (is_whitespace(*last))
        {
            skip_while(last, buffer.end(), is_whitespace);
            --last;
        }
    }
    return (flags & ObjectFlags::ToEnd) ? Selection{first.coord(), last.coord()}
                                        : Selection{last.coord(), first.coord()};
}

Optional<Selection>
select_indent(const Context& context, const Selection& selection,
              int count, ObjectFlags flags)
{
    auto get_indent = [](StringView str, int tabstop) {
        CharCount indent = 0;
        for (auto& c : str)
        {
            if (c == ' ')
                ++indent;
            else if (c =='\t')
                indent = (indent / tabstop + 1) * tabstop;
            else
                break;
        }
        return indent;
    };

    auto is_only_whitespaces = [](StringView str) {
        auto it = str.begin();
        skip_while(it, str.end(),
                   [](char c){ return c == ' ' or c == '\t' or c == '\n'; });
        return it == str.end();
    };

    const bool to_begin = flags & ObjectFlags::ToBegin;
    const bool to_end   = flags & ObjectFlags::ToEnd;

    auto& buffer = context.buffer();
    int tabstop = context.options()["tabstop"].get<int>();
    auto pos = selection.cursor();
    LineCount line = pos.line;
    auto indent = get_indent(buffer[line], tabstop);

    LineCount begin_line = line - 1;
    if (to_begin)
    {
        while (begin_line >= 0 and (buffer[begin_line] == StringView{"\n"} or
                                    get_indent(buffer[begin_line], tabstop) >= indent))
            --begin_line;
    }
    ++begin_line;
    LineCount end_line = line + 1;
    if (to_end)
    {
        const LineCount end = buffer.line_count();
        while (end_line < end and (buffer[end_line] == StringView{"\n"} or
                                   get_indent(buffer[end_line], tabstop) >= indent))
            ++end_line;
    }
    --end_line;
    // remove only whitespaces lines in inner mode
    if (flags & ObjectFlags::Inner)
    {
        while (begin_line < end_line and
               is_only_whitespaces(buffer[begin_line]))
            ++begin_line;
        while (begin_line < end_line and
               is_only_whitespaces(buffer[end_line]))
            --end_line;
    }

    auto first = to_begin ? begin_line : pos;
    auto last = to_end ? BufferCoord{end_line, buffer[end_line].length() - 1} : pos;
    return to_end ? Selection{first, last} : Selection{last, first};
}

Optional<Selection>
select_argument(const Context& context, const Selection& selection,
                int level, ObjectFlags flags)
{
    enum Class { None, Opening, Closing, Delimiter };
    auto classify = [](Codepoint c) {
        switch (c)
        {
        case '(': case '[': case '{': return Opening;
        case ')': case ']': case '}': return Closing;
        case ',': case ';': return Delimiter;
        default: return None;
        }
    };

    auto& buffer = context.buffer();
    BufferIterator pos = buffer.iterator_at(selection.cursor());
    switch (classify(*pos))
    {
        //case Closing: if (pos+1 != buffer.end()) ++pos; break;
        case Opening:
        case Delimiter: if (pos != buffer.begin()) --pos; break;
        default: break;
    };

    bool first_arg = false;
    BufferIterator begin = pos;
    for (int lev = level; begin != buffer.begin(); --begin)
    {
        Class c = classify(*begin);
        if (c == Closing)
            ++lev;
        else if (c == Opening and (lev-- == 0))
        {
            first_arg = true;
            ++begin;
            break;
        }
        else if (c == Delimiter and lev == 0)
        {
            ++begin;
            break;
        }
    }

    bool last_arg = false;
    BufferIterator end = pos;
    for (int lev = level; end != buffer.end(); ++end)
    {
        Class c = classify(*end);
        if (c == Opening)
            ++lev;
        else if (end != pos and c == Closing and (lev-- == 0))
        {
            last_arg = true;
            --end;
            break;
        }
        else if (c == Delimiter and lev == 0)
        {
            // include whitespaces *after* the delimiter only for first argument
            if (first_arg and not (flags & ObjectFlags::Inner))
            {
                while (end + 1 != buffer.end() and is_blank(*(end+1)))
                    ++end;
            }
            break;
        }
    }

    if (flags & ObjectFlags::Inner)
    {
        if (not last_arg)
            --end;
        skip_while(begin, end, is_blank);
        skip_while_reverse(end, begin, is_blank);
    }
    // get starting delimiter for non inner last arg
    else if (not first_arg and last_arg)
        --begin;

    if (end == buffer.end())
        --end;

    if (flags & ObjectFlags::ToBegin and not (flags & ObjectFlags::ToEnd))
        return Selection{pos.coord(), begin.coord()};
    return Selection{(flags & ObjectFlags::ToBegin ? begin : pos).coord(),
                     end.coord()};
}

Optional<Selection>
select_lines(const Context& context, const Selection& selection)
{
    auto& buffer = context.buffer();
    BufferCoord anchor = selection.anchor();
    BufferCoord cursor  = selection.cursor();
    BufferCoord& to_line_start = anchor <= cursor ? anchor : cursor;
    BufferCoord& to_line_end = anchor <= cursor ? cursor : anchor;

    to_line_start.column = 0;
    to_line_end.column = buffer[to_line_end.line].length()-1;

    return target_eol({anchor, cursor});
}

Optional<Selection>
trim_partial_lines(const Context& context, const Selection& selection)
{
    auto& buffer = context.buffer();
    BufferCoord anchor = selection.anchor();
    BufferCoord cursor  = selection.cursor();
    BufferCoord& to_line_start = anchor <= cursor ? anchor : cursor;
    BufferCoord& to_line_end = anchor <= cursor ? cursor : anchor;

    if (to_line_start.column != 0)
        to_line_start = to_line_start.line+1;
    if (to_line_end.column != buffer[to_line_end.line].length()-1)
    {
        if (to_line_end.line == 0)
            return {};

        auto prev_line = to_line_end.line-1;
        to_line_end = BufferCoord{ prev_line, buffer[prev_line].length()-1 };
    }

    if (to_line_start > to_line_end)
        return {};

    return target_eol({anchor, cursor});
}

void select_buffer(SelectionList& selections)
{
    auto& buffer = selections.buffer();
    selections = SelectionList{ buffer, target_eol({{0,0}, buffer.back_coord()}) };
}

static RegexExecFlags
match_flags(const Buffer& buf, const BufferIterator& begin, const BufferIterator& end)
{
    return match_flags(is_bol(begin.coord()), is_eol(buf, end.coord()),
                       is_bow(buf, begin.coord()), is_eow(buf, end.coord()));
}

static bool find_next(const Buffer& buffer, const BufferIterator& pos,
                      MatchResults<BufferIterator>& matches,
                      const Regex& ex, bool& wrapped)
{
    if (pos != buffer.end() and
        regex_search(pos, buffer.end(), matches, ex,
                     match_flags(buffer, pos, buffer.end())))
        return true;
    wrapped = true;
    return regex_search(buffer.begin(), buffer.end(), matches, ex,
                        match_flags(buffer, buffer.begin(), buffer.end()));
}

static bool find_prev(const Buffer& buffer, const BufferIterator& pos,
                      MatchResults<BufferIterator>& matches,
                      const Regex& ex, bool& wrapped)
{
    if (pos != buffer.begin() and
        regex_search<BufferIterator, MatchDirection::Backward>(
            buffer.begin(), pos, matches, ex,
            match_flags(buffer, buffer.begin(), pos) | RegexExecFlags::NotInitialNull))
        return true;
    wrapped = true;
    return regex_search<BufferIterator, MatchDirection::Backward>(
        buffer.begin(), buffer.end(), matches, ex,
        match_flags(buffer, buffer.begin(), buffer.end()) | RegexExecFlags::NotInitialNull);
}

template<MatchDirection direction>
Selection find_next_match(const Context& context, const Selection& sel, const Regex& regex, bool& wrapped)
{
    auto& buffer = context.buffer();
    MatchResults<BufferIterator> matches;
    auto pos = buffer.iterator_at(direction == MatchDirection::Backward ? sel.min() : sel.max());
    wrapped = false;
    const bool found = (direction == MatchDirection::Forward) ?
        find_next(buffer, utf8::next(pos, buffer.end()), matches, regex, wrapped)
      : find_prev(buffer, pos, matches, regex, wrapped);

    if (not found or matches[0].first == buffer.end())
        throw runtime_error(format("'{}': no matches found", regex.str()));

    CaptureList captures;
    for (const auto& match : matches)
        captures.push_back(buffer.string(match.first.coord(), match.second.coord()));

    auto begin = matches[0].first, end = matches[0].second;
    end = (begin == end) ? end : utf8::previous(end, begin);
    if (direction == MatchDirection::Backward)
        std::swap(begin, end);

    return {begin.coord(), end.coord(), std::move(captures)};
}
template Selection find_next_match<MatchDirection::Forward>(const Context&, const Selection&, const Regex&, bool&);
template Selection find_next_match<MatchDirection::Backward>(const Context&, const Selection&, const Regex&, bool&);

using RegexIt = RegexIterator<BufferIterator>;

void select_all_matches(SelectionList& selections, const Regex& regex, int capture)
{
    const int mark_count = (int)regex.mark_count();
    if (capture < 0 or capture > mark_count)
        throw runtime_error("invalid capture number");

    Vector<Selection> result;
    auto& buffer = selections.buffer();
    for (auto& sel : selections)
    {
        auto sel_beg = buffer.iterator_at(sel.min());
        auto sel_end = utf8::next(buffer.iterator_at(sel.max()), buffer.end());
        RegexIt re_it(sel_beg, sel_end, regex,
                      match_flags(buffer, sel_beg, sel_end));
        RegexIt re_end;

        for (; re_it != re_end; ++re_it)
        {
            auto begin = (*re_it)[capture].first;
            if (begin == sel_end)
                continue;
            auto end = (*re_it)[capture].second;

            CaptureList captures;
            captures.reserve(mark_count);
            for (const auto& match : *re_it)
                captures.push_back(buffer.string(match.first.coord(),
                                                 match.second.coord()));

            result.push_back(
                keep_direction({ begin.coord(),
                                 (begin == end ? end : utf8::previous(end, begin)).coord(),
                                 std::move(captures) }, sel));
        }
    }
    if (result.empty())
        throw runtime_error("nothing selected");

    // Avoid SelectionList::operator=(Vector<Selection>) as we know result is
    // already sorted and non overlapping.
    selections = SelectionList{buffer, std::move(result)};
}

void split_selections(SelectionList& selections, const Regex& regex, int capture)
{
    if (capture < 0 or capture > (int)regex.mark_count())
        throw runtime_error("invalid capture number");

    Vector<Selection> result;
    auto& buffer = selections.buffer();
    auto buf_end = buffer.end();
    auto buf_begin = buffer.begin();
    for (auto& sel : selections)
    {
        auto begin = buffer.iterator_at(sel.min());
        auto sel_end = utf8::next(buffer.iterator_at(sel.max()), buffer.end());

        RegexIt re_it(begin, sel_end, regex,
                      match_flags(buffer, begin, sel_end));
        RegexIt re_end;

        for (; re_it != re_end; ++re_it)
        {
            BufferIterator end = (*re_it)[capture].first;
            if (end == buf_end)
                continue;

            if (end != buf_begin)
            {
                auto sel_end = (begin == end) ? end : utf8::previous(end, begin);
                result.push_back(keep_direction({ begin.coord(), sel_end.coord() }, sel));
            }
            begin = (*re_it)[capture].second;
        }
        if (begin.coord() <= sel.max())
            result.push_back(keep_direction({ begin.coord(), sel.max() }, sel));
    }
    if (result.empty())
        throw runtime_error("nothing selected");

    selections = std::move(result);
}

UnitTest test_find_surrounding{[]()
{
    StringView s("[salut { toi[] }]");
    auto check_equal = [&](const char* pos, StringView opening, StringView closing,
                           ObjectFlags flags, int init_level, StringView expected) {
        auto res = find_surrounding(s, pos, opening, closing, flags, init_level);
        auto min = std::min(res->first, res->second),
             max = std::max(res->first, res->second);
        kak_assert(res and StringView{min, max+1} == expected);
    };

    check_equal(s.begin() + 10, '{', '}', ObjectFlags::ToBegin | ObjectFlags::ToEnd, 0, "{ toi[] }");
    check_equal(s.begin() + 10, '[', ']', ObjectFlags::ToBegin | ObjectFlags::ToEnd | ObjectFlags::Inner, 0, "salut { toi[] }");
    check_equal(s.begin(), '[', ']', ObjectFlags::ToBegin | ObjectFlags::ToEnd, 0, "[salut { toi[] }]");
    check_equal(s.begin()+7, '{', '}', ObjectFlags::ToBegin | ObjectFlags::ToEnd, 0, "{ toi[] }");
    check_equal(s.begin() + 12, '[', ']', ObjectFlags::ToBegin | ObjectFlags::ToEnd | ObjectFlags::Inner, 0, "]");
    check_equal(s.begin() + 14, '[', ']', ObjectFlags::ToBegin | ObjectFlags::ToEnd, 0, "[salut { toi[] }]");
    check_equal(s.begin() + 1, '[', ']', ObjectFlags::ToBegin, 0, "[s");

    s = "[]";
    check_equal(s.begin() + 1, '[', ']', ObjectFlags::ToBegin | ObjectFlags::ToEnd, 0, "[]");

    s = "[*][] hehe";
    kak_assert(not find_surrounding(s, s.begin() + 6, '[', ']', ObjectFlags::ToBegin, 0));

    s = "begin tchou begin tchaa end end";
    check_equal(s.begin() + 6, "begin", "end", ObjectFlags::ToBegin | ObjectFlags::ToEnd, 0, s);
}};

}
