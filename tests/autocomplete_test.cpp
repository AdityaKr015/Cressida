#include "app/autocomplete.h"

#include <stdexcept>

int main() {
  const auto require = [](bool ok) {
    if (!ok)
      throw std::runtime_error("Autocomplete token boundary regression");
  };
  const auto query = app::completionQuery("Hello @Maya tail", 11, 11);
  require(query && query->trigger == '@' && query->begin == 6 && query->end == 11 && query->text == "Maya");
  require(app::completionQuery("(@", 2, 2)->text.empty());
  const auto emoji = app::completionQuery("🙂 :smil", 10, 10);
  require(emoji && emoji->begin == 5 && emoji->text == "smil");
  for (const auto text :
       {"mail@example", "https://site/:smile", "`@Maya", "```nix\n:smile", "<@123>", "<:smile:123>",
        ":smile:", "\\@Maya"}) {
    const std::string value(text);
    require(!app::completionQuery(value, value.size(), value.size()));
  }
  require(!app::completionQuery("@Maya", 5, 0));
  require(!app::completionQuery("x", 10, 10));
  const std::string closed = "`literal` :smile";
  require(app::completionQuery(closed, closed.size(), closed.size())->trigger == ':');
  const std::string unicode = "@名前";
  require(app::completionQuery(unicode, unicode.size(), unicode.size())->text == "名前");
}
