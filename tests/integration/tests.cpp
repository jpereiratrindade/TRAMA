#include "trama/trama.hpp"
#include <filesystem>
#include <iostream>
using namespace trama;
namespace fs = std::filesystem;
static void ok(bool value, const char* message) { if (!value) throw std::runtime_error(message); }

int main(int argc, char** argv) {
  try {
    auto tmp = fs::temp_directory_path() / "trama-public-tests-v010";
    fs::remove_all(tmp);
    auto p = paths(tmp, argc > 1 ? argv[1] : fs::current_path());
    initialize(p);
    initialize(p); // Test idempotency
    
    auto status = verify(p);
    ok(status["ok"], "database integrity");
    ok(status["journal_mode"] == "wal", "journal mode WAL");
    ok(status["migrations"] == 7, "applied 7 migrations");
    
    // 1. Territorial Context & Catalog Tests
    auto poa_ctx = territorial_context(p, "4314902");
    ok(!poa_ctx.is_null(), "lookup Porto Alegre by IBGE code");
    ok(poa_ctx["municipio"]["nome"] == "Porto Alegre", "POA municipality name");
    ok(poa_ctx["planejamento"]["corede"]["nome"] == "Metropolitano Delta do Jacuí", "POA COREDE");
    ok(poa_ctx["planejamento"]["regiao_funcional"]["codigo"] == "rf-1", "POA RF 1");
    ok(poa_ctx["ecologia"]["bioma_predominante"] == "Pampa", "POA predominant biome");
    ok(poa_ctx["ecologia"]["biomas_ocorrentes"].is_array(), "POA occurring biomes array");
    ok(poa_ctx["status"] == "verified", "territorial status verified");
    ok(poa_ctx["dataset_version"] == "rs-territorial-2026.1", "dataset version present");

    auto caxias_ctx = territorial_context(p, "Caxias do Sul");
    ok(!caxias_ctx.is_null(), "lookup Caxias do Sul by name");
    ok(caxias_ctx["planejamento"]["corede"]["nome"] == "Serra", "Caxias COREDE Serra");
    ok(caxias_ctx["planejamento"]["regiao_funcional"]["codigo"] == "rf-3", "Caxias RF 3");
    ok(caxias_ctx["ecologia"]["bioma_predominante"] == "Mata Atlântica", "Caxias Bioma Mata Atlântica");

    auto bage_ctx = territorial_context(p, "Bagé");
    ok(!bage_ctx.is_null(), "lookup Bagé by name");
    ok(bage_ctx["planejamento"]["corede"]["nome"] == "Campanha", "Bagé COREDE Campanha");
    ok(bage_ctx["planejamento"]["regiao_funcional"]["codigo"] == "rf-8", "Bagé RF 8");
    ok(bage_ctx["ecologia"]["bioma_predominante"] == "Pampa", "Bagé Bioma Pampa");

    // 2. Empty analytics contract with territorial filters
    auto empty = overview(p, {}, {}, {}, "Metropolitano Delta do Jacuí", "Região Funcional 1", "Pampa");
    ok(empty["kpis"]["reported_attendances"] == 0, "empty analytics reported attendances");
    ok(empty["kpis"]["pending_action_items"] == 0, "empty pending actions");
    ok(empty["observations_by_status"].is_array(), "observation status contract");
    ok(empty["filters"]["corede"] == "Metropolitano Delta do Jacuí", "corede filter echoed");
    ok(empty["filters"]["functional_region"] == "Região Funcional 1", "rf filter echoed");
    ok(empty["filters"]["biome"] == "Pampa", "biome filter echoed");

    // 3. Admin setup & Backup/Restore
    setup_admin(p, "admin", "Administrador", "correct-horse-battery-staple");
    auto out = tmp / "backup.sqlite3";
    backup(p, out);
    ok(fs::file_size(out) > 0, "backup size > 0");
    auto restored = paths(tmp / "restored", argc > 1 ? argv[1] : fs::current_path());
    fs::create_directories(restored.root);
    fs::copy_file(out, restored.db);
    ok(verify(restored)["ok"], "restore verified");

    std::cout << "PASS: 7 migrations, WAL, territorial dual-axis context (POA, Caxias, Bagé), analytics filters, admin and backup/restore\n";
    fs::remove_all(tmp);
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "FAIL: " << e.what() << "\n";
    return 1;
  }
}
