#include <string_view>

#include "loghtning/loghtning.hpp"

int main() {
  auto logger = loghtning::simple_logger();
  auto file = loghtning::file_logger("loghtning-example.log", "file");

  LOGHTNING_INFO(logger, "hello from {}", std::string_view{"loghtning"});
  LOGHTNING_WARNING(
      logger, "a copied string arg survives backend formatting: {}", "yes");
  LOGHTNING_ERROR(file, "file sink value = {}", 42);

  loghtning::Backend::flush();
  loghtning::Backend::stop();
}
