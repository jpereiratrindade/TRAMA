#pragma once
#include <filesystem>
#include <optional>
#include <string>
#include <nlohmann/json.hpp>
namespace trama {
using json = nlohmann::json;
struct Paths { std::filesystem::path root, db, evidence, migrations, web; };
Paths paths(const std::filesystem::path& root, const std::filesystem::path& source = {});
void initialize(const Paths& p);
json overview(const Paths& p, std::optional<std::string> group={}, std::optional<std::string> unit={}, std::optional<std::string> project={}, std::optional<std::string> corede={}, std::optional<std::string> functional_region={}, std::optional<std::string> biome={});
json territorial_context(const Paths& p, const std::string& ibge_or_name);
void setup_admin(const Paths& p, const std::string& login, const std::string& display, const std::string& password);
void backup(const Paths& p, const std::filesystem::path& out);
json verify(const Paths& p);
int cli(int argc, char** argv);
int server_main(int argc, char** argv);
}
