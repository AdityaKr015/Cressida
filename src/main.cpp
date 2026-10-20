#include "app/client_application.h"
#include "app/restart_process.h"
#include "core/environment.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string_view>

int main(int argc, char** argv) {
  bool demo = false, smoke = false;
  std::string tokenFile;
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg(argv[i]);
    if (arg == "--help") {
      std::cout << "Cressida\n\nUsage: cressida [--demo] [--smoke-test] [--token-file PATH]\n"
                   "\nAccount tokens can also be entered in the window or provided through CRESSIDA_TOKEN.\n"
                   "--demo opens sample conversations without contacting Discord.\n"
                   "--smoke-test renders one demo frame and exits.\n";
      return 0;
    }
    if (arg == "--version") {
      std::cout << "cressida 0.1.0\n";
      return 0;
    }
    if (arg == "--demo")
      demo = true;
    else if (arg == "--smoke-test") {
      smoke = true;
      demo = true;
    } else if (arg == "--token-file" && i + 1 < argc)
      tokenFile = argv[++i];
    else {
      std::cerr << "Unknown or incomplete option. Use --help.\n";
      return 2;
    }
  }
  try {
    std::string token;
    if (!demo) {
      if (!tokenFile.empty()) {
        std::ifstream stream(tokenFile);
        if (!stream)
          throw std::runtime_error("Could not open the token file");
        char bytes[4098]{};
        stream.read(bytes, sizeof(bytes));
        if (stream.gcount() > 4096)
          throw std::runtime_error("Token file is too large");
        token.assign(bytes, stream.gcount());
        while (!token.empty() && (token.back() == '\n' || token.back() == '\r'))
          token.pop_back();
      } else if (const char* value = cressida::environment("CRESSIDA_TOKEN", "DISCORD_CPP_TOKEN"))
        token = value;
    }
    int result;
    bool restart;
    {
      app::ClientApplication application(demo, std::move(token));
      result = application.run(smoke);
      restart = application.restartRequested();
    }
    if (restart && !demo)
      app::restartProcess(argv);
    return result;
  } catch (const std::exception& error) {
    std::cerr << "cressida: " << error.what() << '\n';
    return 1;
  }
}
