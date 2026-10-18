#include "app/syntax_highlight.h"

#include <algorithm>
#include <string>

namespace app {
  namespace {
    enum class Language {
      Plain,
      Cpp,
      Js,
      Python,
      Rust,
      Go,
      Java,
      CSharp,
      Json,
      Shell,
      Css,
      Config,
      Yaml,
      Sql,
      Lua,
      Diff,
      Xml,
      Nix
    };
    bool alpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
    bool digit(char c) { return c >= '0' && c <= '9'; }
    bool identifier(char c) {
      return alpha(c) || digit(c) || c == '_' || c == '$' || static_cast<unsigned char>(c) >= 128;
    }
    bool space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
    bool rustLifetime(std::string_view source, std::size_t at) {
      if (source[at] != '\'')
        return false;
      auto end = at + 1;
      while (end < source.size() && identifier(source[end]))
        ++end;
      return end > at + 1 && (end == source.size() || source[end] != '\'');
    }
    bool wordIn(std::string_view words, std::string_view word) {
      if (word.empty())
        return false;
      for (auto at = words.find(word); at != std::string_view::npos; at = words.find(word, at + 1))
        if ((at == 0 || words[at - 1] == ' ') && (at + word.size() == words.size() || words[at + word.size()] == ' '))
          return true;
      return false;
    }
    Language languageFor(std::string_view name) {
      while (!name.empty() && space(name.front()))
        name.remove_prefix(1);
      while (!name.empty() && space(name.back()))
        name.remove_suffix(1);
      std::string lower(name);
      std::ranges::transform(lower, lower.begin(), [](unsigned char c) {
        return c >= 'A' && c <= 'Z' ? c + 'a' - 'A' : c;
      });
      if (wordIn("c cpp c++ cc cxx h hpp hxx", lower))
        return Language::Cpp;
      if (wordIn("js javascript jsx mjs cjs ts typescript tsx mts cts", lower))
        return Language::Js;
      if (wordIn("py python python3", lower))
        return Language::Python;
      if (wordIn("rs rust", lower))
        return Language::Rust;
      if (wordIn("go golang", lower))
        return Language::Go;
      if (lower == "java")
        return Language::Java;
      if (wordIn("cs csharp c#", lower))
        return Language::CSharp;
      if (wordIn("json jsonc json5", lower))
        return Language::Json;
      if (wordIn("sh bash zsh shell shellscript console", lower))
        return Language::Shell;
      if (wordIn("css scss", lower))
        return Language::Css;
      if (wordIn("ini toml conf config", lower))
        return Language::Config;
      if (wordIn("yaml yml", lower))
        return Language::Yaml;
      if (lower == "sql")
        return Language::Sql;
      if (lower == "lua")
        return Language::Lua;
      if (wordIn("diff patch", lower))
        return Language::Diff;
      if (wordIn("html xml svg", lower))
        return Language::Xml;
      if (wordIn("nix nixos", lower))
        return Language::Nix;
      return Language::Plain;
    }
    void appendSpan(std::vector<CodeSpan>& spans, std::size_t start, std::size_t end, CodeToken token) {
      if (!spans.empty() && spans.back().token == token)
        spans.back().length += end - start;
      else
        spans.push_back({start, end - start, token});
    }
    std::vector<CodeSpan> highlightNix(std::string_view source) {
      enum class Mode { Expression, Interpolation, Quoted, Indented, Path };
      struct Context {
        Mode mode;
        unsigned braces = 0;
      };
      std::vector<Context> contexts{{Mode::Expression}};
      std::vector<CodeSpan> result;
      const auto pathChar = [](char c) {
        return alpha(c) || digit(c) || c == '/' || c == '.' || c == '_' || c == '+' || c == '-' || c == '~';
      };
      for (std::size_t i = 0; i < source.size();) {
        if (result.size() >= 16384 || contexts.size() >= 64) {
          appendSpan(result, i, source.size(), CodeToken::Plain);
          break;
        }
        const auto start = i;
        const auto rest = source.substr(i);
        auto& context = contexts.back();
        auto token = CodeToken::Plain;
        if (context.mode == Mode::Quoted || context.mode == Mode::Indented || context.mode == Mode::Path) {
          token = CodeToken::String;
          if (context.mode == Mode::Path && !pathChar(source[i]) && !rest.starts_with("${")) {
            contexts.pop_back();
            continue;
          }
          if (context.mode == Mode::Quoted && source[i] == '\\')
            i += std::min<std::size_t>(2, source.size() - i);
          else if (context.mode == Mode::Indented && (rest.starts_with("''$") || rest.starts_with("'''")))
            i += 3;
          else if (context.mode == Mode::Indented && rest.starts_with("''\\"))
            i += std::min<std::size_t>(4, source.size() - i);
          else if (rest.starts_with("$$"))
            i += 2;
          else if (
              (context.mode == Mode::Quoted && source[i] == '"')
              || (context.mode == Mode::Indented && rest.starts_with("''"))
          ) {
            i += context.mode == Mode::Quoted ? 1 : 2;
            contexts.pop_back();
          } else if (rest.starts_with("${")) {
            i += 2;
            token = CodeToken::Preprocessor;
            contexts.push_back({Mode::Interpolation});
          } else
            ++i;
        } else if (source[i] == '#') {
          const auto end = source.find('\n', i);
          i = end == std::string_view::npos ? source.size() : end;
          token = CodeToken::Comment;
        } else if (rest.starts_with("/*")) {
          const auto end = source.find("*/", i + 2);
          i = end == std::string_view::npos ? source.size() : end + 2;
          token = CodeToken::Comment;
        } else if (source[i] == '"' || rest.starts_with("''")) {
          const auto mode = source[i] == '"' ? Mode::Quoted : Mode::Indented;
          i += mode == Mode::Quoted ? 1 : 2;
          contexts.push_back({mode});
          token = CodeToken::String;
        } else if (rest.starts_with("${")) {
          i += 2;
          contexts.push_back({Mode::Interpolation});
          token = CodeToken::Preprocessor;
        } else if (context.mode == Mode::Interpolation && source[i] == '}') {
          ++i;
          if (context.braces == 0) {
            contexts.pop_back();
            token = CodeToken::Preprocessor;
          } else
            --context.braces;
        } else if (source[i] == '{') {
          ++i;
          if (context.mode == Mode::Interpolation)
            ++context.braces;
        } else if (
            rest.starts_with("./")
            || rest.starts_with("../")
            || rest.starts_with("~/")
            || (source[i] == '/' && i + 1 < source.size() && pathChar(source[i + 1]) && source[i + 1] != '/')
        ) {
          contexts.push_back({Mode::Path});
          continue;
        } else if (source[i] == '<' && i + 1 < source.size() && (alpha(source[i + 1]) || source[i + 1] == '_')) {
          ++i;
          while (i < source.size() && pathChar(source[i]))
            ++i;
          if (i < source.size() && source[i] == '>') {
            ++i;
            token = CodeToken::String;
          }
        } else if (alpha(source[i]) || source[i] == '_') {
          ++i;
          while (i < source.size()
                 && (alpha(source[i]) || digit(source[i]) || source[i] == '_' || source[i] == '-' || source[i] == '\''))
            ++i;
          const auto word = source.substr(start, i - start);
          auto next = i;
          while (next < source.size() && space(source[next]))
            ++next;
          if (wordIn("assert else if in inherit let or rec then with", word))
            token = CodeToken::Keyword;
          else if (wordIn("true false null", word))
            token = CodeToken::Number;
          else if (
              wordIn("builtins import derivation toString map removeAttrs throw abort baseNameOf dirOf isNull", word)
          )
            token = CodeToken::Function;
          else if (
              next < source.size()
              && (source[next] == '.' || (source[next] == '=' && !source.substr(next).starts_with("==")))
          )
            token = CodeToken::Property;
        } else if (digit(source[i]) || (source[i] == '.' && i + 1 < source.size() && digit(source[i + 1]))) {
          ++i;
          while (i < source.size()
                 && (digit(source[i])
                     || source[i] == '.'
                     || source[i] == 'e'
                     || source[i] == 'E'
                     || ((source[i] == '+' || source[i] == '-') && (source[i - 1] == 'e' || source[i - 1] == 'E'))))
            ++i;
          token = CodeToken::Number;
        } else
          ++i;
        appendSpan(result, start, i, token);
      }
      return result;
    }
    std::string_view keywords(Language language) {
      switch (language) {
      case Language::Cpp:
        return "alignas alignof asm auto break case catch class concept const consteval constexpr constinit const_cast "
               "continue co_await co_return co_yield decltype default delete do dynamic_cast else enum explicit export "
               "extern final for friend goto if inline mutable namespace new noexcept operator override private "
               "protected public register reinterpret_cast requires return sizeof static static_assert static_cast "
               "struct switch template this thread_local throw try typedef typeid typename union using virtual "
               "volatile while";
      case Language::Js:
        return "as async await break case catch class const continue debugger declare default delete do else enum "
               "export extends finally for from function get if implements import in instanceof interface let new of "
               "package private protected public readonly return set static super switch this throw try type typeof "
               "var void while with yield";
      case Language::Python:
        return "and as assert async await break case class continue def del elif else except finally for from global "
               "if import in is lambda match nonlocal not or pass raise return try while with yield";
      case Language::Rust:
        return "as async await break const continue crate dyn else enum extern fn for if impl in let loop match mod "
               "move mut pub ref return self Self static struct super trait type unsafe use where while";
      case Language::Go:
        return "break case chan const continue default defer else fallthrough for func go goto if import interface map "
               "package range return select struct switch type var";
      case Language::Java:
        return "abstract assert break case catch class const continue default do else enum extends final finally for "
               "if implements import instanceof interface native new package private protected public record return "
               "sealed static strictfp super switch synchronized this throw throws transient try var volatile while "
               "yield";
      case Language::CSharp:
        return "abstract as async await base break case catch class const continue default delegate do else enum event "
               "explicit extern finally fixed for foreach if implicit in interface internal is lock namespace new "
               "operator out override params private protected public readonly record ref return sealed sizeof "
               "stackalloc static struct switch this throw try typeof unchecked unsafe using var virtual void volatile "
               "while yield";
      case Language::Shell:
        return "if then else elif fi case esac for in do done while until function select time export local readonly "
               "declare unset source alias echo printf cd read exit return";
      case Language::Sql:
        return "select from where insert into values update set delete create table alter drop join inner left right "
               "outer on as order by group having limit offset distinct union all and or not null is in exists between "
               "like primary key references default index view begin commit rollback";
      case Language::Lua:
        return "and break do else elseif end for function goto if in local not or repeat return then until while";
      default:
        return {};
      }
    }
  } // namespace

  std::vector<CodeSpan> highlightCode(std::string_view source, std::string_view language) {
    std::vector<CodeSpan> result;
    const auto lang = languageFor(language);
    // Keep work/memory bounded for unusually large imported messages.
    if (source.empty())
      return result;
    if (lang == Language::Plain || source.size() > 128 * 1024)
      return {{0, source.size()}};
    if (lang == Language::Nix)
      return highlightNix(source);
    auto emit = [&](std::size_t start, std::size_t end, CodeToken token) { appendSpan(result, start, end, token); };
    const auto nextNonSpace = [&](std::size_t at) {
      while (at < source.size() && space(source[at]))
        ++at;
      return at;
    };
    const bool cComments = lang == Language::Cpp
        || lang == Language::Js
        || lang == Language::Rust
        || lang == Language::Go
        || lang == Language::Java
        || lang == Language::CSharp
        || lang == Language::Json;
    const bool hashComments =
        lang == Language::Python || lang == Language::Shell || lang == Language::Config || lang == Language::Yaml;
    bool lineStart = true;
    bool xmlTag = false;
    for (std::size_t i = 0; i < source.size();) {
      if (result.size() >= 16384) {
        emit(i, source.size(), CodeToken::Plain);
        break;
      }
      const auto start = i;
      CodeToken token = CodeToken::Plain;
      const auto rest = source.substr(i);
      if (lang == Language::Diff) {
        const auto end = source.find('\n', i);
        i = end == std::string_view::npos ? source.size() : end + 1;
        token = rest.starts_with('+') ? CodeToken::Added
            : rest.starts_with('-')   ? CodeToken::Removed
            : rest.starts_with("@@")  ? CodeToken::Preprocessor
                                      : CodeToken::Plain;
      } else if (lang == Language::Xml && rest.starts_with("<!--")) {
        const auto end = source.find("-->", i + 4);
        i = end == std::string_view::npos ? source.size() : end + 3;
        token = CodeToken::Comment;
      } else if (lang == Language::Lua && rest.starts_with("--[[")) {
        const auto end = source.find("]]", i + 4);
        i = end == std::string_view::npos ? source.size() : end + 2;
        token = CodeToken::Comment;
      } else if (
          (cComments && rest.starts_with("//"))
          || (hashComments && source[i] == '#')
          || (lang == Language::Config && source[i] == ';')
          || ((lang == Language::Sql || lang == Language::Lua) && rest.starts_with("--"))
      ) {
        const auto end = source.find('\n', i);
        i = end == std::string_view::npos ? source.size() : end;
        token = CodeToken::Comment;
      } else if ((cComments || lang == Language::Css || lang == Language::Sql) && rest.starts_with("/*")) {
        i += 2;
        unsigned depth = 1;
        while (i < source.size() && depth) {
          if (lang == Language::Rust && source.substr(i).starts_with("/*")) {
            ++depth;
            i += 2;
          } else if (source.substr(i).starts_with("*/")) {
            --depth;
            i += 2;
          } else
            ++i;
        }
        token = CodeToken::Comment;
      } else if (lang == Language::Cpp && lineStart && source[i] == '#') {
        ++i;
        while (i < source.size() && (source[i] == ' ' || source[i] == '\t'))
          ++i;
        while (i < source.size() && alpha(source[i]))
          ++i;
        token = CodeToken::Preprocessor;
      } else if (lang == Language::Cpp && rest.starts_with("R\"")) {
        const auto open = source.find('(', i + 2);
        if (open != std::string_view::npos && open - i <= 18) {
          const auto closing = ")" + std::string(source.substr(i + 2, open - i - 2)) + "\"";
          const auto end = source.find(closing, open + 1);
          i = end == std::string_view::npos ? source.size() : end + closing.size();
        } else
          i += 2;
        token = CodeToken::String;
      } else if (lang == Language::Rust && rustLifetime(source, i)) {
        ++i;
        while (i < source.size() && identifier(source[i]))
          ++i;
        token = CodeToken::Property;
      } else if (
          source[i] == '"'
          || source[i] == '\''
          || (source[i] == '`' && (lang == Language::Js || lang == Language::Go || lang == Language::Shell))
      ) {
        const char quote = source[i++];
        const bool triple =
            (lang == Language::Python || lang == Language::Config) && source.substr(i, 2) == std::string(2, quote);
        if (triple)
          i += 2;
        while (i < source.size()) {
          if (source[i] == '\\') {
            i += std::min<std::size_t>(2, source.size() - i);
            continue;
          }
          if (source[i] == quote) {
            ++i;
            if (!triple)
              break;
            if (source.substr(i, 2) == std::string(2, quote)) {
              i += 2;
              break;
            }
          } else if (
              source[i] == '\n' && !triple && lang != Language::Js && lang != Language::Go && lang != Language::Shell
          )
            break;
          else
            ++i;
        }
        const auto next = nextNonSpace(i);
        token = next < source.size()
                && ((lang == Language::Json && source[next] == ':')
                    || (lang == Language::Config && source[next] == '='))
            ? CodeToken::Property
            : CodeToken::String;
      } else if (digit(source[i]) && (i == 0 || !identifier(source[i - 1]))) {
        ++i;
        while (i < source.size()) {
          const char c = source[i];
          if (digit(c)
              || c == '.'
              || c == '_'
              || (c >= 'a' && c <= 'f')
              || (c >= 'A' && c <= 'F')
              || c == 'x'
              || c == 'X'
              || c == 'o'
              || c == 'b')
            ++i;
          else if ((c == '+' || c == '-') && (source[i - 1] == 'e' || source[i - 1] == 'E'))
            ++i;
          else
            break;
        }
        token = CodeToken::Number;
      } else if (lang == Language::Shell && source[i] == '$') {
        ++i;
        if (i < source.size() && source[i] == '{') {
          const auto end = source.find('}', i);
          i = end == std::string_view::npos ? source.size() : end + 1;
        } else
          while (i < source.size() && identifier(source[i]))
            ++i;
        token = CodeToken::Property;
      } else if (lang == Language::Css && source[i] == '#' && i + 1 < source.size() && identifier(source[i + 1])) {
        ++i;
        while (i < source.size() && identifier(source[i]))
          ++i;
        token = CodeToken::Number;
      } else if (lang == Language::Xml && source[i] == '<') {
        ++i;
        while (i < source.size() && (identifier(source[i]) || source[i] == '/' || source[i] == '!' || source[i] == '-'))
          ++i;
        xmlTag = true;
        token = CodeToken::Keyword;
      } else if (identifier(source[i])) {
        ++i;
        while (i < source.size()
               && (identifier(source[i])
                   || ((lang == Language::Css
                        || lang == Language::Xml
                        || lang == Language::Config
                        || lang == Language::Yaml)
                       && source[i] == '-')))
          ++i;
        auto word = std::string(source.substr(start, i - start));
        if (lang == Language::Sql)
          std::ranges::transform(word, word.begin(), [](unsigned char c) {
            return c >= 'A' && c <= 'Z' ? c + 'a' - 'A' : c;
          });
        const auto next = nextNonSpace(i);
        if (wordIn("true false null nullptr None True False nil undefined NaN Infinity", word))
          token = CodeToken::Number;
        else if (wordIn(keywords(lang), word))
          token = CodeToken::Keyword;
        else if (
            wordIn(
                "bool char char8_t char16_t char32_t double float int long short signed unsigned void wchar_t string "
                "boolean byte decimal object dynamic str u8 u16 u32 u64 usize i8 i16 i32 i64 isize f32 f64 String Vec "
                "Option Result",
                word
            )
        )
          token = CodeToken::Type;
        else if (
            next < source.size()
            && ((source[next] == ':' && (lang == Language::Css || lang == Language::Yaml || lang == Language::Json))
                || (source[next] == '=' && (lang == Language::Config || (lang == Language::Xml && xmlTag))))
        )
          token = CodeToken::Property;
        else if (next < source.size() && source[next] == '(' && lang != Language::Json)
          token = CodeToken::Function;
      } else {
        if (lang == Language::Xml && source[i] == '>')
          xmlTag = false;
        ++i;
      }
      for (auto at = start; at < i; ++at) {
        if (source[at] == '\n')
          lineStart = true;
        else if (source[at] != ' ' && source[at] != '\t' && source[at] != '\r')
          lineStart = false;
      }
      emit(start, i, token);
    }
    return result;
  }
} // namespace app
